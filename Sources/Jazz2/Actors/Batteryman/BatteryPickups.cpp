#include "BatteryPickups.h"
#include "BatteryWorld.h"
#include "BatterymanPlayer.h"

// Ports of the routines in teardown/41-objects-pickups-ports.md; "+0xNN" is the object record, "0x40xxxx" the
// instruction the behaviour was read from. T is the tick counter _DAT_004b11c0.

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		/** @brief Tile of a 16.16 coordinate, `(v + (v >> 31 & 0x1fffff)) >> 21` */
		std::int32_t ToTile(std::int32_t v) {
			return (v + ((v >> 31) & 0x1fffff)) >> 21;
		}

		/** @brief `FUN_00408910(tx, ty) + FUN_00408950(tx, ty, 0, 6)` - the link id of an event: 64·bits 8..9 + bits 12..17 */
		std::int32_t LinkIdAt(BatteryWorld* world, std::int32_t tx, std::int32_t ty) {
			return (std::int32_t)((world->GetEvent(tx, ty) >> 2) & 0xc0) + world->GetEventParam(tx, ty, 0, 6);
		}

		/** @brief `param(offset, 1) * 2 - 1`: bit set → +1, clear → -1 */
		std::int8_t FacingAt(BatteryWorld* world, std::int32_t tx, std::int32_t ty, std::int32_t offset) {
			return (std::int8_t)(world->GetEventParam(tx, ty, offset, 1) * 2 - 1);
		}

		/** @brief Signed division by 64 truncating toward zero, `(v + (v >> 31 & 0x3f)) >> 6` */
		std::int32_t Div64(std::int32_t v) {
			return (v + ((v >> 31) & 0x3f)) >> 6;
		}
	}

	void Pickup::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		std::uint8_t state = U8(0x41);

		if (state == 0x1f) {
			// Collected (0x406f40): flies toward its HUD panel for 61 drawn ticks - weight n/64 of the destination,
			// so it never quite arrives - on layer 2 in mode 9, then frees its slot
			std::int32_t camX = p->GetCameraX16(), camY = p->GetCameraY16();
			constexpr std::int32_t W = BatterymanPlayer::ViewWidth, H = BatterymanPlayer::ViewHeight;
			std::int32_t destX, destY, bordAnim;
			switch (U8(0x44)) {
				case 0x3c: destX = camX + 32 * 0x10000; destY = camY + (H - 32) * 0x10000; bordAnim = 9; break;
				case 0x3e: destX = camX + 32 * 0x10000; destY = camY + 32 * 0x10000; bordAnim = 0xb; break;
				case 0x3f: destX = camX + (W - 32) * 0x10000; destY = camY + (H - 32) * 0x10000; bordAnim = 0xa; break;
				default: destX = camX + (W - 32) * 0x10000; destY = camY + 32 * 0x10000; bordAnim = 8; break;
			}
			std::int32_t n = I32(0x50);
			std::int32_t x = Div64(destX) * n + Div64(I32(0x04)) * (0x40 - n);
			std::int32_t y = Div64(destY) * n + Div64(I32(0x08)) * (0x40 - n);
			U16(0x34) = MakeAnimHandle((std::int32_t)AnimSet::Bord, bordAnim);
			std::int32_t k = ++I32(0x24);
			std::int32_t fc = std::max(FrameCountOf(U16(0x34)), 1);
			std::int32_t frame = (k / 4) % fc;
			DrawSprite(x, y, 2, S8(0x45), 9, 0, FrameOf(U16(0x34), frame));
			if (0x3c < ++I32(0x50)) {
				_world->Destroy(_slot);
			}
			return;
		}

		if (state == 0) {
			// 0x40715d: first update, nothing drawn yet
			U8(0x41) = 0x1c;
			I32(0x50) = 0;
			return;
		}

		// Idle (0x4070d9): bobs ±8 px with period 256 ticks, all pickups in phase; a frame every 8th tick
		I32(0x10) = I32(0x08) + BatteryWorld::Sine((std::int32_t)((t * 4) & 0x3ff)) * 8;
		if ((t & 7) == 0) {
			std::int32_t fc = FrameCountOf(U16(0x34));
			U8(0x3c) = (U8(0x3c) + 1 >= fc ? 0 : U8(0x3c) + 1);
		}
		U32(0x28) = FrameOf(U16(0x34), U8(0x3c));
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
	}

	void Receptacle::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		constexpr std::int32_t Batterij = (std::int32_t)AnimSet::Batterij, Man = (std::int32_t)AnimSet::Man;

		if (U8(0x41) == 0) {
			// Init (0x4068ae), falls through: link id and facing from the event, owner of the id, 11 px lower
			std::int32_t tx = ToTile(I32(0x0c)), ty = ToTile(I32(0x10));
			std::int32_t id = LinkIdAt(_world, tx, ty);
			I32(0x50) = id;
			S8(0x45) = FacingAt(_world, tx, ty, 6);
			_world->LinkOwner(id) = _slot;
			_world->Trigger(id) = 0;
			U8(0x41) = 0x1c;
			I32(0x10) += 0xb0000;
			U8(0x3e) = 0;
		}

		std::int32_t id = I32(0x50);
		std::int8_t facing = S8(0x45);
		// Power decays one step every 4th tick (0x40693c)
		if (_world->Trigger(id) != 0 && (t & 3) == 0) {
			_world->Trigger(id)--;
		}
		std::int32_t power = _world->Trigger(id);
		// The battery is in at c = 34: full power and one more activation (0x40695d)
		if (U8(0x41) == 0x10 && I32(0x24) >= 0x22) {
			_world->Trigger(id) = 0xff;
			_world->ActivationCount(id)++;
			U8(0x41) = 8;
			LOGI("[BatteryObject] tick {} slot {} receptacle id {} powered, activations {}", t, _slot, id, _world->ActivationCount(id));
		}

		std::int32_t x = I32(0x0c), y = I32(0x10);
		std::int32_t slotX = x - ((std::int32_t)facing << 20);
		std::int32_t slotY = y - 0x1f0000;
		std::int32_t fullIcon = (_world->IsBossMode() ? 0x3f : 6);

		if (U8(0x41) != 8 && U8(0x41) != 0x10) {
			// Idle (0x406997): body, the slot icon (empty without power, else full - a superbattery in the boss
			// fight - never mirrored), and with power the dial, spinning faster with more power, and the gauge
			std::uint16_t body = MakeAnimHandle(Batterij, 0);
			U32(0x28) = FrameOf(body, 0);
			DrawSprite(x, y, 3, facing, 0, 0, U32(0x28));
			std::int32_t icon = (power <= 0 ? 4 : fullIcon);
			DrawSprite(slotX, slotY, 3, 0, 0, 0, FrameOf(MakeAnimHandle(Man, icon), 0));
			if (power > 0 && _world->IsDrawingTick()) {
				std::uint32_t r = ((t << 9) / (std::uint32_t)(0x1100 - 16 * power)) % 6;
				DrawSprite(x, y, 3, facing, 0, 0, FrameOf(body, 0x12 + (std::int32_t)r));
				const FrameInfo* gauge = BatteryAnims::Get().GetFrame(Batterij, 0, 0x18);
				std::int32_t gaugeHeight = (gauge != nullptr ? gauge->Height : 0);
				DrawSprite(x, y, 3, facing, 8, (gaugeHeight * (0x100 - power)) >> 8, FrameOf(body, 0x18));
			}
			return;
		}

		// Insertion (0x406aef): c runs from -40 to 53; the press arm on 0..33, the full icon from 34, and from 0 the
		// empty battery the player threw arcing from the slot toward the player
		std::int32_t c = I32(0x24);
		std::uint16_t body = MakeAnimHandle(Batterij, 0);
		U32(0x28) = FrameOf(body, (c >= 0 && c <= 33) ? c / 2 : 0);
		DrawSprite(x, y, 3, facing, 0, 0, U32(0x28));
		if (c < 0) {
			DrawSprite(slotX, slotY, 3, 0, 0, 0, FrameOf(MakeAnimHandle(Man, 4), 0));
		} else {
			if (c >= 34) {
				DrawSprite(slotX, slotY, 3, 0, 0, 0, FrameOf(MakeAnimHandle(Man, fullIcon), 0));
			}
			std::int32_t fx = slotX + ((p->X16() - slotX) * c) / 54;
			std::int32_t fy = y - ((BatteryWorld::Sine(((c << 9) >> 6) & 0x3ff) + 0x3e00) << 7);
			std::uint32_t frames = _world->GetRenderedFrames();
			std::uint32_t frame;
			if (c < 4) {
				frame = FrameOf(MakeAnimHandle(Man, 4), 0);
			} else if (c < 16) {
				frame = FrameOf(MakeAnimHandle(Man, 3), (std::int32_t)(frames % 2));
			} else {
				frame = FrameOf(MakeAnimHandle(Man, 2), (std::int32_t)((frames >> 2) % 5));
			}
			DrawSprite(fx, fy, 3, facing, 0, 0, frame);
		}

		c++;
		I32(0x24) = c;
		if (c == 4) {
			// 0x406d14: sSND_B021, non-positional
			PlaySample(0, 0, 0x20, 0, 0);
		}
		if (c >= 0x36) {
			I32(0x24) = 0;
			U8(0x41) = 0x1c;
			LOGI("[BatteryObject] tick {} slot {} receptacle id {} idle again", t, _slot, id);
		}
	}

	void PlasmaLamp::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			// Init (0x406d5a), falls through
			std::int32_t id = LinkIdAt(_world, ToTile(I32(0x0c)), ToTile(I32(0x10)));
			I32(0x50) = id;
			I32(0x60) = _world->ActivationCount(id);
			I32(0x64) = 0;
			U8(0x41) = 0x1c;
		}

		std::int32_t id = I32(0x50);
		// Placed 64 px in front of its receptacle (by the receptacle's facing) and 8 px lower
		BatteryObject* owner = _world->GetObject(_world->LinkOwner(id));
		if (owner != nullptr) {
			S8(0x45) = owner->S8(0x45);
			I32(0x0c) = owner->I32(0x0c) + ((std::int32_t)S8(0x45) << 22);
			I32(0x10) = owner->I32(0x10) + 0x80000;
		}
		if (I32(0x60) != (std::int32_t)_world->ActivationCount(id)) {
			// A battery went in: the respawn point is where the player is now, and the power stays on
			I32(0x64) = 1;
			I32(0x60) = _world->ActivationCount(id);
			p->SpawnX16() = p->X16();
			p->SpawnY16() = p->Y16();
			PlaySample(I32(0x0c), I32(0x10), 0x3b, 0, 0);
		}
		if (I32(0x64) != 0) {
			_world->Trigger(id) = 0xff;
		}
		if (_world->IsDrawingTick()) {
			U16(0x34) = MakeAnimHandle((std::int32_t)AnimSet::Batterij, 1);
			U32(0x28) = FrameOf(U16(0x34), 0);
			DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), 0, 0, U32(0x28));
			if (I32(0x64) != 0) {
				U8(0x3c) = (std::uint8_t)((p->GetTickCount() >> 3) % 5 + 1);
				U32(0x28) = FrameOf(U16(0x34), U8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), 0, 0, U32(0x28));
			}
		}
	}

	void ChargingPort::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			// Init (0x403feb), falls through: facing from parameter bit 0, 22 px lower
			S8(0x45) = FacingAt(_world, ToTile(I32(0x0c)), ToTile(I32(0x10)), 0);
			U16(0x34) = MakeAnimHandle((std::int32_t)AnimSet::Poort, 0);
			U8(0x3c) = 0;
			U8(0x41) = 0x1c;
			I32(0x10) += 0x160000;
		}

		// 0x40403b: the pillars and sparks - behind the player in mode 0, in front of it in mode 9. The PRNG is
		// drawn every tick, drawn or not
		I32(0x24) += 1;
		std::int32_t x = I32(0x0c), y = I32(0x10);
		std::int8_t facing = S8(0x45);
		std::uint16_t anim = U16(0x34);
		DrawSprite(x, y, 3, facing, 0, 0, FrameOf(anim, 0));
		if ((_world->Random() & 1) != 0) {
			std::uint32_t w = _world->Random();
			DrawSprite(x, y, 3, facing, 0, 0, FrameOf(anim, 1 + (std::int32_t)(w & 1)));
		}
		DrawSprite(x, y, 3, facing, 9, 0, FrameOf(anim, 3));
		DrawSprite(x, y, 3, facing, 9, 0, FrameOf(anim, 4 + ((I32(0x24) / 16) & 3)));
		std::uint32_t u = _world->Random();
		DrawSprite(x, y, 3, facing, 9, 0, FrameOf(anim, 8 + (std::int32_t)(u & 3)));

		// Within 64 px, not in a transporter or hit: tops up from 6650, else holds the player in pose 7 (0x404190)
		if (_world->IsPlayerWithin(this, 0x1000) && p->TransporterTicks() == 0 && p->HazardSlot() == 0) {
			if (p->Energy() >= 0x19fa) {
				p->Energy() = 0x1b58;
				return;
			}
			p->VX16() = 0;
			p->VY16() = 0;
			if (p->RechargeSlot() != _slot) {
				PlaySample(x, y, 0x1e, 0, 0);
				LOGI("[BatteryObject] tick {} slot {} port grabs the player at energy {}", p->GetTickCount(), _slot, p->Energy());
			}
			p->RechargeSlot() = _slot;
			p->SetPoseFromObject(7, 0);
		}
	}

	void SoundMarker::Behave()
	{
		// 0x401ee0: sSND_O006 at volume 16 while the player is within 128 px on both axes (strict)
		BatterymanPlayer* p = _world->GetPlayer();
		std::int32_t x = I32(0x0c), y = I32(0x10);
		if (x - 0x800000 < p->X16() && p->X16() < x + 0x800000 && y - 0x800000 < p->Y16() && p->Y16() < y + 0x800000) {
			HoldSample(x, y, 0x38, 0x10, 0);
		}
	}
}
