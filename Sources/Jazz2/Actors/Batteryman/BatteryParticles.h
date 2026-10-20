#pragma once

#include "BatteryObject.h"

namespace Jazz2::Actors::Batteryman
{
	class BatteryWorld;

	/**
		@brief BatteryCheck's particles (`0x4c1420`, 512 × 0x20 bytes; teardown/43 §8)

		The record is kept byte for byte: `+0x00/+0x04` X/Y, `+0x08/+0x0c` speed (16.16), `+0x10` aux (the object a
		drop sticks to), `+0x14` type (0 = free), `+0x15` size, `+0x16` drawn counter, `+0x17..+0x1f` per type. The
		allocator, the spawners, the physics (`FUN_00416fa0`) and the drop's collision (`FUN_00416ce0`) are ported
		statement by statement, PRNG draws included, so the stream stays the original's. A particle that was not
		drawn by the last render dies on the next tick (the cull rule of `FUN_00416fa0`); the drawing pass, done once
		per rendered frame, is @ref MarkDrawn().
	*/
	class BatteryParticles
	{
	public:
		static constexpr std::int32_t Count = 512;
		static constexpr std::int32_t RecordSize = 0x20;

		explicit BatteryParticles(BatteryWorld* world);

		/** @brief `FUN_00416090` - everything free (level start) */
		void Clear();

		/** @brief `FUN_004161b0(x, y)` - a smoke puff scattered around a point (3 draws) */
		void SpawnSmoke(std::int32_t x, std::int32_t y);
		/** @brief `FUN_00416120(x, y, 3)` - a smoke puff at a point (no draws) */
		void SpawnSmokeAt(std::int32_t x, std::int32_t y);
		/** @brief `FUN_004162a0(x, y)` - a sparkle (MAN animation 61), flung up (2 draws) */
		void SpawnSparkle(std::int32_t x, std::int32_t y);
		/** @brief `FUN_00416360(x, y, p)` - a VONK spark: up for `p < 0` (2 draws), down otherwise (3 draws) */
		void SpawnSpark(std::int32_t x, std::int32_t y, std::int32_t p);
		/** @brief `FUN_004165d0(x, y)` - a rain drop, if the point is free (2 draws) */
		void SpawnRainDrop(std::int32_t x, std::int32_t y);
		/** @brief `FUN_004166b0(x, y)` - a heavy drip (2 draws) */
		void SpawnDrip(std::int32_t x, std::int32_t y);
		/** @brief `FUN_00416780(x, y, waterId)` - a bubble rising in a water volume (2 draws) */
		void SpawnBubble(std::int32_t x, std::int32_t y, std::int32_t waterId);
		/**
		 * @brief `FUN_00416850(obj, mode)` - the object's sprite bursts into pixels
		 *
		 * The pixels are picked with the C runtime's `rand()` among the opaque ones of the object's current frame
		 * (its collision mask), as there; their speeds come from the PRNG.
		 */
		void Shatter(BatteryObject* obj, std::int32_t mode);

		/** @brief `FUN_00416fa0` - one tick of every particle */
		void Tick();
		/**
		 * @brief The bookkeeping of the draw `FUN_004178a0`, once per rendered frame: visible particles count as drawn
		 *
		 * Types 7 and 8 are always drawn; the others only inside the view (by type, teardown/43 §8.4).
		 */
		void MarkDrawn(std::int32_t camX, std::int32_t camY);
		/**
		 * @brief The pixels of the draw `FUN_004178a0`: what @ref MarkDrawn() counted, at the particle render layer
		 *
		 * The pixel types are quads of their palette colours (the smoke's remap toward colour 0xee, ⅔ colour over
		 * ⅓ background, is drawn as that colour at ⅔ opacity), the others the original's sprites, in slot order.
		 */
		void Draw(RenderQueue& renderQueue, std::int32_t camX, std::int32_t camY);

		std::int32_t& I32(std::int32_t slot, std::size_t offset) {
			return *reinterpret_cast<std::int32_t*>(&_records[slot * RecordSize + offset]);
		}
		std::uint8_t& U8(std::int32_t slot, std::size_t offset) {
			return _records[slot * RecordSize + offset];
		}

	private:
		BatteryWorld* _world;
		std::uint8_t _records[Count * RecordSize];
		std::int32_t _cursor;			// DAT_00452844
		bool _drawnSinceTick;			// DAT_00452840
		std::uint32_t _crtRandom;		// the C runtime's rand() seed (0x44f940, 1 at program start)
		SmallVector<std::unique_ptr<RenderCommand>, 0> _quadCommands;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _spriteCommands;
		Metadata* _metadata;

		/** @brief One pixel of colour `index` at a view pixel */
		void DrawPixel(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::uint8_t index, float alpha, std::uint16_t order);
		/** @brief `FUN_0040e800(sx, sy, 1, 0, frame)` - a sprite at a view pixel */
		void DrawFrame(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::uint32_t frameHandle, std::uint16_t order);

		/** @brief `FUN_004160b0` - the first free slot from the cursor, or -1 (the cursor then restarts at 0) */
		std::int32_t Allocate();
		/** @brief `FUN_00416100(i)` */
		void Free(std::int32_t slot);
		/** @brief `FUN_00416ce0(drop)` - what a falling drop hits: an object's slot, -1 the player (damaged), 0 nothing */
		std::int32_t HitTestDrop(std::int32_t slot);
		/** @brief MSVC `rand()`: `s = s * 214013 + 2531011; return (s >> 16) & 0x7fff` */
		std::int32_t CrtRand();
	};
}
