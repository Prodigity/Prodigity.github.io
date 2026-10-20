#include "BatteryParticles.h"
#include "BatteryWorld.h"
#include "BatterymanPlayer.h"
#include "../../ContentResolver.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <algorithm>
#include <cstring>

// Ports of teardown/43 §8 and the decompiles of FUN_004160b0, FUN_004161b0, FUN_00416850, FUN_00416ce0 and
// FUN_00416fa0. Record offsets: +0x00 X, +0x04 Y, +0x08 vx, +0x0c vy, +0x10 aux, +0x14 type, +0x15 size,
// +0x16 drawn, +0x17.. per type.

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		constexpr std::int32_t X = 0x00, Y = 0x04, VX = 0x08, VY = 0x0c, Aux = 0x10;
		constexpr std::int32_t Type = 0x14, Size = 0x15, Drawn = 0x16, B17 = 0x17, B18 = 0x18, B19 = 0x19;

		/** @brief DAT_004d82e0, the particles' gravity */
		constexpr std::int32_t Gravity = 0x2000;
	}

	BatteryParticles::BatteryParticles(BatteryWorld* world)
		: _world(world), _cursor(0), _drawnSinceTick(false), _crtRandom(1), _metadata(nullptr)
	{
		Clear();
	}

	void BatteryParticles::Clear()
	{
		std::memset(_records, 0, sizeof(_records));
		_cursor = 0;
	}

	std::int32_t BatteryParticles::Allocate()
	{
		// FUN_004160b0: from the cursor to the end, no wrap; a failure restarts the cursor at 0
		if (_cursor < Count) {
			while (_cursor < Count && U8(_cursor, Type) != 0) {
				_cursor++;
			}
		} else {
			_cursor = Count;
		}
		if (_cursor < Count) {
			return _cursor++;
		}
		_cursor = 0;
		return -1;
	}

	void BatteryParticles::Free(std::int32_t slot)
	{
		// FUN_00416100
		U8(slot, Type) = 0;
		if (slot < _cursor) {
			_cursor = slot;
		}
	}

	std::int32_t BatteryParticles::CrtRand()
	{
		_crtRandom = _crtRandom * 214013u + 2531011u;
		return (std::int32_t)((_crtRandom >> 16) & 0x7fff);
	}

	void BatteryParticles::SpawnSmoke(std::int32_t x, std::int32_t y)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		std::uint32_t r1 = _world->Random();
		I32(i, X) = x - 0x7ffe + (std::int32_t)(r1 & 0x7fff) * 2;
		std::uint32_t r2 = _world->Random();
		I32(i, Y) = y + (std::int32_t)(r2 & 0x7fff) * -2;
		std::uint32_t r3 = _world->Random();
		U8(i, Size) = (std::uint8_t)(r3 & 3);
		U8(i, Drawn) = 1;
		U8(i, Type) = 3;
		U8(i, B17) = 0x47;
	}

	void BatteryParticles::SpawnSmokeAt(std::int32_t x, std::int32_t y)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		U8(i, Drawn) = 1;
		U8(i, Type) = 3;
		U8(i, B17) = 0x47;
	}

	void BatteryParticles::SpawnSparkle(std::int32_t x, std::int32_t y)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		std::uint32_t r1 = _world->Random();
		I32(i, VY) = -(std::int32_t)(r1 & 0x7fff) * 4;
		std::uint32_t r2 = _world->Random();
		I32(i, VX) = (std::int32_t)(r2 & 0x7fff) * 4 - 0xfffc;
		U8(i, Size) = 0;
		U8(i, Drawn) = 1;
		U8(i, Type) = 5;
		U8(i, B17) = 0x28;
	}

	void BatteryParticles::SpawnSpark(std::int32_t x, std::int32_t y, std::int32_t p)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		std::uint32_t r1 = _world->Random();
		if (p < 0) {
			I32(i, VY) = -(std::int32_t)(r1 & 0x7fff) * 4;
			std::uint32_t r2 = _world->Random();
			I32(i, VX) = (std::int32_t)(r2 & 0x7fff) * 4 - 0xfffc;
			U8(i, B17) = 0;
		} else {
			I32(i, VY) = (std::int32_t)(r1 & 0x7fff) * 4;
			std::uint32_t r2 = _world->Random();
			I32(i, VX) = (std::int32_t)(r2 & 0x7fff) - 0x3fff;
			std::uint32_t r3 = _world->Random();
			U8(i, B17) = (std::uint8_t)(r3 & 7);
		}
		U8(i, Drawn) = 1;
		U8(i, Type) = 9;
		if (_world->IsTracingParticles()) {
			LOGI("[Trace] t {} particle spark {} at {} {} p {}", _world->GetPlayer()->GetTickCount(), i, x >> 16, y >> 16, p);
		}
	}

	void BatteryParticles::SpawnRainDrop(std::int32_t x, std::int32_t y)
	{
		if (_world->IsPixelSolid(x, y)) {
			return;
		}
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		std::uint32_t r1 = _world->Random();
		I32(i, VX) = 0x7ff - (std::int32_t)(r1 & 0xfff);
		std::uint32_t r2 = _world->Random();
		I32(i, VY) = (std::int32_t)(r2 & 0x7fff) * 8 + 0x50000;
		U8(i, B17) = 0;
		U8(i, B18) = 0;
		U8(i, B19) = 0;
		U8(i, Drawn) = 1;
		U8(i, Type) = 7;
		if (_world->IsTracingParticles()) {
			LOGI("[Trace] t {} particle rain {} at {} {}", _world->GetPlayer()->GetTickCount(), i, x >> 16, y >> 16);
		}
	}

	void BatteryParticles::SpawnDrip(std::int32_t x, std::int32_t y)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		std::uint32_t r1 = _world->Random();
		I32(i, VX) = (std::int32_t)(r1 & 0x3fff) - 0x1fff;
		std::uint32_t r2 = _world->Random();
		I32(i, VY) = (std::int32_t)(r2 & 0x3fff) + 0x8000;
		U8(i, B17) = 0;
		U8(i, B18) = 0;
		U8(i, B19) = 1;
		U8(i, Drawn) = 1;
		U8(i, Type) = 7;
		if (_world->IsTracingParticles()) {
			LOGI("[Trace] t {} particle drip {} at {} {}", _world->GetPlayer()->GetTickCount(), i, x >> 16, y >> 16);
		}
	}

	void BatteryParticles::SpawnBubble(std::int32_t x, std::int32_t y, std::int32_t waterId)
	{
		std::int32_t i = Allocate();
		if (i < 0) {
			return;
		}
		I32(i, X) = x;
		I32(i, Y) = y;
		std::uint32_t r1 = _world->Random();
		I32(i, VX) = (std::int32_t)(r1 & 0x3fff) - 0x1fff;
		std::uint32_t r2 = _world->Random();
		I32(i, VY) = (-0x2000 - (std::int32_t)(r2 & 0x7fff)) * 2;
		U8(i, B17) = (std::uint8_t)waterId;
		U8(i, B18) = 0;
		U8(i, B19) = 0x23;
		U8(i, Drawn) = 1;
		U8(i, Type) = 8;
	}

	void BatteryParticles::Shatter(BatteryObject* obj, std::int32_t mode)
	{
		// FUN_00416850
		const FrameInfo* f = BatteryAnims::Get().GetFrame(obj->U32(0x28));
		if (f == nullptr || f->Pixels.empty()) {
			return;
		}
		std::int32_t w = (std::uint16_t)f->Width;
		std::int32_t total = (std::uint16_t)f->Height * w;
		// The decoded frame, with the original's 0x400 bytes of slack after it (read by the neighbour picks)
		SmallVector<std::uint8_t, 0> buffer;
		buffer.resize(total + 0x400);
		std::memset(buffer.data(), 0, buffer.size());
		std::memcpy(buffer.data(), f->Pixels.data(), std::min<std::size_t>(f->Pixels.size(), (std::size_t)total));

		std::int8_t facing = obj->S8(0x45);
		std::int32_t baseX = (facing < 1 ? obj->I32(0x0c) + (f->HotspotX + w) * -0x10000 : f->HotspotX * 0x10000 + obj->I32(0x0c));
		std::int32_t baseY = obj->I32(0x10);
		std::int32_t left = total >> 2;
		std::int32_t i = Allocate();
		while (i >= 0) {
			// A random opaque pixel; after more than 16 misses in a row the burst ends
			std::int32_t misses = 0, index;
			std::uint8_t colour;
			do {
				index = CrtRand() % total;
				misses++;
				colour = buffer[index];
				if (0x10 < misses) {
					return;
				}
			} while (colour == 0);
			std::uint8_t* px = &buffer[index];
			std::int32_t col = index % w;
			*px = 0;
			if (facing < 0) {
				col = w - col;
			}
			I32(i, X) = col * 0x10000 + baseX;
			I32(i, Y) = (index / w) * 0x10000 + f->HotspotY * 0x10000 + baseY;
			if (mode == 0) {
				std::uint32_t r1 = _world->Random();
				I32(i, VX) = (std::int32_t)(r1 & 0x7fff) * 4 - 0xfffc;
				std::uint32_t r2 = _world->Random();
				U8(i, Type) = 1;
				I32(i, VY) = (-0x4000 - (std::int32_t)(r2 & 0x7fff)) * 4;
			} else if (mode == 1) {
				std::uint32_t r1 = _world->Random();
				I32(i, VY) = (std::int32_t)(r1 & 0x7fff);
				U8(i, Type) = 2;
				std::uint32_t r2 = _world->Random();
				U8(i, Size) = (std::uint8_t)(r2 & 3);
				U8(i, B17) = 0x28;
				std::uint32_t r3 = _world->Random();
				if ((r3 & 7) == 1) {
					SpawnSmokeAt(I32(i, X), I32(i, Y) - 0x10000);
				}
			} else if (mode == 2) {
				std::uint32_t r1 = _world->Random();
				I32(i, VX) = (std::int32_t)(r1 & 0x7fff) * 8 - 0x1fff8;
				std::uint32_t r2 = _world->Random();
				U8(i, Type) = 1;
				I32(i, VY) = (-0x4000 - (std::int32_t)(r2 & 0x7fff)) * 8;
				std::uint32_t r3 = _world->Random();
				U8(i, Size) = (std::uint8_t)(r3 & 3);
			}
			U8(i, Drawn) = 1;
			if (mode != 1) {
				// The pixel's colour, and for the bigger sizes its neighbours (the unpicked bytes stay as they were)
				std::uint32_t r = _world->Random();
				std::uint8_t size = (std::uint8_t)(r & 3);
				U8(i, Size) = size;
				if (size == 0) {
					U8(i, B17) = colour;
				} else if (size == 1) {
					U8(i, B17) = colour;
					U8(i, 0x18) = px[1];
					U8(i, 0x19) = px[w];
					U8(i, 0x1a) = px[w + 1];
				} else {
					std::uint32_t bits = _world->Random();
					U8(i, B17) = colour;
					if ((bits & 0x02) != 0) U8(i, 0x18) = px[1];
					if ((bits & 0x04) != 0) U8(i, 0x19) = px[2];
					std::uint8_t* row = px + w;
					if ((bits & 0x08) != 0) U8(i, 0x1a) = row[0];
					if ((bits & 0x10) != 0) U8(i, 0x1b) = row[1];
					if ((bits & 0x20) != 0) U8(i, 0x1c) = row[2];
					row += w;
					if ((bits & 0x40) != 0) U8(i, 0x1d) = row[0];
					if ((bits & 0x80) != 0) U8(i, 0x1e) = row[1];
					if ((bits & 0x100) != 0) U8(i, 0x1f) = row[2];
				}
			}
			if (--left < 1) {
				return;
			}
			i = Allocate();
		}
	}

	std::int32_t BatteryParticles::HitTestDrop(std::int32_t i)
	{
		// FUN_00416ce0: the first object (in slot order) whose box holds the drop catches it - a water volume by its
		// water, the belt by its length, anything else drawn by its current frame's box; else the player, on any
		// one overlapping mask pixel, takes the damage
		std::int32_t x = I32(i, X), y = I32(i, Y);
		for (std::int32_t s = 1; s < _world->GetLiveEnd(); s++) {
			BatteryObject* o = _world->GetObject(s);
			if (o == nullptr) {
				continue;
			}
			if (o->U8(0x44) == 0x2e) {
				if (0 < o->I32(0x6c) && o->I32(0x0c) < x && x < o->I32(0x54) * 0x10000 + o->I32(0x0c) &&
					o->I32(0x10) < y && y < o->I32(0x68) * 0x10000 + o->I32(0x10)) {
					return s;
				}
				continue;
			}
			const FrameInfo* f = BatteryAnims::Get().GetFrame(o->U32(0x28));
			if (f == nullptr) {
				continue;
			}
			std::int32_t top = f->HotspotY * 0x10000 + o->I32(0x10);
			std::int32_t bottom = ((std::int32_t)(std::uint16_t)f->Height + f->HotspotY) * 0x10000 + o->I32(0x10);
			if (!(top <= y && y < bottom)) {
				continue;
			}
			if (o->U8(0x44) == 0x2f) {
				if (o->I32(0x0c) <= x && x < o->I32(0x60) * 0x10000 + o->I32(0x0c)) {
					return s;
				}
			} else if (o->S8(0x45) < 0) {
				if (o->I32(0x0c) + ((std::int32_t)(std::uint16_t)f->Width + f->HotspotX) * -0x10000 <= x && x < o->I32(0x0c) + f->HotspotX * -0x10000) {
					return s;
				}
			} else if (f->HotspotX * 0x10000 + o->I32(0x0c) <= x && x < ((std::int32_t)(std::uint16_t)f->Width + f->HotspotX) * 0x10000 + o->I32(0x0c)) {
				return s;
			}
		}

		BatterymanPlayer* p = _world->GetPlayer();
		auto pixels = [](std::int32_t v) { return (v + ((v >> 31) & 0xffff)) >> 16; };
		std::uint32_t dropFrame = MakeFrameHandle((std::int32_t)AnimSet::Druppel, U8(i, B19) != 0 ? 1 : 0, U8(i, B17));
		std::int32_t n = BatteryAnims::Get().CountMaskOverlap(p->GetFrameHandle(), pixels(p->X16()), pixels(p->Y16()), p->GetFacing(),
			dropFrame, pixels(x), pixels(y), 1);
		if (n != 0) {
			p->Energy() -= _world->DropDamage();
			if (_world->IsTracingParticles()) {
				LOGI("[Trace] t {} particle {} hits the player: energy {} (-{})", p->GetTickCount(), i, p->Energy(), _world->DropDamage());
			}
			_world->PlayWorldSample(x, y, 0x1f, 0x20, 0);
			SpawnSparkle(x, y);
			SpawnSparkle(x, y);
			SpawnSmoke(x, y);
			SpawnSmoke(x, y);
			SpawnSmoke(x, y);
			return -1;
		}
		return 0;
	}

	void BatteryParticles::Tick()
	{
		// FUN_00416fa0
		std::uint32_t tick = _world->GetPlayer()->GetTickCount();
		for (std::int32_t i = 0; i < Count; i++) {
			std::uint8_t type = U8(i, Type);
			if (type == 0) {
				continue;
			}
			// Not drawn by the last render: gone
			if (U8(i, Drawn) == 0 && _drawnSinceTick) {
				Free(i);
				continue;
			}

			bool killed = false;
			switch (type) {
				case 1: { // pixel debris: slows down, bounces off walls and floors
					std::int32_t vx = I32(i, VX);
					if (vx > 0x100) {
						I32(i, VX) = vx - 0x100;
					} else if (vx < -0x100) {
						I32(i, VX) = vx + 0x100;
					} else {
						I32(i, VX) = 0;
					}
					I32(i, X) += I32(i, VX);
					if (_world->IsPixelSolid(I32(i, X), I32(i, Y))) {
						I32(i, X) -= I32(i, VX);
						std::int32_t t = I32(i, VX) * -7;
						I32(i, VX) = (t + ((t >> 31) & 7)) >> 3;
					}
					I32(i, Y) += I32(i, VY);
					I32(i, VY) += Gravity / 2;
					if (_world->IsPixelSolid(I32(i, X), I32(i, Y))) {
						std::int32_t vy = I32(i, VY);
						std::int32_t bounced = -(vy / 2);
						I32(i, VY) = bounced;
						std::int32_t magnitude = (bounced < 1 ? vy / 2 : bounced);
						if (magnitude < 0x4000) {
							Free(i);
							killed = true;
						} else {
							I32(i, Y) += bounced;
						}
					}
					break;
				}
				case 2: { // glowing pixel: cools down (colour + 1 on 1 tick in 8), sometimes smokes, falls slowly
					std::uint32_t r = _world->Random();
					if ((r & 7) == 0) {
						U8(i, B17)++;
						std::uint32_t r2 = _world->Random();
						if ((r2 & 7) == 0) {
							SpawnSmokeAt(I32(i, X), I32(i, Y) - 0x10000);
						}
					}
					if (0x2f < U8(i, B17)) {
						Free(i);
						continue;
					}
					std::int32_t g = (Gravity + ((Gravity >> 31) & 7)) >> 3;
					std::int32_t vy = I32(i, VY);
					I32(i, VY) = vy + g;
					I32(i, Y) += vy + g;
					if (_world->IsPixelSolid(I32(i, X), I32(i, Y))) {
						I32(i, Y) -= I32(i, VY);
						I32(i, VY) = -(I32(i, VY) / 2);
					}
					break;
				}
				case 3: { // smoke: rises, wobbles on the sine, fades
					std::uint32_t r1 = _world->Random();
					I32(i, Y) += (std::int32_t)(r1 & 0x7fff) * -2;
					std::uint32_t r2 = _world->Random();
					I32(i, X) += BatteryWorld::Sine((std::int32_t)(r2 & 0x3ff));
					std::uint32_t r3 = _world->Random();
					if ((r3 & 0xf) == 0) {
						U8(i, B17)--;
					}
					if (U8(i, B17) < 0x40) {
						Free(i);
						killed = true;
					}
					break;
				}
				case 4: { // ember (no spawner in the game)
					std::uint32_t r = _world->Random();
					if ((r & 7) == 0) {
						U8(i, B17)++;
					}
					if (0x27 < U8(i, B17)) {
						Free(i);
						continue;
					}
					std::int32_t g = (Gravity + ((Gravity >> 31) & 3)) >> 2;
					std::int32_t vy = I32(i, VY);
					I32(i, VY) = vy + g;
					I32(i, Y) += vy + g;
					break;
				}
				case 9: { // VONK spark
					std::uint32_t r = _world->Random();
					if ((r & 3) == 0) {
						U8(i, B17)++;
					}
					if (10 < U8(i, B17)) {
						Free(i);
						continue;
					}
					std::int32_t vy = I32(i, VY);
					I32(i, VY) = vy + 0x1000;
					I32(i, Y) += vy + 0x1000;
					I32(i, X) += I32(i, VX);
					break;
				}
				case 5: { // sparkle
					std::int32_t vy = I32(i, VY);
					I32(i, VY) = vy + 0x1000;
					I32(i, Y) += vy + 0x1000;
					I32(i, X) += I32(i, VX);
					break;
				}
				case 6: { // floating text (no spawner in the game)
					std::int32_t vx = I32(i, VX), vy = I32(i, VY);
					I32(i, X) += vx;
					I32(i, Y) += vy;
					I32(i, VY) = ((vy + ((vy >> 31) & 0xf)) >> 4) + vy;
					I32(i, VX) = ((vx + ((vx >> 31) & 0xf)) >> 4) + vx;
					break;
				}
				case 8: { // bubble: rises inside its water, pops at the surface or the sides
					if (U8(i, B18) < 8) {
						std::int32_t oldY = I32(i, Y);
						I32(i, Y) = oldY + I32(i, VY);
						std::int32_t nx = I32(i, VX) + I32(i, X);
						I32(i, X) = nx;
						BatteryObject* water = _world->GetObject(_world->WaterSlot(U8(i, B17)));
						U8(i, B18) = (std::uint8_t)(((tick >> 2) + (std::uint32_t)i) & 7);
						std::int32_t wx = (water != nullptr ? water->I32(0x0c) : 0), wy = (water != nullptr ? water->I32(0x10) : 0);
						std::int32_t ww = (water != nullptr ? water->I32(0x54) : 0);
						if (oldY + I32(i, VY) < wy + 0xa0000 || nx < wx + 0x80000 || (ww - 8) * 0x10000 + wx < nx) {
							U8(i, B18) = 8;
						}
					} else {
						if ((((std::uint32_t)i ^ tick) & 7) == 0) {
							U8(i, B18)++;
						}
						if (0xb < U8(i, B18)) {
							Free(i);
							killed = true;
						}
					}
					break;
				}
				case 7: { // rain drop / drip
					if (U8(i, B18) == 0) {
						std::int32_t vx = I32(i, VX);
						I32(i, X) += vx;
						std::int32_t vy = I32(i, VY);
						I32(i, Y) += vy;
						std::uint32_t r1 = _world->Random();
						I32(i, VX) = (std::int32_t)(r1 & 0x3ff) - 0x1ff + vx;
						std::uint32_t r2 = _world->Random();
						std::int32_t dvy = (U8(i, B19) == 0 ? (std::int32_t)(r2 & 0x3ff) - 0x1ff : (std::int32_t)(r2 & 0x3ff) + 0x1000);
						I32(i, VY) = dvy + vy;
						U8(i, B17) = (std::uint8_t)((tick >> 2) & 1);
						if (!_world->IsPixelSolid(I32(i, X), I32(i, Y))) {
							std::int32_t hit = HitTestDrop(i);
							if (hit < 0) {
								Free(i);
								killed = true;
							} else if (hit > 0) {
								// It splashes on the object and moves with it
								_world->PlayWorldSample(I32(i, X), I32(i, Y), 0x2b, 10, 0);
								BatteryObject* o = _world->GetObject(hit);
								I32(i, Aux) = hit;
								U8(i, B18) = 1;
								I32(i, VX) = I32(i, X) - (o != nullptr ? o->I32(0x0c) : 0);
								I32(i, VY) = I32(i, Y) - (o != nullptr ? o->I32(0x10) : 0);
							}
						} else {
							_world->PlayWorldSample(I32(i, X), I32(i, Y), 0x2b, 10, 0);
							U8(i, B18) = 1;
							I32(i, VX) = 0;
							I32(i, VY) = 0;
							I32(i, Aux) = 0;
						}
					} else {
						std::uint8_t phase = (std::uint8_t)(U8(i, B18) + 1);
						U8(i, B18) = phase;
						if (I32(i, Aux) != 0) {
							BatteryObject* o = _world->GetObject(I32(i, Aux));
							if (o != nullptr) {
								I32(i, X) = I32(i, VX) + o->I32(0x0c);
								I32(i, Y) = I32(i, VY) + o->I32(0x10);
							}
						}
						if ((phase & 7) == 0) {
							if (U8(i, B17) < 4) {
								U8(i, B17)++;
							} else {
								Free(i);
								killed = true;
							}
						}
					}
					break;
				}
				default: { // tile shards (no spawner in the game)
					if (0x3f < type) {
						std::int32_t vy = I32(i, VY);
						I32(i, X) += I32(i, VX);
						I32(i, Y) += vy;
						I32(i, VY) = Gravity / 2 + vy;
					}
					break;
				}
			}
			if (!killed) {
				U8(i, Drawn) = 0;
			}
		}
		_drawnSinceTick = false;
	}

	void BatteryParticles::MarkDrawn(std::int32_t camX, std::int32_t camY)
	{
		// The draw FUN_004178a0, without the pixels: which particles it draws (and so counts) this frame
		_drawnSinceTick = true;
		constexpr std::int32_t W = BatterymanPlayer::ViewWidth, H = BatterymanPlayer::ViewHeight;
		for (std::int32_t i = 0; i < Count; i++) {
			std::uint8_t type = U8(i, Type);
			if (type == 0) {
				continue;
			}
			std::int32_t dx = I32(i, X) - camX, dy = I32(i, Y) - camY;
			std::int32_t sx = dx / 0x10000, sy = dy / 0x10000;
			bool visible;
			switch (type) {
				case 1: case 2: case 3: case 4:
					visible = ((std::uint32_t)sx < (std::uint32_t)(W - 4) && (std::uint32_t)sy < (std::uint32_t)(H - 3));
					break;
				case 5: case 9:
					visible = (-17 < sx && sx < W + 16 && -17 < sy && sy < H + 16);
					break;
				case 6:
					visible = (sy >= 0 && -0x41 < sx && sx <= W);
					break;
				case 7: case 8:
					visible = true;
					break;
				default:
					visible = (-0x41 < sx && sx < W + 0x30 && -0x81 < sy && sy < H + 0x30);
					break;
			}
			if (visible) {
				U8(i, Drawn)++;
			}
		}
	}

	void BatteryParticles::Draw(RenderQueue& renderQueue, std::int32_t camX, std::int32_t camY)
	{
		if (_metadata == nullptr) {
			_metadata = ContentResolver::Get().RequestMetadata("Interactive/BatteryObjects"_s);
		}
		constexpr std::int32_t W = BatterymanPlayer::ViewWidth, H = BatterymanPlayer::ViewHeight;
		std::uint32_t tick = _world->GetPlayer()->GetTickCount();
		std::int32_t viewX = (camX >> 16), viewY = (camY >> 16);
		std::int32_t quads = 0, sprites = 0;
		for (std::int32_t i = 0; i < Count; i++) {
			std::uint8_t type = U8(i, Type);
			if (type == 0) {
				continue;
			}
			std::int32_t dx = I32(i, X) - camX, dy = I32(i, Y) - camY;
			std::int32_t sx = (dx + ((dx >> 31) & 0xffff)) >> 16, sy = (dy + ((dy >> 31) & 0xffff)) >> 16;
			std::int32_t x = viewX + sx, y = viewY + sy;
			std::uint16_t order = (std::uint16_t)i;
			switch (type) {
				case 5:
					if (-0x11 < sx && -0x11 < sy && sx < W + 0x10 && sy < H + 0x10) {
						DrawFrame(renderQueue, sprites, x, y, MakeFrameHandle((std::int32_t)AnimSet::Man, 61, (std::int32_t)(((tick + (std::uint32_t)i) >> 2) % 5)), order);
					}
					break;
				case 9:
					if (-0x11 < sx && -0x11 < sy && sx < W + 0x10 && sy < H + 0x10) {
						DrawFrame(renderQueue, sprites, x, y, MakeFrameHandle((std::int32_t)AnimSet::Vonk, 0, U8(i, B17)), order);
					}
					break;
				case 7:
					DrawFrame(renderQueue, sprites, x, y, MakeFrameHandle((std::int32_t)AnimSet::Druppel, U8(i, B19) != 0 ? 1 : 0, U8(i, B17)), order);
					break;
				case 8:
					DrawFrame(renderQueue, sprites, x, y, MakeFrameHandle((std::int32_t)AnimSet::Water, 0, U8(i, B18)), order);
					break;
				case 1: case 2: case 3: {
					if (!((std::uint32_t)sx < (std::uint32_t)(W - 4) && (std::uint32_t)sy < (std::uint32_t)(H - 3))) {
						break;
					}
					std::uint8_t size = U8(i, Size);
					if (type == 1) {
						// The pixels themselves; after the first, a 0 byte is a hole
						std::int32_t n = (size == 0 ? 1 : (size == 1 ? 2 : 3));
						for (std::int32_t row = 0; row < n; row++) {
							for (std::int32_t col = 0; col < n; col++) {
								std::uint8_t c = U8(i, B17 + row * n + col);
								if (c != 0 || (row == 0 && col == 0)) {
									DrawPixel(renderQueue, quads, x + col, y + row, c, 1.0f, order);
								}
							}
						}
					} else {
						// FUN_00417810 (solid colour) and FUN_00417700 (the smoke's remap toward 0xee) - the same shapes
						std::uint8_t c = (type == 2 ? U8(i, B17) : (std::uint8_t)0xee);
						// Remap row 0xee is the nearest colour to (2·colour + background) / 3 (FUN_00411180)
						float alpha = (type == 2 ? 1.0f : 2.0f / 3.0f);
						switch (size & 3) {
							case 0:
								DrawPixel(renderQueue, quads, x, y, c, alpha, order);
								break;
							case 1:
								DrawPixel(renderQueue, quads, x, y, c, alpha, order);
								DrawPixel(renderQueue, quads, x + 1, y, c, alpha, order);
								break;
							case 2:
								for (std::int32_t k = 0; k < 4; k++) {
									DrawPixel(renderQueue, quads, x + (k & 1), y + (k >> 1), c, alpha, order);
								}
								break;
							default:
								for (std::int32_t k = 0; k < 3; k++) {
									DrawPixel(renderQueue, quads, x + k, y, c, alpha, order);
									DrawPixel(renderQueue, quads, x + k, y + 2, c, alpha, order);
								}
								for (std::int32_t k = -1; k < 4; k++) {
									DrawPixel(renderQueue, quads, x + k, y + 1, c, alpha, order);
								}
								break;
						}
					}
					break;
				}
				default:
					// Types 4, 6 and the tile shards have no spawner in the game
					break;
			}
		}
	}

	void BatteryParticles::DrawPixel(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::uint8_t index, float alpha, std::uint16_t order)
	{
		if (used >= (std::int32_t)_quadCommands.size()) {
			auto& command = _quadCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
			command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		auto* command = _quadCommands[used++].get();
		std::uint32_t rgba = ContentResolver::Get().GetPalettes()[index];
		Colorf color((float)(rgba & 0xff) / 255.0f, (float)((rgba >> 8) & 0xff) / 255.0f, (float)((rgba >> 16) & 0xff) / 255.0f, alpha);
		auto* instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(1.0f, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
		command->SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
		command->SetLayer(BatteryWorld::GetParticleRenderLayer());
		command->SetVisitOrder(order);
		renderQueue.AddCommand(command);
	}

	void BatteryParticles::DrawFrame(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::uint32_t frameHandle, std::uint16_t order)
	{
		if (_metadata == nullptr || frameHandle == 0) {
			return;
		}
		std::uint32_t v = frameHandle - 1;
		std::uint32_t set = (v >> 16), anim = ((v >> 8) & 0xff), frame = (v & 0xff);
		auto* res = _metadata->FindAnimation((AnimState)(0x10000u + (set << 8) + anim));
		// WATER frame 11 is read past the end of its list in the original; nothing is drawn for it here
		if (res == nullptr || res->Base == nullptr || res->Base->TextureDiffuse == nullptr || (std::int32_t)frame >= res->FrameCount) {
			return;
		}
		if (used >= (std::int32_t)_spriteCommands.size()) {
			auto& command = _spriteCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
			command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		auto* command = _spriteCommands[used++].get();
		auto& resolver = ContentResolver::Get();
		auto* base = res->Base;
		bool indexed = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
		resolver.ConfigureSpriteShader(*command, indexed);

		Vector2i texSize = base->TextureDiffuse->GetSize();
		Recti frameRect = base->GetFrameRect(res->FrameOffset + (std::int32_t)frame);
		auto* instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(float(frameRect.W) / float(texSize.X),
			float(frameRect.X) / float(texSize.X), float(frameRect.H) / float(texSize.Y), float(frameRect.Y) / float(texSize.Y));
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)frameRect.W, (float)frameRect.H);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());
		// The frame's top-left at (x + hotX, y + hotY), as for the objects' sprites
		command->SetTransformation(Matrix4x4f::Translation((float)(x - base->Hotspot.X), (float)(y - base->Hotspot.Y), 0.0f));
		command->SetLayer(BatteryWorld::GetParticleRenderLayer());
		command->SetVisitOrder(order);
		resolver.BindSpritePalette(*command, *base->TextureDiffuse.get(), indexed, res->PaletteOffset);
		renderQueue.AddCommand(command);
	}
}
