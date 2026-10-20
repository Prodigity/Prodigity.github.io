#include "BatteryBoss.h"
#include "BatteryWorld.h"
#include "BatterymanPlayer.h"

#include <algorithm>

// Ports of teardown/43 §2-§6 (Freddy 0x4056e0, the empty battery 0x4055d0, the boss body 0x403580, the red
// button 0x403090, the projectile 0x403310). "+0xNN" is the object record, P the player, T the tick counter.

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		/** @brief Signed division by 2^n truncating toward zero */
		std::int32_t DivPow2(std::int32_t v, std::int32_t n) {
			return (v + ((v >> 31) & ((1 << n) - 1))) >> n;
		}

		std::uint16_t Baas(std::int32_t anim) {
			return MakeAnimHandle((std::int32_t)AnimSet::Baas, anim);
		}

		std::uint16_t FreddyAnim(std::int32_t anim) {
			return MakeAnimHandle((std::int32_t)AnimSet::Freddy, anim);
		}
	}

	// ---- 0x35 FREDDY -----------------------------------------------------------------------------------------

	void Freddy::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		switch (U8(0x41)) {
			case 0x00:
				// 20 px up from the spawn point, not drawn this tick
				U8(0x41) = 0x1c;
				I32(0x10) = I32(0x08) - 0x140000;
				return;
			case 0x1c: {
				// Idle (8 ticks per frame on the global clock) until the player is within 64 px with empties
				U16(0x34) = FreddyAnim(0);
				if ((t & 7) == 0) {
					S8(0x3c)++;
				}
				if (FrameCountOf(U16(0x34)) <= S8(0x3c)) {
					U8(0x3c) = 0;
				}
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
				if (0 < p->Empties() && _world->IsPlayerWithin(this, 0x1000)) {
					U8(0x41) = 0x10;
					I32(0x50) = 0;
					LOGI("[Freddy] tick {} slot {} starts eating, empties {}", t, _slot, p->Empties());
				}
				return;
			}
			case 0x10: {
				// One empty every 32 ticks; every third of this visit comes back as a battery, at the player
				if ((U8(0x24) & 0x1f) == 0) {
					if (0 < p->Empties()) {
						p->Empties()--;
						I32(0x50)++;
					}
					if (p->Empties() <= 0) {
						p->Empties() = 0;
						U8(0x41) = 0x1c;
					}
					_world->Spawn(p->X16(), p->Y16(), 0x43, _slot);
					if (I32(0x50) == 3) {
						I32(0x50) = 0;
						_world->Spawn(p->X16(), p->Y16(), 0x3e, 0);
						LOGI("[Freddy] tick {} slot {} gives a battery", t, _slot);
					}
				}
				std::int32_t k = ++I32(0x24);
				U16(0x34) = FreddyAnim(1);
				if ((k & 7) == 0) {
					S8(0x3c)++;
				}
				if (FrameCountOf(U16(0x34)) <= S8(0x3c)) {
					U8(0x3c) = 0;
				}
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
				return;
			}
			default:
				U8(0x41) = 0x1c;
				return;
		}
	}

	// ---- 0x43 the eaten empty --------------------------------------------------------------------------------

	void EmptyBatteryIcon::Behave()
	{
		if (U8(0x41) == 0) {
			// Aimed once at Freddy (+0x30) + 80 px, 64 steps
			U8(0x41) = 8;
			BatteryObject* target = _world->GetObject(I32(0x30));
			std::int32_t tx = (target != nullptr ? target->I32(0x0c) : 0), ty = (target != nullptr ? target->I32(0x10) : 0);
			I32(0x54) = DivPow2(tx - I32(0x0c) + 0x500000, 6);
			I32(0x58) = DivPow2(ty - I32(0x10), 6);
		}
		if (U8(0x41) != 8) {
			return;
		}
		std::int32_t k = ++I32(0x24);
		I32(0x0c) += I32(0x54);
		I32(0x10) += I32(0x58);
		U16(0x34) = MakeAnimHandle((std::int32_t)AnimSet::Bord, 8);
		std::int32_t fc = FrameCountOf(U16(0x34));
		U8(0x3c) = (std::uint8_t)(fc > 0 ? DivPow2(k, 2) % fc : 0);
		U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
		// (The original destroys it first and still draws it on its last tick; a destroyed actor is not drawn here)
		DrawSprite(I32(0x0c), I32(0x10), 2, S8(0x45), 9, 0, U32(0x28));
		if (0x3f < k) {
			PlaySample(I32(0x0c), I32(0x10), 0xc, 0x40, 0);
			_world->Destroy(_slot);
		}
	}

	// ---- 0x38 BAAS -------------------------------------------------------------------------------------------

	void BossBody::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();

		// n: how many of the receptacle channels 0..3 have had a battery
		I32(0x70) = 0;
		for (std::int32_t ch = 0; ch < 4; ch++) {
			if (_world->ActivationCount(ch) != 0) {
				I32(0x70)++;
			}
		}
		std::int32_t n = I32(0x70);
		std::int32_t x = I32(0x0c), y = I32(0x10);
		auto frameCount = [&]() { return FrameCountOf(U16(0x34)); };
		auto drawCurrent = [&]() {
			U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
			DrawSprite(x, y, 3, 0, 0, 0, U32(0x28));
		};

		std::uint8_t before = U8(0x41);
		switch (U8(0x41)) {
			case 0x00: {
				I32(0x10) -= 0x200000;
				I32(0x0c) += 0xf0000;
				U8(0x41) = 0x1c;
				p->Batteries() = 0;
				p->Empties() = 0;
				_world->BossSlot() = _slot;
				break;
			}
			case 0x1c: {
				// Idle: the eye whites, the pupils following the player ±4 px, the body over them
				DrawSprite(x, y, 3, 0, 0, 0, FrameOf(Baas(1), 0));
				std::int32_t ex = std::clamp(DivPow2(p->X16() - x, 20), -4, 4);
				std::int32_t ey = std::clamp(DivPow2(p->Y16() - y + 0x200000, 20), -4, 4);
				DrawSprite(x + ex * 0x10000, y + ey * 0x10000, 3, 0, 0, 0, FrameOf(Baas(7), 0));
				U16(0x34) = Baas(1);
				U32(0x28) = FrameOf(U16(0x34), 1);
				DrawSprite(x, y, 3, 0, 0, 0, U32(0x28));
				if (0x46 < ++I32(0x24)) {
					I32(0x24) = 0;
					U8(0x3c) = 0;
					I32(0x58) = (I32(0x58) + 1) & 3;
					switch (I32(0x58)) {
						case 0: case 2:
							U8(0x41) = 7;
							I32(0x50) = (std::int32_t)(_world->Random() & 3);
							break;
						case 1:
							U16(0x34) = Baas(2);
							U8(0x41) = 0x1f;
							I32(0x5c) = (std::int32_t)(_world->Random() & 3) + 2;
							break;
						default:
							U16(0x34) = Baas(3);
							U8(0x41) = 0x1f;
							I32(0x5c) = (std::int32_t)(_world->Random() & 3) + 2;
							break;
					}
				}
				break;
			}
			case 0x1f: {
				// A gesture, 6 ticks per frame, +0x5c + 1 loops
				drawCurrent();
				if (5 < ++I32(0x24)) {
					I32(0x24) = 0;
					if (frameCount() <= ++S8(0x3c)) {
						U8(0x3c) = 0;
						if (--I32(0x5c) < 0) {
							U8(0x41) = 0x1c;
						}
					}
				}
				break;
			}
			case 0x07: {
				// A volley: a projectile at frame 10, again every 15 ticks while +0x50 lasts
				U16(0x34) = Baas(14);
				drawCurrent();
				if (4 < ++I32(0x24)) {
					I32(0x24) = 0;
					S8(0x3c)++;
					if (S8(0x3c) == 10) {
						PlaySample(x, y, 1, 0x80, 0);
						_world->Spawn(x, y - 0x640000, 0x44, _slot);
						if (0 <= --I32(0x50)) {
							U8(0x3c) = 9;
							I32(0x24) = -10;
						}
					} else if (frameCount() <= S8(0x3c)) {
						U8(0x41) = 0x1c;
					}
				}
				break;
			}
			case 0x10:
			case 0x0e: {
				if (U8(0x41) == 0x10) {
					// Woken by the red button
					if (n == 1) {
						PlaySample(0, 0, 4, 0x80, 0);
					} else if (n == 2) {
						PlaySample(0, 0, 5, 0x80, 0);
					} else if (n >= 3) {
						PlaySample(0, 0, 6, 0x80, 0);
					}
					U8(0x41) = 0x0e;
					LOGI("[Boss] tick {} slot {} shocked, {} channels powered", t, _slot, n);
				}
				// The shock: 35·n ticks of the X-ray flash, alternating on the rendered frames
				U8(0x3c) = (std::uint8_t)(_world->GetRenderedFrames() & 1);
				U16(0x34) = Baas(6);
				drawCurrent();
				for (std::int32_t ch = 0; ch < 4; ch++) {
					std::uint8_t b = _world->Trigger(ch);
					_world->Trigger(ch) = (b >= 7 ? (std::uint8_t)(b - 7) : 0);
				}
				if ((n * 70) / 2 < ++I32(0x24)) {
					U8(0x41) = 0x1d;
					U8(0x3c) = 0;
					I32(0x24) = 0;
					if (n == 1) {
						I32(0x6c) = 3;
						S8(0x45) = 1;
						U16(0x34) = Baas(11);
						break;
					} else if (n == 2) {
						I32(0x6c) = 3;
						S8(0x45) = 1;
						U16(0x34) = Baas(12);
						break;
					} else if (n >= 3) {
						S8(0x3e) = -1;
						I32(0x6c) = 6;
						U16(0x34) = Baas(5);
					}
				}
				S8(0x45) = 1;
				break;
			}
			case 0x1d: {
				// The reaction to a shock
				if (U16(0x34) == Baas(13) && S8(0x3c) != 0) {
					DrawSprite(x, y, 3, 0, 0, 0, FrameOf(Baas(13), 0));
				}
				if (I32(0x74) != 0) {
					// Something falls on its head from 100 px above while anim 0 holds its last frame
					std::uint16_t a = Baas(18);
					if ((t & 3) == 0) {
						I32(0x74)++;
					}
					if (I32(0x74) < FrameCountOf(a) + 1) {
						DrawSprite(x, y - 0x640000, 3, 0, 0, 0, FrameOf(a, I32(0x74) - 1));
						U16(0x34) = Baas(0);
						S8(0x3c) = (std::int8_t)(FrameCountOf(Baas(0)) - 1);
					} else {
						I32(0x74) = 0;
						S8(0x45) = -1;
						U16(0x34) = Baas(0);
						S8(0x3c) = (std::int8_t)(FrameCountOf(Baas(0)) - 2);
					}
				}
				drawCurrent();
				if ((++I32(0x24) & 3) == 0) {
					S8(0x3c) = (std::int8_t)(S8(0x3c) + S8(0x45));
					if (S8(0x3c) < 0) {
						U8(0x3c) = 0;
						U8(0x41) = 0x1c;
						I32(0x24) = 0;
						break;
					}
					if (frameCount() <= S8(0x3c)) {
						if (U16(0x34) == Baas(0)) {
							if (I32(0x74) == 0) {
								I32(0x74) = 1;
							}
							break;
						}
						U8(0x3c) = 0;
						if (0 < --I32(0x6c)) {
							break;
						}
						if (n == 1) {
							if (U16(0x34) == Baas(11)) {
								U16(0x34) = Baas(12);
								I32(0x6c) = 3;
							} else if (U16(0x34) == Baas(12)) {
								U16(0x34) = Baas(13);
								I32(0x6c) = 3;
							} else {
								U8(0x41) = 0x1c;
								I32(0x24) = 0;
							}
						} else if (n == 2) {
							if (U16(0x34) == Baas(12)) {
								U16(0x34) = Baas(0);
								I32(0x6c) = 1;
							} else if (U16(0x34) == Baas(0)) {
								U16(0x34) = Baas(13);
								I32(0x6c) = 1;
							} else {
								U8(0x41) = 0x1c;
								I32(0x24) = 0;
							}
						} else if (n >= 3) {
							U8(0x41) = 3;
							I32(0x70) = 0;
							PlaySample(0, 0, 0, 0x80, 0);
							LOGI("[Boss] tick {} slot {} defeated", t, _slot);
						}
					}
				}
				break;
			}
			case 0x03: {
				// Defeated: collapses and holds; 45 ticks later the sign drops to (320, 225) px
				U16(0x34) = Baas(4);
				drawCurrent();
				if ((++I32(0x24) & 3) == 0) {
					if (++S8(0x3c) == 4 && I32(0x70) == 0) {
						// Never true (+0x70 is n ≥ 3 here, rebuilt every tick), kept as the original has it
						I32(0x70) = 1;
						for (std::int32_t k = 0; k < 64; k++) {
							std::uint32_t r1 = _world->Random();
							std::uint32_t r2 = _world->Random();
							_world->SpawnSpark(x + ((std::int32_t)(r2 & 0xff) - 0x7f) * 0x10000, y + (std::int32_t)(r1 & 0x1f) * 0x10000, -1);
						}
					} else if (frameCount() <= S8(0x3c)) {
						S8(0x3c) = (std::int8_t)(frameCount() - 1);
					}
				}
				if (0x8c < I32(0x24)) {
					std::int32_t k = I32(0x24) - 0x8c;
					std::int32_t yy;
					if (k < 0x1e) {
						yy = (k * k) << 14;
					} else {
						yy = 0xe10000;
						I32(0x24) = 0x230;
					}
					DrawSprite(0x1400000, yy, 3, 0, 9, 0, FrameOf(Baas(17), 0));
				}
				break;
			}
			default:
				break;
		}
		if (U8(0x41) != before) {
			LOGI("[Boss] tick {} slot {} state 0x{:.2x} -> 0x{:.2x}", t, _slot, before, U8(0x41));
		}
	}

	// ---- 0x42 the red button ---------------------------------------------------------------------------------

	void RedButton::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		// Any live trigger of the channels 0..3 is held at its maximum
		for (std::int32_t ch = 0; ch < 4; ch++) {
			if (_world->Trigger(ch) != 0) {
				_world->Trigger(ch) = 0xff;
			}
		}
		// The boss (DAT_004c1408), or the empty slot 0 - which counts as alive
		BatteryObject* boss = _world->GetObject(_world->BossSlot());
		auto bossAlive = [&]() { return (boss == nullptr || 0 <= boss->S8(0x3e)); };

		switch (U8(0x41)) {
			case 0x00:
				I32(0x10) += 0x290000;
				I32(0x0c) += 0x360000;
				U8(0x41) = 0x1c;
				[[fallthrough]];
			case 0x1c: {
				// Invisible until a battery went into any of the channels 0..3
				for (std::int32_t ch = 0; ch < 4; ch++) {
					if (_world->Trigger(ch) != 0) {
						U8(0x41) = 0x10;
						return;
					}
				}
				return;
			}
			case 0x10: {
				// Appears, 4 ticks per frame
				U16(0x34) = Baas(8);
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, 0, 10, 0, U32(0x28));
				if ((++I32(0x24) & 3) == 0 && FrameCountOf(U16(0x34)) <= ++S8(0x3c)) {
					U8(0x3c) = 0;
					U8(0x41) = 0x1f;
				}
				return;
			}
			case 0x1f: {
				// Blinks on the real-time clock; pressed by holding Action within 80 px
				U8(0x3c) = (std::uint8_t)((p->GetClockMs() >> 7) & 1);
				U16(0x34) = Baas(9);
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, 0, 10, 0, U32(0x28));
				if (_world->IsPlayerWithin(this, 0x1900) && p->IsActionHeld()) {
					U8(0x41) = 0x1d;
					if (bossAlive()) {
						if (boss != nullptr) {
							boss->U8(0x41) = 0x10;
							boss->I32(0x24) = 0;
							boss->U8(0x3c) = 0;
						}
						I32(0x24) = 0;
						PlaySample(0, 0, 1, 0, 0);
						LOGI("[RedButton] tick {} slot {} pressed", p->GetTickCount(), _slot);
					}
				}
				return;
			}
			case 0x1d: {
				// Pressed; re-armed after 141 ticks while the boss lives
				U16(0x34) = Baas(10);
				U32(0x28) = FrameOf(U16(0x34), 0);
				DrawSprite(I32(0x0c), I32(0x10), 3, 0, 10, 0, U32(0x28));
				if (bossAlive() && 0x8c < ++I32(0x24)) {
					U8(0x41) = 0x1f;
				}
				return;
			}
			default:
				return;
		}
	}

	// ---- 0x44 the boss's projectile --------------------------------------------------------------------------

	void BossProjectile::Behave()
	{
		BatterymanPlayer* p = _world->GetPlayer();
		switch (U8(0x41)) {
			case 0x00:
				PlaySample(I32(0x0c), I32(0x10), 2, 0x100, 0);
				U8(0x41) = 0x1c;
				[[fallthrough]];
			case 0x1c: {
				// Charges for 24 ticks, then is aimed once at the player: 1/64 of the way per tick
				U16(0x34) = Baas(15);
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), 9, 0, U32(0x28));
				if ((++I32(0x24) & 3) == 0 && FrameCountOf(U16(0x34)) <= ++S8(0x3c)) {
					U8(0x41) = 7;
					I32(0x50) = 0;
					I32(0x54) = DivPow2(p->X16() - I32(0x0c), 6);
					I32(0x58) = DivPow2(p->Y16() - I32(0x10), 6);
				}
				return;
			}
			case 0x07: {
				I32(0x0c) += I32(0x54);
				I32(0x10) += I32(0x58);
				if (_world->TestPixel(I32(0x0c), I32(0x10)) != 0) {
					U8(0x41) = 0x0a;
				}
				std::int32_t k = ++I32(0x24);
				U16(0x34) = Baas(16);
				std::int32_t fc = FrameCountOf(U16(0x34));
				U8(0x3c) = (std::uint8_t)(fc > 0 ? k % fc : 0);
				U32(0x28) = FrameOf(U16(0x34), S8(0x3c));
				DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), 0, 0, U32(0x28));
				return;
			}
			case 0x0a: {
				// Bursts into 16 sparks (it hit a wall, left the level, or touched the player this tick)
				for (std::int32_t i = 0; i < 16; i++) {
					std::uint32_t r1 = _world->Random();
					std::uint32_t r2 = _world->Random();
					_world->SpawnSpark(I32(0x0c) + ((std::int32_t)(r2 & 0x1f) - 0xf) * 0x10000, I32(0x10) + ((std::int32_t)(r1 & 0x1f) - 0xf) * 0x10000, -1);
				}
				_world->Destroy(_slot);
				return;
			}
			default:
				return;
		}
	}
}
