#include "BatteryAnims.h"
#include "../../ContentResolver.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		constexpr StringView SetNames[AnimSetCount] = {
			"baas"_s, "baasogen"_s, "band"_s, "batterij"_s, "bigsucker"_s, "bord"_s, "destscen"_s, "drijver"_s, "druppel"_s,
			"font"_s, "freddy"_s, "kleinepomp"_s, "lift"_s, "man"_s, "menu"_s, "paraplu"_s, "pickups"_s, "platform"_s,
			"pomp"_s, "poort"_s, "snd"_s, "stamper"_s, "sucker"_s, "vonk"_s, "water"_s, "zuiger"_s, "zwarestamper"_s
		};

		// Animations per set, as in the shipped Anims.j2a
		constexpr std::uint8_t AnimCounts[AnimSetCount] = {
			20, 3, 3, 2, 8, 12, 5, 1, 2, 2, 2, 1, 2, 75, 0, 1, 3, 1, 1, 1, 0, 1, 7, 1, 1, 2, 1
		};

		bool MaskBit(const FrameInfo& f, std::uint32_t bit)
		{
			// The original reads the mask as 32-bit words, bit (k & 31) of word k >> 5 - the same bit as here. The
			// last row of a mirrored sprite reads one bit past its mask (see CountMaskOverlap), which is taken as 0
			std::uint32_t byte = (bit >> 3);
			if (byte >= f.Mask.size()) {
				return false;
			}
			return ((f.Mask[byte] >> (bit & 7)) & 1) != 0;
		}
	}

	BatteryAnims& BatteryAnims::Get()
	{
		static BatteryAnims instance;
		return instance;
	}

	BatteryAnims::BatteryAnims()
	{
		_sets.resize(AnimSetCount);
		for (std::int32_t i = 0; i < AnimSetCount; i++) {
			_sets[i].resize(AnimCounts[i]);
		}
	}

	StringView BatteryAnims::GetSetName(std::int32_t set)
	{
		return (set >= 0 && set < AnimSetCount ? SetNames[set] : StringView{});
	}

	std::int32_t BatteryAnims::GetAnimCount(std::int32_t set)
	{
		return (set >= 0 && set < AnimSetCount ? AnimCounts[set] : 0);
	}

	BatteryAnims::AnimFrames& BatteryAnims::Load(std::int32_t set, std::int32_t anim)
	{
		AnimFrames& a = _sets[set][anim];
		if (a.Loaded) {
			return a;
		}
		a.Loaded = true;

		char number[4];
		number[0] = (char)('0' + (anim / 10) % 10);
		number[1] = (char)('0' + anim % 10);
		number[2] = '\0';
		String path = fs::CombinePath({ "Animations"_s, "BatteryCheck"_s, String(SetNames[set] + "_"_s + StringView(number) + ".mask"_s) });
		auto s = ContentResolver::Get().OpenContentFile(path);
		if (s == nullptr || !s->IsValid()) {
			LOGW("Cannot open \"{}\"", path);
			return a;
		}
		if (s->ReadValueAsLE<std::uint32_t>() != 0x324D4342) {	// "BCM2"
			LOGW("\"{}\" has an invalid header (converted by an older version, clear the cache)", path);
			return a;
		}
		std::uint16_t frameCount = s->ReadValueAsLE<std::uint16_t>();
		a.Frames.resize(frameCount);
		for (std::uint16_t i = 0; i < frameCount; i++) {
			FrameInfo& f = a.Frames[i];
			f.Width = s->ReadValueAsLE<std::int16_t>();
			f.Height = s->ReadValueAsLE<std::int16_t>();
			f.HotspotX = s->ReadValueAsLE<std::int16_t>();
			f.HotspotY = s->ReadValueAsLE<std::int16_t>();
			f.ColdspotY = s->ReadValueAsLE<std::int16_t>();
			f.Translucent = (s->ReadValueAsLE<std::uint16_t>() & 1) != 0;
			std::uint32_t maskSize = s->ReadValueAsLE<std::uint32_t>();
			f.Mask.resize(maskSize);
			if (maskSize > 0) {
				s->Read(f.Mask.data(), maskSize);
			}
			std::uint32_t pixelCount = s->ReadValueAsLE<std::uint32_t>();
			f.Pixels.resize(pixelCount);
			if (pixelCount > 0) {
				s->Read(f.Pixels.data(), pixelCount);
			}
		}
		return a;
	}

	std::int32_t BatteryAnims::GetFrameCount(std::int32_t set, std::int32_t anim)
	{
		if (set < 0 || set >= AnimSetCount || anim < 0 || anim >= AnimCounts[set]) {
			return 0;
		}
		return (std::int32_t)Load(set, anim).Frames.size();
	}

	const FrameInfo* BatteryAnims::GetFrame(std::int32_t set, std::int32_t anim, std::int32_t frame)
	{
		if (set < 0 || set >= AnimSetCount || anim < 0 || anim >= AnimCounts[set]) {
			return nullptr;
		}
		auto& a = Load(set, anim);
		return (frame >= 0 && frame < (std::int32_t)a.Frames.size() ? &a.Frames[frame] : nullptr);
	}

	const FrameInfo* BatteryAnims::GetFrame(std::uint32_t frameHandle)
	{
		if (frameHandle == 0) {
			return nullptr;
		}
		std::uint32_t v = frameHandle - 1;
		return GetFrame((std::int32_t)(v >> 16), (std::int32_t)((v >> 8) & 0xff), (std::int32_t)(v & 0xff));
	}

	std::int32_t BatteryAnims::CountMaskOverlap(std::uint32_t frameA, std::int32_t xA, std::int32_t yA, std::int8_t facingA,
		std::uint32_t frameB, std::int32_t xB, std::int32_t yB, std::int8_t facingB)
	{
		// FUN_00407cc0, step by step. A mirrored sprite (facing < 0) spans [x - hotX - w, x - hotX), a plain one
		// [x + hotX, x + hotX + w); the horizontal tests compare unsigned, as there
		const FrameInfo* a = GetFrame(frameA);
		const FrameInfo* b = GetFrame(frameB);
		if (a == nullptr || b == nullptr) {
			return 0;
		}

		std::uint32_t wA = (std::uint16_t)a->Width, wB = (std::uint16_t)b->Width;
		std::int32_t topA = a->HotspotY + yA;
		std::int32_t topB = b->HotspotY + yB;

		std::uint32_t uA, leftA, uB, leftB;
		bool flipA = (facingA < 0), flipB = (facingB < 0);
		if (flipA) {
			uA = (std::uint32_t)(xA - a->HotspotX);
			leftA = uA - wA;
		} else {
			uA = (std::uint32_t)(xA + a->HotspotX);
			leftA = uA;
		}
		if (flipB) {
			uB = (std::uint32_t)(xB - b->HotspotX);
			leftB = uB - wB;
		} else {
			uB = (std::uint32_t)(xB + b->HotspotX);
			leftB = uB;
		}

		std::uint32_t rightA = leftA + wA;
		std::uint32_t rightB = leftB + wB;
		std::int32_t bottomA = (std::int32_t)(std::uint16_t)a->Height + topA;
		std::int32_t bottomB = (std::int32_t)(std::uint16_t)b->Height + topB;
		if (!(leftB < rightA && leftA < rightB && topB < bottomA && topA < bottomB)) {
			return 0;
		}

		std::uint32_t left = (leftA <= leftB ? leftB : leftA);
		std::uint32_t right = (rightB <= rightA ? rightB : rightA);
		std::int32_t top = (topB < topA ? topA : topB);
		std::int32_t bottom = (bottomB <= bottomA ? bottomB : bottomA);

		// First bit of each sprite's mask on the first overlapping row; a mirrored one is read right to left, which
		// starts one bit to the right of the plain mirror image (the original's own off-by-one, kept)
		std::uint32_t bitA = (flipA ? ((std::uint32_t)(top - topA) * wA - left) + uA : ((std::uint32_t)(top - topA) * wA - uA) + left);
		std::uint32_t bitB = (flipB ? ((std::uint32_t)(top - topB) * wB - left) + uB : ((std::uint32_t)(top - topB) * wB - uB) + left);

		std::int32_t count = 0;
		for (; top < bottom; top++) {
			std::uint32_t ia = bitA, ib = bitB;
			for (std::int32_t n = (std::int32_t)(right - left); n > 0; n--) {
				if (MaskBit(*a, ia) && MaskBit(*b, ib)) {
					count++;
					if (count > 7) {
						return count;
					}
				}
				ia = (flipA ? ia - 1 : ia + 1);
				ib = (flipB ? ib - 1 : ib + 1);
			}
			bitA += wA;
			bitB += wB;
		}
		return count;
	}
}
