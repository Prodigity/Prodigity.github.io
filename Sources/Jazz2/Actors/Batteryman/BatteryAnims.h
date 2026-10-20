#pragma once

#include <cstdint>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Actors::Batteryman
{
	/** @brief Number of animation sets in BatteryCheck's Anims.j2a (its roster, in alphabetical order) */
	constexpr std::int32_t AnimSetCount = 27;

	/** @brief BatteryCheck's animation sets - the set index is the alphabetical roster index (BATTERY-ENGINE-TEARDOWN.md §1) */
	enum class AnimSet : std::uint8_t {
		Baas, BaasOgen, Band, Batterij, BigSucker, Bord, DestScen, Drijver, Druppel, Font, Freddy, KleinePomp, Lift, Man,
		Menu, Paraplu, Pickups, Platform, Pomp, Poort, Snd, Stamper, Sucker, Vonk, Water, Zuiger, ZwareStamper
	};

	/** @brief One frame as the original's frame table holds it (`DAT_00481f80`, stride 0x18) */
	struct FrameInfo {
		std::int16_t Width, Height;			// +0x00, +0x02
		std::int16_t HotspotX, HotspotY;	// +0x08, +0x0a
		std::int16_t ColdspotY;				// +0x06
		bool Translucent;					// bit 15 of the image's width word (+0x10 → +1 & 0x80)
		SmallVector<std::uint8_t, 0> Mask;	// +0x14: 1 bit per pixel, row after row; bit k = bit k%8 of byte k/8
		SmallVector<std::uint8_t, 0> Pixels;	// +0x10 decoded: the palette index of each pixel, 0 = transparent
	};

	/**
		@brief The original's animation and frame handles

		The original addresses an animation by a global index (set base + animation, the object's `+0x34`) and a
		frame by a global frame-table index (`+0x28`). Its code only ever works with them relative to a set's base,
		so here they encode the set, animation and frame directly; 0 is "no frame", as there.
	*/
	constexpr std::uint16_t MakeAnimHandle(std::int32_t set, std::int32_t anim) {
		return (std::uint16_t)((set << 8) | anim);
	}
	constexpr std::uint32_t MakeFrameHandle(std::int32_t set, std::int32_t anim, std::int32_t frame) {
		return 1u + (((std::uint32_t)set << 16) | ((std::uint32_t)anim << 8) | (std::uint32_t)frame);
	}

	/** @brief Animation data of all sets, loaded on first use from the converted `Animations/BatteryCheck/*.mask` */
	class BatteryAnims
	{
	public:
		static BatteryAnims& Get();

		/** @brief Lowercase roster name of a set, as the converted file names use it */
		static StringView GetSetName(std::int32_t set);
		/** @brief Number of animations in a set */
		static std::int32_t GetAnimCount(std::int32_t set);

		/** @brief Number of frames of an animation (`*(ushort*)(DAT_004a77c0 + anim * 8)`), 0 if it doesn't exist */
		std::int32_t GetFrameCount(std::int32_t set, std::int32_t anim);
		std::int32_t GetFrameCount(std::uint16_t animHandle) {
			return GetFrameCount(animHandle >> 8, animHandle & 0xff);
		}
		/** @brief A frame, or `nullptr` */
		const FrameInfo* GetFrame(std::int32_t set, std::int32_t anim, std::int32_t frame);
		/** @brief The frame of a frame handle, or `nullptr` for 0 / unknown */
		const FrameInfo* GetFrame(std::uint32_t frameHandle);

		/**
		 * @brief `FUN_00407cc0` - pixels where both sprites' collision masks are set, counted up to 8
		 *
		 * Each sprite is its frame at a position in whole pixels, mirrored when its facing is negative. The original
		 * stops counting at 8 and every caller tests `> 7`, so the result is 0..8.
		 */
		std::int32_t CountMaskOverlap(std::uint32_t frameA, std::int32_t xA, std::int32_t yA, std::int8_t facingA,
			std::uint32_t frameB, std::int32_t xB, std::int32_t yB, std::int8_t facingB);

	private:
		struct AnimFrames {
			bool Loaded = false;
			SmallVector<FrameInfo, 0> Frames;
		};

		BatteryAnims();

		AnimFrames& Load(std::int32_t set, std::int32_t anim);

		SmallVector<SmallVector<AnimFrames, 0>, 0> _sets;
	};
}
