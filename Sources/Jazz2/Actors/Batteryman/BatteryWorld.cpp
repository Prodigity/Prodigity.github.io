#include "BatteryWorld.h"
#include "BatterymanPlayer.h"
#include "BatteryPickups.h"
#include "BatteryMovers.h"
#include "BatteryBoss.h"
#include "../../ILevelHandler.h"
#include "../../ContentResolver.h"
#include "../../Tiles/TileMap.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

#include <algorithm>
#include <cstring>

using namespace Death::IO;

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		const std::int32_t SineTable[1024] = {
#include "BatterySine.inc"
		};

		const std::uint8_t RandomSeedTable[256] = {
#include "BatteryRandomSeed.inc"
		};

		const StringView SampleNames[67] = {
#include "BatterySampleNames.inc"
		};

		/** @brief `(v + (v >> 31 & 0xffff)) >> 16` - whole pixels, truncated toward zero */
		std::int32_t Pixels(std::int32_t v) {
			return (v + ((v >> 31) & 0xffff)) >> 16;
		}
	}

	BatteryWorld::BatteryWorld(ILevelHandler* levelHandler, BatterymanPlayer* player)
		: _levelHandler(levelHandler), _player(player), _eventsWidth(0), _eventsHeight(0), _liveEnd(1), _nextFree(1),
			_drawingTick(false), _lastDistance(0), _bossSlot(0), _renderedFrames(0), _spriteSequence(0), _randomIndex(0)
	{
		// FUN_00407670: 512 slots, slot 0 reserved, DAT_004b00e0 = DAT_004b00e8 = 1. The level load (FUN_004106a0)
		// zeroes the power levels and activation counters; the owners table is never cleared (starts at 0 here)
		std::memset(_pool, 0, sizeof(_pool));
		std::memset(_trigger, 0, sizeof(_trigger));
		std::memset(_activations, 0, sizeof(_activations));
		std::memset(_linkOwners, 0, sizeof(_linkOwners));
		std::memset(_waterSlots, 0, sizeof(_waterSlots));
		RegisterTypes();
		// FUN_00401860: the PRNG starts from seed 0 at every level, the particles are all freed (FUN_00416090)
		SeedRandom(0);
		_particles = std::make_unique<BatteryParticles>(this);
		SetupTileAnimations();
		UpdateTileAnimations(0);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Development aid: BATTERYCHECK_TRACE=23,26,... (hex event ids) logs those objects' record every tick
		if (const char* types = std::getenv("BATTERYCHECK_TRACE")) {
			for (const char* c = types; *c != '\0';) {
				_tracedTypes[std::strtol(c, nullptr, 16) & 0xff] = true;
				while (*c != '\0' && *c != ',') {
					c++;
				}
				if (*c == ',') {
					c++;
				}
			}
		}
#endif
	}

	std::int32_t& BatteryWorld::DropDamage()
	{
		static std::int32_t dropDamage = 2;
		return dropDamage;
	}

	void BatteryWorld::TickParticles()
	{
		_particles->Tick();
	}

	void BatteryWorld::SetupTileAnimations()
	{
		// FUN_004106a0, per animation: 4 rounds when it has a random wait (1 otherwise), each the frames, with
		// ping-pong the hold on the last frame and the frames backwards, then the wait (random part drawn here)
		// on the last entry. The random part used the keystream before the level reseeds it, so it's not
		// reproducible there; a generator of its own keeps the level's stream untouched here.
		auto* tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr) {
			return;
		}
		std::uint32_t seed = 0x1234567;
		auto random = [&seed]() {
			seed = seed * 214013u + 2531011u;
			return (std::int32_t)((seed >> 16) & 0x7fff);
		};
		auto& anims = tileMap->GetAnimatedTiles();
		_tileAnimations.resize(anims.size());
		for (std::size_t i = 0; i < anims.size(); i++) {
			Tiles::AnimatedTile& anim = anims[i];
			TileAnimation& info = _tileAnimations[i];
			// The converter keeps the speed as 16 · 50 / Speed
			float duration16 = anim.FrameDuration * 16.0f;
			info.Speed = (duration16 > 0.0f ? (std::uint32_t)(800.0f / duration16 + 0.5f) : 0);

			SmallVector<Tiles::AnimatedTileFrame, 0> frames(anim.Tiles);
			SmallVector<Tiles::AnimatedTileFrame, 0> list;
			std::int32_t rounds = (anim.DelayJitter != 0 ? 4 : 1);
			for (std::int32_t round = 0; round < rounds && !frames.empty(); round++) {
				std::int32_t extra = (((random() & 0x7fff) - 0x3fff) * anim.DelayJitter) / 16384;
				std::int32_t wait = std::max(anim.Delay + extra, 0);
				for (auto& f : frames) {
					list.push_back(f);
				}
				if (anim.IsPingPong) {
					for (std::int32_t k = 0; k < anim.PingPongDelay; k++) {
						list.push_back(list.back());
					}
					for (std::size_t k = frames.size(); k > 0; k--) {
						list.push_back(frames[k - 1]);
					}
				}
				for (std::int32_t k = 0; k < wait; k++) {
					list.push_back(list.back());
				}
			}
			info.Length = (std::uint32_t)(list.size() & 0xff);
			if (!list.empty()) {
				anim.Tiles = std::move(list);
			}
			// The engine's own timing is off; the world sets the frame
			anim.FrameDuration = 0.0f;
			anim.CurrentTileIdx = 0;
		}
		if (!_tileAnimations.empty()) {
			String lengths;
			for (std::size_t i = 0; i < _tileAnimations.size(); i++) {
				char buffer[24];
				std::snprintf(buffer, sizeof(buffer), "%s%u@%u", (i == 0 ? "" : " "), _tileAnimations[i].Length, _tileAnimations[i].Speed);
				lengths += buffer;
			}
			LOGI("[BatteryWorld] {} tile animations (entries@speed): {}", _tileAnimations.size(), lengths);
		}
	}

	void BatteryWorld::UpdateTileAnimations(std::uint32_t ms)
	{
		auto* tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr) {
			return;
		}
		auto& anims = tileMap->GetAnimatedTiles();
		for (std::size_t i = 0; i < anims.size() && i < _tileAnimations.size(); i++) {
			const TileAnimation& info = _tileAnimations[i];
			if (info.Length == 0) {
				continue;
			}
			// MUL 0x10624dd3 / SHR 6 on the 32-bit product, then the byte length
			std::uint32_t entry = ((ms * info.Speed) / 1000u) % info.Length;
			anims[i].CurrentTileIdx = (std::int32_t)std::min<std::uint32_t>(entry, (std::uint32_t)anims[i].Tiles.size() - 1);
		}
	}

	void BatteryWorld::MarkParticlesDrawn(std::int32_t camX, std::int32_t camY)
	{
		_particles->MarkDrawn(camX, camY);
	}

	bool BatteryWorld::IsPixelSolid(std::int32_t x, std::int32_t y) const
	{
		auto* tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr) {
			return true;
		}
		std::int32_t px = (x >> 16), py = (y >> 16);
		Vector2i bounds = tileMap->GetLevelBounds();
		if (px < 0 || py < 0 || px >= bounds.X || py >= bounds.Y) {
			return true;
		}
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true, WeaponType::Blaster, 0, 0.0f, 0 };
		return !tileMap->IsTileEmpty(AABBf((float)px, (float)py, (float)px, (float)py), params);
	}

	void BatteryWorld::PlayWorldSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq)
	{
		_player->PlayPositionalSample(x, y, id, volume, freq);
	}

	std::uint32_t BatteryWorld::TestPixel(std::int32_t x, std::int32_t y) const
	{
		// FUN_00407990: the pixel's coordinates compare unsigned with the level's last pixel, so left of or above
		// the level is outside too
		auto* tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr) {
			return 0xfffffffe;
		}
		Vector2i bounds = tileMap->GetLevelBounds();
		std::uint32_t px = (std::uint32_t)(x >> 16), py = (std::uint32_t)(y >> 16);
		if (px > (std::uint32_t)(bounds.X - 1) || py > (std::uint32_t)(bounds.Y - 1)) {
			return 0xfffffffe;
		}
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true, WeaponType::Blaster, 0, 0.0f, 0 };
		return (tileMap->IsTileEmpty(AABBf((float)px, (float)py, (float)px, (float)py), params) ? 0 : 1);
	}

	bool BatteryWorld::ProbeRow(std::int32_t x, std::int32_t n, std::int32_t y)
	{
		// FUN_00407a40: the tile row must be in the map (DAT_004d82f0 = rows - 1); tiles right of it are empty
		auto* tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr) {
			return false;
		}
		Vector2i bounds = tileMap->GetLevelBounds();
		std::int32_t lastColumn = bounds.X / 32 - 1, lastRow = bounds.Y / 32 - 1;
		std::int32_t ty = (y + ((y >> 31) & 0x1f)) >> 5;
		if (ty < 0 || lastRow < ty) {
			return false;
		}
		bool solid = false;
		std::int32_t rest = n;
		while (true) {
			std::int32_t bit = x & 0x1f;
			std::int32_t tx = (x + ((x >> 31) & 0x1f)) >> 5;
			bool hasTile = (0 <= tx && tx <= lastColumn && HasMainLayerTile(tx, ty));
			if (hasTile) {
				_probeEvent = GetEventType(tx, ty);
			}
			std::int32_t end = bit + rest;
			std::int32_t last;
			if (end < 0x1f) {
				last = end;
				rest = 0;
			} else {
				last = 0x1f;
				rest = end - 0x20;
			}
			if (hasTile) {
				std::int32_t tileX = x - bit;
				for (std::int32_t b = bit; b <= last && !solid; b++) {
					solid = (TestPixel((tileX + b) << 16, y << 16) == 1);
				}
			}
			if (end >= 0x1f) {
				x += 0x20 - bit;
			}
			if (solid || rest < 1) {
				return solid;
			}
		}
	}

	std::int32_t BatteryWorld::ProbeColumn(std::int32_t x, std::int32_t y, std::int32_t n)
	{
		// FUN_00407ba0: a column left or right of the map is free (and clears the latch)
		auto* tileMap = _levelHandler->TileMap();
		std::int32_t tx = (x + ((x >> 31) & 0x1f)) >> 5;
		Vector2i bounds = (tileMap != nullptr ? tileMap->GetLevelBounds() : Vector2i(0, 0));
		std::int32_t lastColumn = bounds.X / 32 - 1, lastRow = bounds.Y / 32 - 1;
		if (tx < 0 || lastColumn < tx) {
			_probeEvent = 0;
			return 0;
		}
		bool first = true, solid = false, hasTile = false;
		std::int32_t row = y;
		while (true) {
			if ((row & 0x1f) == 0 || first) {
				std::int32_t ty = (row + ((row >> 31) & 0x1f)) >> 5;
				hasTile = (0 <= ty && ty <= lastRow && HasMainLayerTile(tx, ty));
				if (hasTile) {
					first = false;
					_probeEvent = GetEventType(tx, ty);
				}
			}
			if (hasTile) {
				solid = (TestPixel(x << 16, row << 16) == 1);
			}
			row++;
			if (solid || n + y < row) {
				return row - y;
			}
		}
	}

	bool BatteryWorld::HasMainLayerTile(std::int32_t tx, std::int32_t ty) const
	{
		auto* tileMap = _levelHandler->TileMap();
		return (tileMap != nullptr && tileMap->HasSpriteLayerTile(tx, ty));
	}

	std::int32_t BatteryWorld::GetLevelMaxY16() const
	{
		auto* tileMap = _levelHandler->TileMap();
		return (tileMap != nullptr ? (tileMap->GetLevelBounds().Y - 1) * 0x10000 : 0);
	}

	std::int32_t BatteryWorld::FindWaterAt(std::int32_t x, std::int32_t y) const
	{
		// FUN_00401da0
		for (std::int32_t i = 1; i < _liveEnd; i++) {
			BatteryObject* o = _pool[i];
			if (o != nullptr && o->U8(0x44) == 0x2e && 0 < o->I32(0x6c) && o->I32(0x0c) < x && x < o->I32(0x54) * 0x10000 + o->I32(0x0c) &&
				o->I32(0x10) < y && y < o->I32(0x68) * 0x10000 + o->I32(0x10)) {
				return i;
			}
		}
		return 0;
	}

	void BatteryWorld::SnapToGround(BatteryObject* obj)
	{
		// FUN_004083b0: 15 px up, then down 1 px at a time until the pixel under the frame's middle (at its
		// coldspot height) is solid or outside, or the level ends
		obj->I32(0x10) -= 0xf0000;
		std::uint32_t frame = BatteryObject::FrameOf(obj->U16(0x34), 0);
		obj->U32(0x28) = frame;
		const FrameInfo* f = BatteryAnims::Get().GetFrame(frame);
		std::int32_t w = (f != nullptr ? (std::uint16_t)f->Width : 0);
		std::int32_t hotX = (f != nullptr ? f->HotspotX : 0), hotY = (f != nullptr ? f->HotspotY : 0), coldY = (f != nullptr ? f->ColdspotY : 0);
		std::int32_t maxY = GetLevelMaxY16();
		while (obj->I32(0x10) < maxY) {
			if (TestPixel((hotX + (w >> 1)) * 0x10000 + obj->I32(0x0c), (hotY - coldY) * 0x10000 + obj->I32(0x10)) != 0) {
				return;
			}
			obj->I32(0x10) += 0x10000;
		}
	}

	void BatteryWorld::SolidBox(BatteryObject* obj, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, std::int32_t type)
	{
		// FUN_0040b800
		BatterymanPlayer* p = _player;
		std::int32_t slot = obj->GetSlot();
		if (p->TransporterTicks() != 0 || p->GrabbedBySlot() != 0) {
			return;
		}
		std::int32_t& pX = p->X16();
		std::int32_t& pY = p->Y16();
		std::int32_t& vx = p->VX16();
		std::int32_t& vy = p->VY16();
		std::int32_t right = (w + 0x14) * 0x10000 + x;
		std::int32_t bottom = (h + 0x10) * 0x10000 + y;

		// Head bump from below
		if (bottom < pY && pY < bottom + 0x200000 && x - 0x140000 < pX && pX < right && vy < 0) {
			vy = 0;
			p->HeadBumpSlot() = slot;
		} else if (p->HeadBumpSlot() == slot) {
			p->HeadBumpSlot() = 0;
		}

		std::int32_t result = 0;
		// Side push: a box with type 1 is a wall to the player at its sides
		if (type == 1 && y - 0x2c0000 < pY && pY < bottom) {
			bool fromRight = (vx < 0);
			if (!fromRight) {
				if (!(x - 0x140000 < pX) && !(pX + vx < x - 0x148000)) {
					pX = x - 0x148000;
					if (p->Ax16() > 0) {
						p->Ax16() = 0;
					}
					vx = 0;
					result = 1;
				} else if (vx <= 0) {
					fromRight = true;
				}
			}
			if (fromRight && result == 0) {
				if (!(pX < right) && !(right + 0x8000 < pX + vx)) {
					pX = right + 0x8000;
					if (p->Ax16() < 0) {
						p->Ax16() = 0;
					}
					vx = 0;
					result = 1;
				}
			}
		}

		if (result == 0) {
			std::uint8_t ev = obj->U8(0x44);
			if (ev == 0x2b || ev == 0x2c) {
				// A stamper on its way down crushes whoever is under it
				if (0 < obj->I32(0x18) && x - 0x40000 < pX && pX < right - 0x100000 && y - 0xc0000 < pY && pY < bottom + 0x400000) {
					result = 1;
					if (slot != 0) {
						PlayWorldSample(0, 0, 0x1a, 0, 0);
					}
					p->GrabbedBySlot() = slot;
					LOGI("[BatteryWorld] tick {} slot {} stamper crushes the player", p->GetTickCount(), slot);
				} else if (y - 0x300000 <= pY && x - 0x138000 < pX && pX < right - 0x8000 && pY < y + 0x120000) {
					result = 2;
				}
			} else if (y - 0x300000 <= pY && x - 0x138000 < pX && pX < right - 0x8000 && pY < bottom && -1 < vy) {
				result = 2;
			}
		}

		if (result == 0) {
			if (p->RideSlot() == slot) {
				// Walking off a running belt launches the player
				if (obj->U8(0x44) == 0x2f && p->ExtVX() != 0) {
					p->Boost() = 0x23;
					vx += p->ExtVX() / 2;
				}
				p->RideSlot() = 0;
				p->RideType() = 0;
				p->ExtVX() = 0;
				p->ExtVY() = 0;
				p->PushSlot() = 0;
			}
			if (p->PushSlot() == slot) {
				p->PushSlot() = 0;
			}
		} else if (result == 1) {
			p->PushSlot() = slot;
			p->RideSlot() = 0;
			p->RideType() = 0;
			p->ExtVX() = 0;
			p->ExtVY() = 0;
			vx = 0;
			// (+0x48 = max(+0x48, 0x3c): never read)
			if (p->GrabbedBySlot() == 0) {
				p->SetPoseFromObject(0, 0x12);
			}
		} else {
			p->RideOffY() = (y - 0x2c0000) - obj->I32(0x10);
			if (p->RideSlot() != slot) {
				p->RideOffX() = pX - obj->I32(0x0c);
			}
			p->RideSlot() = slot;
			p->RideType() = type;
			p->ExtVX() = 0;
			p->ExtVY() = 0;
			p->PushSlot() = 0;
		}
	}

	void BatteryWorld::TopPlatform(BatteryObject* obj, std::int32_t w, std::int32_t h, std::int32_t type)
	{
		// FUN_0040b6b0
		BatterymanPlayer* p = _player;
		if (p->TransporterTicks() != 0) {
			return;
		}
		std::int32_t slot = obj->GetSlot();
		auto pixels = [](std::int32_t v) { return (v + ((v >> 31) & 0xffff)) >> 16; };
		std::int32_t dx = pixels(p->X16() - obj->I32(0x0c));
		std::int32_t dy = pixels(p->Y16() - obj->I32(0x10));
		std::int32_t half = w / 2;
		if (p->RideSlot() == slot) {
			if (-half < dx && dx < half && -4 - h < dy && dy < 0) {
				p->RideOffY() = h * -0x10000;
				p->ExtVX() = 0;
				p->ExtVY() = 0;
				return;
			}
			p->RideSlot() = 0;
			p->RideType() = 0;
			p->ExtVX() = 0;
			p->ExtVY() = 0;
			return;
		}
		if (-half < dx && dx < half && -4 - h < dy && dy < 4 - h && obj->I32(0x18) - 0x10000 < p->VY16()) {
			p->RideOffY() = h * -0x10000;
			p->RideOffX() = p->X16() - obj->I32(0x0c);
			p->RideType() = type;
			p->RideSlot() = slot;
			p->ExtVX() = 0;
			p->ExtVY() = 0;
		}
	}

	StringView BatteryWorld::GetSampleName(std::int32_t id)
	{
		return (id >= 0 && id < (std::int32_t)arraySize(SampleNames) ? SampleNames[id] : StringView{});
	}

	void BatteryWorld::ComputeMix(std::int32_t x, std::int32_t y, std::int32_t volume, std::int32_t& outVolume, std::int32_t& outPan) const
	{
		// FUN_00419100, one channel; this game has one player
		std::int32_t vol = (volume == 0 ? 0x3f : volume);
		std::int32_t v, pan;
		if (x == 0) {
			v = vol;
			pan = 0x20;
		} else {
			auto units = [](std::int32_t d) { return (d + ((d >> 31) & 0x7ffff)) >> 19; };	// 8 px, toward zero
			std::int32_t dx = units(x - _player->X16());
			std::int32_t dy = units(y - _player->Y16());
			std::int32_t d2 = dx * dx + dy * dy;
			std::int32_t p = (d2 < 0x11 ? vol : vol - (d2 + ((d2 >> 31) & 0xf)) / 16);
			v = std::max(0, p);
			pan = 0;
			if (p > 0) {
				std::int32_t t = v * dx;
				pan = std::clamp((t + ((t >> 31) & 0x7f)) >> 7, -0x1f, 0x1f);
			}
			pan = (pan < -0x20 ? 0 : (pan < 0x21 ? pan + 0x20 : 0x40));
		}
		outVolume = std::clamp(v, 0, 0x3f);
		outPan = pan;
	}

	std::int32_t BatteryWorld::Sine(std::int32_t index)
	{
		return SineTable[index & 0x3ff];
	}

	namespace
	{
		/** @brief Depth of tile layer L, as JJ2Level's converter assigns it (500 - adjusted (L - 3) * 100) */
		std::int32_t TileLayerDepth(std::int32_t layer)
		{
			std::int32_t d = (layer - 3) * 100;
			if (d < -200) {
				d = -200 - (200 - d) / 20;
			} else if (d > 300) {
				d = 300 + (d - 300) / 20;
			}
			return ILevelHandler::MainPlaneZ - d;
		}
	}

	std::uint16_t BatteryWorld::GetSpriteRenderLayer(std::int32_t layer, std::int32_t mode)
	{
		// Passes above a tile layer: +1 water tint / mode 10, +2 normal sprites, +3 the player, +4 mode 9, +5 particles,
		// +6 the water tint's second pass. Modes 4, 9 and 10 are drawn in the layer-3 flush whatever their list layer
		// (FUN_0040e3f0)
		if (mode == 9) {
			return (std::uint16_t)(TileLayerDepth(3) + 4);
		}
		if (mode == 4 || mode == 10) {
			return (std::uint16_t)(TileLayerDepth(3) + 1);
		}
		return (std::uint16_t)(TileLayerDepth(std::clamp(layer, 0, 7)) + 2);
	}

	std::uint16_t BatteryWorld::GetTintRenderLayer(std::int32_t pass)
	{
		return (std::uint16_t)(TileLayerDepth(3) + (pass == 0 ? 1 : 6));
	}

	std::uint8_t& BatteryWorld::WaterSurfaceRemap()
	{
		static std::uint8_t value = 0;	// DAT_004dac4c, 0 in the image
		return value;
	}

	std::uint8_t& BatteryWorld::WaterBodyRemap()
	{
		static std::uint8_t value = 0;	// DAT_004c5420, 0 in the image
		return value;
	}

	std::uint16_t BatteryWorld::GetPlayerRenderLayer()
	{
		return (std::uint16_t)(TileLayerDepth(3) + 3);
	}

	std::uint16_t BatteryWorld::GetParticleRenderLayer()
	{
		return (std::uint16_t)(TileLayerDepth(3) + 5);
	}

	void BatteryWorld::SeedRandom(std::uint32_t seed)
	{
		// FUN_00423bb0: buffer[i] = table[(seed + i) & 0xff] for i = 0..0x103, index = seed & 0xff
		for (std::uint32_t i = 0; i < sizeof(_random); i++) {
			_random[i] = RandomSeedTable[(seed + i) & 0xff];
		}
		_randomIndex = (seed & 0xff);
	}

	std::uint32_t BatteryWorld::Random()
	{
		// v = u32 at (index - 0x2c) & 0xff; index = (index + 4) & 0xff; u32 at index ^= v; return v
		std::uint32_t c = (_randomIndex & 0xff);
		std::uint32_t from = ((c - 0x2c) & 0xff);
		std::uint32_t v;
		std::memcpy(&v, &_random[from], sizeof(v));
		std::uint32_t to = ((c + 4) & 0xff);
		std::uint32_t w;
		std::memcpy(&w, &_random[to], sizeof(w));
		w ^= v;
		std::memcpy(&_random[to], &w, sizeof(w));
		_randomIndex = to;
		return v;
	}

	std::int32_t BatteryWorld::FindNearest(std::int32_t x, std::int32_t y, std::int32_t radiusSquared, std::int32_t type, std::int32_t state) const
	{
		// FUN_00408c60
		std::int32_t best = radiusSquared, found = -1;
		for (std::int32_t i = 1; i < _liveEnd; i++) {
			BatteryObject* obj = _pool[i];
			if (obj == nullptr || obj->_record[0x44] != (std::uint8_t)type || (state >= 0 && obj->_record[0x41] != (std::uint8_t)state)) {
				continue;
			}
			std::int32_t dy = Pixels(y - obj->I32(0x10));
			std::int32_t dx = Pixels(x - obj->I32(0x0c));
			std::int32_t d = dx * dx + dy * dy;
			if (d < best) {
				best = d;
				found = i;
			}
		}
		return found;
	}

	bool BatteryWorld::IsPlayerWithin(BatteryObject* obj, std::int32_t radiusSquared)
	{
		// FUN_00408da0: x first, then y only if x alone is already inside
		_lastDistance = radiusSquared;
		std::int32_t dx = Pixels(_player->X16() - obj->I32(0x0c));
		std::int32_t d = dx * dx;
		if (d < radiusSquared) {
			std::int32_t dy = Pixels(_player->Y16() - obj->I32(0x10));
			d += dy * dy;
			if (d < radiusSquared) {
				_lastDistance = d;
				return true;
			}
		}
		return false;
	}

	BatteryWorld::~BatteryWorld()
	{
		for (std::int32_t i = 1; i < PoolSize; i++) {
			if (_pool[i] != nullptr) {
				_pool[i]->_world = nullptr;
				_pool[i]->SetState(ActorState::IsDestroyed, true);
				_pool[i] = nullptr;
			}
		}
	}

	void BatteryWorld::RegisterTypes()
	{
		// FUN_00407170: the template rows written into the descriptor table (0x4d83a0 + type * 0x80). Only these
		// types spawn; every other event id in 0x20..0x4f is inert. Columns: +0x34 animation (pickups only, set 16
		// base + n), +0x36 set, +0x3d hit flags, +0x40 class
		std::memset(_templates, 0, sizeof(_templates));
		auto reg = [this](std::int32_t type, std::uint16_t set, std::uint8_t hitFlags, std::uint32_t cls, std::int32_t pickupAnim = -1) {
			Template& t = _templates[type];
			t.Registered = true;
			t.AnimSet = set;
			t.HitFlags = hitFlags;
			t.Class = cls;
			// +0x34 is 0 in every row but the pickups': global animation 0, the first animation of the first set the
			// preload loads, FONT (0x4076ae)
			t.AnimHandle = (pickupAnim >= 0 ? MakeAnimHandle((std::int32_t)AnimSet::Pickups, pickupAnim)
				: MakeAnimHandle((std::int32_t)AnimSet::Font, 0));
		};
		reg(0x46, 0x17, 1, 8);		// LAB_00401e10
		reg(0x01, 0x00, 1, 8);		// LAB_00401ee0 (event 1 with parameter bit 0x1000)
		reg(0x3a, 0x0f, 1, 8);		// LAB_00402db0
		reg(0x37, 0x01, 1, 8);		// LAB_00403e40
		reg(0x42, 0x00, 1, 8);		// LAB_00403090 (red button)
		reg(0x38, 0x00, 1, 8);		// LAB_00403580 (boss body)
		reg(0x44, 0x00, 1, 2);		// LAB_00403310 (boss projectile)
		reg(0x2a, 0x13, 1, 8);		// LAB_00403fd0 (charging port)
		reg(0x2e, 0x18, 1, 8);		// LAB_00404c60
		reg(0x41, 0x18, 0, 8);		// LAB_00404f50
		reg(0x3b, 0x18, 1, 8);		// LAB_00404fc0
		reg(0x29, 0x18, 1, 8);		// LAB_00405100
		reg(0x25, 0x18, 1, 8);		// LAB_00405100
		reg(0x23, 0x07, 1, 8);		// LAB_00404fd0
		reg(0x3d, 0x03, 1, 8);		// LAB_00406d40
		reg(0x35, 0x0a, 1, 8);		// LAB_004056e0 (Freddy)
		reg(0x43, 0x0a, 1, 8);		// LAB_004055d0 (Freddy's empty battery)
		reg(0x33, 0x16, 0, 8);		// LAB_00401f50
		reg(0x34, 0x04, 0, 8);		// LAB_00404250
		reg(0x36, 0x08, 1, 8);		// LAB_00405890
		reg(0x32, 0x08, 1, 8);		// LAB_004059f0
		reg(0x30, 0x19, 1, 8);		// LAB_00405310
		reg(0x26, 0x0c, 1, 8);		// LAB_00406530
		reg(0x28, 0x11, 1, 8);		// LAB_004061c0
		reg(0x2b, 0x15, 1, 8);		// LAB_00405db0
		reg(0x2c, 0x1a, 1, 8);		// LAB_004029f0
		reg(0x21, 0x03, 1, 8);		// LAB_00406890
		reg(0x3c, 0x10, 0, 5, 1);	// FUN_00406f10 (life)
		reg(0x3e, 0x10, 0, 5, 0);	// FUN_00406f10 (battery)
		reg(0x3f, 0x10, 0, 5, 2);	// FUN_00406f10 (superbattery)
		reg(0x45, 0x10, 0, 5, 0);	// FUN_00406f10
		reg(0x2f, 0x02, 1, 8);		// LAB_00405aa0
	}

	bool BatteryWorld::LoadEvents(StringView levelName)
	{
		String path = ContentResolver::Get().FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(levelName) + ".j2l.bcev"_s)));
		auto s = (path.empty() ? nullptr : fs::Open(path, FileAccess::Read));
		if (s == nullptr || !s->IsValid() || s->ReadValueAsLE<std::uint32_t>() != 0x56454342) {	// "BCEV"
			LOGW("Level \"{}\" has no original event layer, no objects are spawned", levelName);
			return false;
		}
		_eventsWidth = (std::int32_t)s->ReadValueAsLE<std::uint32_t>();
		_eventsHeight = (std::int32_t)s->ReadValueAsLE<std::uint32_t>();
		_events = std::make_unique<std::uint32_t[]>((std::size_t)_eventsWidth * _eventsHeight);
		for (std::int32_t i = 0; i < _eventsWidth * _eventsHeight; i++) {
			_events[i] = s->ReadValueAsLE<std::uint32_t>();
		}
		LOGI("[BatteryWorld] {}x{} event grid loaded", _eventsWidth, _eventsHeight);
		return true;
	}

	bool BatteryWorld::IsInEventGrid(std::int32_t tx, std::int32_t ty) const
	{
		// The readers compare (unsigned) against DAT_004d82ec / DAT_004d82f0, which the preload sets to width - 1 and
		// height - 1 (0x4076ee / 0x4076f7) - so the last column and the last row of the grid are never read
		return ((std::uint32_t)tx < (std::uint32_t)(_eventsWidth - 1) && (std::uint32_t)ty < (std::uint32_t)(_eventsHeight - 1));
	}

	std::uint32_t BatteryWorld::GetEvent(std::int32_t tx, std::int32_t ty) const
	{
		return (IsInEventGrid(tx, ty) ? _events[tx + ty * _eventsWidth] : 0);
	}

	void BatteryWorld::SetEvent(std::int32_t tx, std::int32_t ty, std::uint32_t value)
	{
		if (IsInEventGrid(tx, ty)) {
			_events[tx + ty * _eventsWidth] = value;
		}
	}

	std::int32_t BatteryWorld::GetEventParam(std::int32_t tx, std::int32_t ty, std::int32_t offset, std::int32_t bits) const
	{
		// FUN_00408950: the field starts at bit 12 + offset; a negative width is a signed field of -bits bits
		if (!IsInEventGrid(tx, ty)) {
			return 0;
		}
		std::uint32_t v = _events[tx + ty * _eventsWidth];
		if (bits >= 0) {
			return (std::int32_t)((v >> ((offset + 12) & 31)) & ((1u << (bits & 31)) - 1u));
		}
		std::uint32_t range = (1u << ((-bits) & 31));
		std::uint32_t field = (v >> ((offset + 12) & 31)) & (range - 1u);
		if (((range >> 1) & field) == (range >> 1)) {
			return (std::int32_t)(field - range);
		}
		return (std::int32_t)field;
	}

	void BatteryWorld::SpawnAll()
	{
		// FUN_00408ad0 → FUN_00408a30 per tile, column after column (`< DAT_004d82ec`, so never the last column or
		// row): event 1 spawns only with parameter bit 0x1000, otherwise a registered type of 0x20 or more that is not
		// marked spawned (0x800); the object is centred 15 px into its tile and the tile is marked spawned
		for (std::int32_t tx = 0; tx < _eventsWidth - 1; tx++) {
			for (std::int32_t ty = 0; ty < _eventsHeight - 1; ty++) {
				std::uint32_t ev = GetEvent(tx, ty);
				std::uint32_t type = (ev & 0xff);
				bool spawn;
				if (type == 1) {
					spawn = ((ev & 0x1000) != 0);
				} else {
					if (type < 0x20 || (ev & 0x800) != 0) {
						continue;
					}
					spawn = (type < 0x50 && _templates[type].Registered);
				}
				if (spawn) {
					Spawn(tx * 0x200000 + 0xf0000, ty * 0x200000 + 0xf0000, (std::int32_t)type, 0x4000);
					SetEvent(tx, ty, ev | 0x800);
				}
			}
		}
		std::int32_t count = 0;
		for (std::int32_t i = 1; i < PoolSize; i++) {
			if (_pool[i] != nullptr) {
				count++;
			}
		}
		LOGI("[BatteryWorld] {} objects spawned, live end {}", count, _liveEnd);
	}

	std::int32_t BatteryWorld::Spawn(std::int32_t x, std::int32_t y, std::int32_t type, std::int32_t flags)
	{
		// FUN_004096e0: the first free slot from the hint DAT_004b00e8, else from slot 1; 0 when the pool is full
		std::int32_t slot = PoolSize;
		if (_nextFree < PoolSize) {
			slot = _nextFree;
			if (_pool[slot] == nullptr) {
				_nextFree = slot + 1;
			} else {
				for (; slot < PoolSize; slot++) {
					if (_pool[slot] == nullptr) {
						_nextFree = slot + 1;
						break;
					}
				}
			}
		}
		if (slot >= PoolSize) {
			for (slot = 1; slot < PoolSize; slot++) {
				if (_pool[slot] == nullptr) {
					_nextFree = slot + 1;
					break;
				}
			}
			if (slot >= PoolSize) {
				return 0;
			}
		}
		if (type < 0 || type >= 0x50 || !_templates[type].Registered) {
			// The original copies an empty template, which leaves the slot free again (+0x00 == 0)
			return slot;
		}

		std::shared_ptr<BatteryObject> obj = CreateObject(type);
		const Template& t = _templates[type];
		std::memset(obj->_record, 0, sizeof(obj->_record));
		obj->U16(0x34) = t.AnimHandle;
		// The preload's last loop (0x407830): every row's +0x28 is the first frame of its +0x34 animation
		obj->U32(0x28) = MakeFrameHandle(t.AnimHandle >> 8, t.AnimHandle & 0xff, 0);
		obj->U16(0x36) = t.AnimSet;
		obj->U8(0x3d) = t.HitFlags;
		obj->U32(0x40) = t.Class;
		obj->I32(0x0c) = obj->I32(0x04) = x;
		obj->I32(0x10) = obj->I32(0x08) = y;
		obj->I32(0x30) = flags;
		obj->U8(0x44) = (std::uint8_t)type;		// written back by the preload's end loop (BATTERY-OBJECTS §1)
		AttachObject(std::move(obj), slot);
		return slot;
	}

	void BatteryWorld::AttachObject(std::shared_ptr<BatteryObject> obj, std::int32_t slot)
	{
		obj->_world = this;
		obj->_slot = slot;
		_pool[slot] = obj.get();
		if (_liveEnd <= slot) {
			_liveEnd = slot + 1;
		}

		ActorActivationDetails details(_levelHandler, Vector3i(obj->I32(0x0c) >> 16, obj->I32(0x10) >> 16, 0));
		details.Type = EventType::BatterymanObject;
		obj->OnActivated(details);
		_levelHandler->AddActor(obj);
	}

	void BatteryWorld::Save(Death::IO::Stream& dest) const
	{
		// FUN_0041b240: the object count, then per slot 1..n-1 either 0 (free) or the behaviour and the record;
		// then the link tables 0x4b11e0, 0x4d7dc0, 0x4d7ee0 and 0x4d7d20
		dest.WriteValueAsLE<std::int32_t>(_liveEnd);
		dest.WriteValueAsLE<std::int32_t>(_nextFree);
		for (std::int32_t i = 1; i < _liveEnd; i++) {
			BatteryObject* obj = _pool[i];
			if (obj == nullptr) {
				dest.WriteValue<std::uint8_t>(0);
				continue;
			}
			dest.WriteValue<std::uint8_t>(1);
			dest.Write(obj->_record, sizeof(obj->_record));
		}
		dest.Write(_trigger, sizeof(_trigger));
		dest.Write(_activations, sizeof(_activations));
		for (std::int32_t i = 0; i < 256; i++) {
			dest.WriteValueAsLE<std::int32_t>(_linkOwners[i]);
		}
		for (std::int32_t i = 0; i < 64; i++) {
			dest.WriteValueAsLE<std::uint16_t>(_waterSlots[i]);
		}
	}

	bool BatteryWorld::Restore(Death::IO::Stream& src)
	{
		// FUN_0041b450(0x100): the objects come back with their records (the behaviour is the event type's), and no
		// event of the level is spawned
		std::int32_t liveEnd = src.ReadValueAsLE<std::int32_t>();
		std::int32_t nextFree = src.ReadValueAsLE<std::int32_t>();
		if (liveEnd < 1 || liveEnd > PoolSize) {
			return false;
		}
		std::int32_t count = 0;
		for (std::int32_t i = 1; i < liveEnd; i++) {
			if (src.ReadValue<std::uint8_t>() == 0) {
				continue;
			}
			std::uint8_t record[BatteryObject::RecordSize];
			src.Read(record, sizeof(record));
			std::shared_ptr<BatteryObject> obj = CreateObject(record[0x44]);
			std::memcpy(obj->_record, record, sizeof(record));
			AttachObject(std::move(obj), i);
			count++;
		}
		_liveEnd = liveEnd;
		_nextFree = nextFree;
		src.Read(_trigger, sizeof(_trigger));
		src.Read(_activations, sizeof(_activations));
		for (std::int32_t i = 0; i < 256; i++) {
			_linkOwners[i] = src.ReadValueAsLE<std::int32_t>();
		}
		for (std::int32_t i = 0; i < 64; i++) {
			_waterSlots[i] = src.ReadValueAsLE<std::uint16_t>();
		}
		LOGI("[BatteryWorld] {} objects restored, live end {}", count, _liveEnd);
		return true;
	}

	void BatteryWorld::Destroy(std::int32_t slot)
	{
		// FUN_004097c0: the slot is free once +0x00 is 0; the free-slot hint moves down to it
		if (slot > 0 && slot < _nextFree) {
			_nextFree = slot;
		}
		if (slot > 0 && slot < PoolSize && _pool[slot] != nullptr) {
			_pool[slot]->_world = nullptr;
			_pool[slot]->SetState(ActorState::IsDestroyed, true);
			_pool[slot] = nullptr;
		}
	}

	std::shared_ptr<BatteryObject> BatteryWorld::CreateObject(std::int32_t type)
	{
		// The behaviour routines are ported one family at a time; a type without its port yet still takes its
		// slot (so the pool order is the original's) but does nothing
		switch (type) {
			case 0x3c: case 0x3e: case 0x3f: case 0x45: return std::make_shared<Pickup>();
			case 0x21: return std::make_shared<Receptacle>();
			case 0x3d: return std::make_shared<PlasmaLamp>();
			case 0x2a: return std::make_shared<ChargingPort>();
			case 0x01: return std::make_shared<SoundMarker>();
			// teardown/42
			case 0x23: return std::make_shared<FloatRaft>();
			case 0x25: case 0x29: return std::make_shared<Pump>();
			case 0x26: return std::make_shared<Lift>();
			case 0x28: return std::make_shared<SwayPlatform>();
			case 0x2b: return std::make_shared<Stamper>(false);
			case 0x2c: return std::make_shared<Stamper>(true);
			case 0x2e: return std::make_shared<WaterVolume>();
			case 0x2f: return std::make_shared<ConveyorBelt>();
			case 0x30: return std::make_shared<SuctionTube>();
			case 0x32: return std::make_shared<DripEmitter>();
			case 0x33: return std::make_shared<Sucker>(false);
			case 0x34: return std::make_shared<Sucker>(true);
			case 0x36: return std::make_shared<RainGenerator>();
			case 0x37: return std::make_shared<BossEye>();
			case 0x3a: return std::make_shared<Umbrella>();
			case 0x3b: return std::make_shared<InertMarker>();
			case 0x41: return std::make_shared<WaterColour>();
			case 0x46: return std::make_shared<SparkEmitter>();
			// teardown/43
			case 0x35: return std::make_shared<Freddy>();
			case 0x43: return std::make_shared<EmptyBatteryIcon>();
			case 0x38: return std::make_shared<BossBody>();
			case 0x42: return std::make_shared<RedButton>();
			case 0x44: return std::make_shared<BossProjectile>();
			default: return std::make_shared<BatteryObject>();
		}
	}

	void BatteryWorld::TickObjects(bool lastTick)
	{
		// FUN_00409650: first the live end is recomputed from the highest used slot, then every used slot gets the
		// common step and its behaviour, in slot order
		_drawingTick = lastTick;
		if (lastTick) {
			// FUN_0040e3d0: the draw list starts empty on the drawing tick
			_spriteSequence = 0;
		}
		std::int32_t highest = 0;
		for (std::int32_t i = 1; i < PoolSize; i++) {
			if (_pool[i] != nullptr) {
				highest = i;
			}
		}
		_liveEnd = highest + 1;

		for (std::int32_t i = 1; i < _liveEnd; i++) {
			BatteryObject* obj = _pool[i];
			if (obj == nullptr) {
				continue;
			}
			if (lastTick) {
				obj->BeginSprites();
			}
			PreUpdate(obj);
			// The step may have freed the slot
			if (_pool[i] == obj) {
				obj->Behave();
			}
			if (_tracedTypes[obj->_record[0x44]] && _pool[i] == obj && _traceLines < 20000) {
				_traceLines++;
				LOGI("[Trace] t {} slot {} ev {:x} x {} y {} vx {} vy {} | {} {} {} {} {} {} {} {} {} {} {} {} | p {} {}", _player->GetTickCount(), i,
					obj->_record[0x44], obj->I32(0x0c), obj->I32(0x10), obj->I32(0x14), obj->I32(0x18),
					obj->I32(0x48), obj->I32(0x4c), obj->I32(0x50), obj->I32(0x54), obj->I32(0x58), obj->I32(0x5c),
					obj->I32(0x60), obj->I32(0x64), obj->I32(0x68), obj->I32(0x6c), obj->I32(0x70), obj->I32(0x74), _player->X16(), _player->Y16());
			}
		}

		if (lastTick && _player->GetTickCount() >= _nextTraceTick) {
			_nextTraceTick = _player->GetTickCount() + 350;
			LOGI("[BatteryWorld] tick {}: {} sprites queued by objects, live end {}", _player->GetTickCount(), _spriteSequence, _liveEnd);
		}

		// The mixer's pass (FUN_00419100, step 10 of the tick): sustained samples not requested this tick stop
		for (std::int32_t i = 1; i < PoolSize; i++) {
			if (_pool[i] != nullptr) {
				_pool[i]->ReleaseHeldSample();
			}
		}
	}

	void BatteryWorld::PreUpdate(BatteryObject* obj)
	{
		// FUN_004095a0
		if (obj->GetClass() == 1) {
			// A projectile in flight (state not 0 or 10) hits other objects: FUN_00407870, ported with the projectiles
			return;
		}

		TouchPlayer(obj);

		// Standing on tile event 10 (a conveyor) moves the object by the event's signed 8-bit parameter, px/tick
		std::int32_t tx = obj->I32(0x0c) / 0x200000;
		std::int32_t ty = obj->I32(0x10) / 0x200000;
		if (GetEventType(tx, ty + 1) == 10) {
			obj->I32(0x0c) += GetEventParam(tx, ty, 0, -8) * 0x10000;
		}
	}

	void BatteryWorld::TouchPlayer(BatteryObject* obj)
	{
		// FUN_0040be70: FUN_00407cc0(player, object) > 7 → FUN_0040bd00(object, 0)
		auto pixels = [](std::int32_t v) { return (v + ((v >> 31) & 0xffff)) >> 16; };
		std::int32_t n = BatteryAnims::Get().CountMaskOverlap(
			_player->GetFrameHandle(), pixels(_player->X16()), pixels(_player->Y16()), _player->GetFacing(),
			obj->U32(0x28), pixels(obj->I32(0x0c)), pixels(obj->I32(0x10)), obj->S8(0x45));
		if (n > 7) {
			obj->Touch(0);
		}
	}
}
