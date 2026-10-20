#pragma once

#include "../ActorBase.h"
#include "BatteryAnims.h"

#include <cstring>

namespace Jazz2::Actors::Batteryman
{
	class BatteryWorld;

	/**
		@brief One slot of BatteryCheck's object pool (`DAT_004b11dc`, 512 × 0x80 bytes)

		The original's objects are a 128-byte record and a behaviour function (`+0x00`) called once per 70 Hz tick.
		The record is kept here byte for byte, with accessors at the original offsets, so a behaviour reads as its
		decompiled routine does; @ref Behave() is the routine. The position fields drive this actor's own
		position, and the sprites a behaviour draws (`FUN_0040e080`) are replayed by @ref OnDraw().

		Record fields used by the framework (BATTERY-OBJECTS.md §1): `+0x04/+0x08` spawn X/Y, `+0x0c/+0x10` X/Y,
		`+0x14/+0x18` X/Y speed (16.16), `+0x28` frame handle, `+0x30` spawn flags, `+0x34` animation handle,
		`+0x36` animation set, `+0x3c` frame, `+0x3d` hit flags, `+0x40` class (low nibble), `+0x41` state,
		`+0x44` event type, `+0x45` facing.
	*/
	class BatteryObject : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

		friend class BatteryWorld;

	public:
		/** @brief Size of the original's object record */
		static constexpr std::int32_t RecordSize = 0x80;

		BatteryObject();
		~BatteryObject() override;

		/** @brief The behaviour routine of the object's type, one call per tick (`(*obj[+0])(slot)`) */
		virtual void Behave() {}
		/**
		 * @brief `FUN_0040bd00(slot, arg)` - the touch handler, called when the player's sprite overlaps this one
		 *
		 * The pre-update (`FUN_004095a0` → `FUN_0040be70`) calls it with 0 for every object that is not a
		 * projectile (class 1).
		 */
		virtual void Touch(std::int32_t arg);

		/** @brief The frame handle of frame `frame` of an animation handle (`frames(anim)[frame]`) */
		static std::uint32_t FrameOf(std::uint16_t animHandle, std::int32_t frame) {
			return MakeFrameHandle(animHandle >> 8, animHandle & 0xff, frame);
		}
		/** @brief The frame count of an animation handle */
		static std::int32_t FrameCountOf(std::uint16_t animHandle) {
			return BatteryAnims::Get().GetFrameCount(animHandle);
		}

		/** @brief Pool slot (the original's object index) */
		std::int32_t GetSlot() const {
			return _slot;
		}

		std::int32_t& I32(std::size_t offset) {
			return *reinterpret_cast<std::int32_t*>(&_record[offset]);
		}
		std::uint32_t& U32(std::size_t offset) {
			return *reinterpret_cast<std::uint32_t*>(&_record[offset]);
		}
		std::int16_t& I16(std::size_t offset) {
			return *reinterpret_cast<std::int16_t*>(&_record[offset]);
		}
		std::uint16_t& U16(std::size_t offset) {
			return *reinterpret_cast<std::uint16_t*>(&_record[offset]);
		}
		std::int8_t& S8(std::size_t offset) {
			return *reinterpret_cast<std::int8_t*>(&_record[offset]);
		}
		std::uint8_t& U8(std::size_t offset) {
			return _record[offset];
		}

		/** @brief Collision / pickup class, the low nibble of `+0x40` */
		std::int32_t GetClass() const {
			return (_record[0x40] & 0x0f);
		}

		/**
		 * @brief `FUN_0040e080(x, y, layer, direction, p5, p6, frame)` - queue a sprite for this frame
		 *
		 * As there, only the last tick of a rendered frame draws (`DAT_004d8388 == 0`), and a frame handle of 0
		 * draws nothing. Positions are 16.16 world coordinates.
		 */
		void DrawSprite(std::int32_t x, std::int32_t y, std::int32_t layer, std::int32_t direction, std::int32_t p5,
			std::int32_t p6, std::uint32_t frameHandle);
		/**
		 * @brief `FUN_0040dec0(slot)` - the current frame (`+0x28`) at the object's position, layer 3, facing `+0x45`
		 *
		 * Mode 5 with colour 0xf while `+0x46` is set, else mode 1 for a translucent frame, else mode 0.
		 */
		void DrawSelf();

		/** @brief The tile column / row of the object's position, rounded toward zero (`(X + bias) >> 21`) */
		std::int32_t CellX() {
			return (I32(0x0c) + ((I32(0x0c) >> 31) & 0x1fffff)) >> 21;
		}
		std::int32_t CellY() {
			return (I32(0x10) + ((I32(0x10) >> 31) & 0x1fffff)) >> 21;
		}

		/** @brief `FUN_00418ea0(x, y, id, volume, freq)` - a one-shot sample; `x == 0` is non-positional */
		void PlaySample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq);
		/**
		 * @brief `FUN_00418f90(x, y, id, volume, freq, handle)` - the object's sustained sample, kept while requested
		 *
		 * As the player's: a channel re-requested every tick keeps playing (and follows volume and pan), and stops
		 * after the first tick without a request.
		 */
		void HoldSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq);
		/** @brief Stops the sustained sample if it was not requested this tick */
		void ReleaseHeldSample();

	protected:
		BatteryWorld* _world;
		std::int32_t _slot;
		std::uint8_t _record[RecordSize];

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		bool OnTileDeactivated() override;

	private:
		struct SpriteRequest {
			std::int32_t X, Y;
			std::int32_t Layer, Direction, P5, P6;
			std::uint32_t Frame;
			std::uint16_t RenderLayer, VisitOrder;
			std::unique_ptr<RenderCommand> Command;
		};

		SmallVector<SpriteRequest, 1> _sprites;
		std::int32_t _spriteCount;

		std::shared_ptr<AudioBufferPlayer> _heldSound;
		std::int32_t _heldSoundId;
		bool _heldSoundRequested;

		std::shared_ptr<AudioBufferPlayer> StartSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume,
			std::int32_t freq, bool looping);

		/** @brief Clears the sprites of the previous rendered frame (`FUN_0040e3d0`, last tick only) */
		void BeginSprites();
	};
}
