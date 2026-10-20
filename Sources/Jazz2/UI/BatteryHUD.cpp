#include "BatteryHUD.h"
#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../Actors/Batteryman/BatterymanPlayer.h"
#include "../Actors/Batteryman/BatteryWorld.h"

#include "../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

using namespace Jazz2::Actors::Batteryman;

namespace Jazz2::UI
{
	BatteryHUD::BatteryHUD(LevelHandler* levelHandler)
		: HUD(levelHandler), _bcMetadata(nullptr), _ticks(0.0f)
	{
		_bcMetadata = ContentResolver::Get().RequestMetadata("UI/BatteryHUD"_s);
		if (_bcMetadata == nullptr) {
			LOGE("BatteryCheck HUD metadata \"UI/BatteryHUD\" could not be loaded");
		}
	}

	void BatteryHUD::OnUpdate(float timeMult)
	{
		HUD::OnUpdate(timeMult);

		// timeMult is relative to 60 Hz, the original's counter advances at 70 Hz
		_ticks += timeMult * (TicksPerSecond / 60.0f);
	}

	// The stock JJ2 elements (overview, score, weapon ammo, boss health, coin/gem notices) are not part of
	// BatteryCheck's HUD
	void BatteryHUD::OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player) {}
	void BatteryHUD::OnDrawScore(const Rectf& view, Actors::Player* player) {}
	void BatteryHUD::OnDrawWeaponAmmo(const Rectf& adjustedView, Actors::Player* player) {}
	void BatteryHUD::OnDrawActiveBoss(const Rectf& adjustedView) {}
	void BatteryHUD::OnDrawCoins(const Rectf& view, std::int32_t& charOffset) {}
	void BatteryHUD::OnDrawGems(const Rectf& view, std::int32_t& charOffset) {}

	void BatteryHUD::OnDrawHealth(const Rectf& view, const Rectf& adjustedView, Actors::Player* player)
	{
		if (_bcMetadata == nullptr) {
			return;
		}

		auto* batteryman = runtime_cast<BatterymanPlayer>(player);
		const float left = view.X, top = view.Y, w = view.W, h = view.H;
		// The icons run on the game tick (_DAT_004b11c0), so they stand still in a pause
		const std::int32_t tick = (batteryman != nullptr ? (std::int32_t)batteryman->GetTickCount() : (std::int32_t)_ticks);
		char buffer[32];

		// 1-2. Lives: PICKUPS anim 1 at (20, H-30), frame (tick >> 3) % count; "X %i" at (40, H-30), "X 0" below 1
		DrawSprite(PickupsState + 1, tick >> 3, left + 20.0f, top + h - 30.0f, MainLayer);
		std::int32_t lives = player->GetLives();
		buffer[0] = 'X'; buffer[1] = ' ';
		i32tos(lives >= 1 ? lives : 0, buffer + 2);
		DrawText(buffer, left + 40.0f, top + h - 30.0f);

		// 3-4. Superbatteries: PICKUPS anim 2, frame (tick >> 2) % count, at (W-100 + hotspotX, H-50) - the
		// original adds the frame's hotspot X to the position and the blit then adds it again, kept as is;
		// "X %i" at (W-60, H-30)
		if (auto* res = FindSprite(PickupsState + 2)) {
			std::int32_t frame = (tick >> 2) % std::max(res->Base->FrameCount, 1);
			DrawSprite(PickupsState + 2, frame, left + w - 100.0f + GetFrameHotspotX(res->Base, frame), top + h - 50.0f, MainLayer);
		}
		std::int32_t superbatteries = (batteryman != nullptr ? batteryman->GetSuperbatteries() : 0);
		i32tos(superbatteries >= 1 ? superbatteries : 0, buffer + 2);
		DrawText(buffer, left + w - 60.0f, top + h - 30.0f);

		// 5-10. FULL (batteries) and EMPTY (empty batteries) panels with their two digits, not in boss mode.
		// The tens and units digits share the panel's anchor; each digit frame's hotspot puts it in its slot
		if (batteryman == nullptr || !batteryman->IsBossMode()) {
			// Values of 100 or more would index past frame 9 in the original (no clamp); clamp instead
			std::int32_t batteries = std::clamp(batteryman != nullptr ? batteryman->GetBatteries() : 0, 0, 99);
			std::int32_t empties = std::clamp(batteryman != nullptr ? batteryman->GetEmpties() : 0, 0, 99);

			DrawSprite(BordState + 5, 0, left + 50.0f, top + 30.0f, MainLayer);
			DrawSprite(BordState + 6, batteries / 10, left + 50.0f, top + 30.0f, MainLayer + 1);
			DrawSprite(BordState + 7, batteries % 10, left + 50.0f, top + 30.0f, MainLayer + 1);

			DrawSprite(BordState + 0, 0, left + w - 50.0f, top + 30.0f, MainLayer);
			DrawSprite(BordState + 1, empties / 10, left + w - 50.0f, top + 30.0f, MainLayer + 1);
			DrawSprite(BordState + 2, empties % 10, left + w - 50.0f, top + 30.0f, MainLayer + 1);
		}

		// 11-12. Energy: BORD anim 3 frame at (W/2, 30); fill BORD anim 4 on top, right edge clipped at
		// left + width * energy / 7000 (drawn only while energy > 0)
		std::int32_t energy = (batteryman != nullptr ? batteryman->GetEnergy() : BatterymanPlayer::MaxEnergy);
		float centerX = left + std::floor(w * 0.5f);
		DrawSprite(BordState + 3, 0, centerX, top + 30.0f, MainLayer);
		if (energy > 0) {
			DrawSprite(BordState + 4, 0, centerX, top + 30.0f, MainLayer + 1, std::min(energy, BatterymanPlayer::MaxEnergy));
		}

		// The pause (FUN_00401860): FUN_0041abf0(0x8000, H / 2, clock70 * 8, 0x2000, "PAUZE", FONT/1) after the HUD
		if (batteryman != nullptr && batteryman->IsPaused()) {
			DrawWavyText("PAUZE", left, w, top + std::floor(h / 2.0f), batteryman->GetClock70() * 8, 0x2000);
		}
	}

	void BatteryHUD::DrawWavyText(const char* text, float left, float viewWidth, float y, std::uint32_t phase, std::int32_t div)
	{
		// FUN_0041abf0, centred: the pen advances by each glyph's width (16 for a space or a missing glyph), each
		// glyph sits sine[phase] / div px off the line, and the phase moves on by 8 per character code above 0x20
		auto* res = FindSprite(FontState + 1);
		if (res == nullptr) {
			return;
		}
		GenericGraphicResource* base = res->Base;
		auto glyphOf = [base](char c) {
			std::int32_t glyph = (std::uint8_t)c - 0x20;
			return (glyph < 1 || glyph >= base->FrameCount ? -1 : glyph);
		};
		std::int32_t width = 0;
		for (const char* c = text; *c != '\0'; c++) {
			std::int32_t glyph = glyphOf(*c);
			width += (glyph < 0 ? 0x10 : base->GetFrameRect(glyph).W);
		}
		std::int32_t x = (std::int32_t)viewWidth / 2 - width / 2;
		for (const char* c = text; *c != '\0'; c++) {
			std::int32_t glyph = glyphOf(*c);
			if (glyph < 0) {
				x += 0x10;
			} else {
				std::int32_t wave = Actors::Batteryman::BatteryWorld::Sine((std::int32_t)(phase & 0x3ff)) / div;
				DrawSprite(FontState + 1, glyph, left + (float)x, y - 10.0f + (float)wave, FontLayer);
				x += base->GetFrameRect(glyph).W;
			}
			phase += (std::uint32_t)(((std::int32_t)(std::uint8_t)*c - 0x20) * 8);
		}
	}

	GraphicResource* BatteryHUD::FindSprite(std::uint32_t state) const
	{
		auto* res = _bcMetadata->FindAnimation((AnimState)state);
		return (res != nullptr && res->Base != nullptr && res->Base->TextureDiffuse != nullptr && res->Base->FrameCount > 0 ? res : nullptr);
	}

	Vector2i BatteryHUD::GetOrigin(const GenericGraphicResource* base)
	{
		// The importer stores the origin with its 2 px frame border added, which cancels the border - except for
		// an animation whose frames all have a non-negative hotspot, which it stores as (0, 0) and so leaves the
		// border standing (teardown/90-port-side.md §2.1)
		if (base->Hotspot.X == 0 && base->Hotspot.Y == 0) {
			return Vector2i(GenericGraphicResource::FrameBorder, GenericGraphicResource::FrameBorder);
		}
		return base->Hotspot;
	}

	std::int32_t BatteryHUD::GetFrameHotspotX(const GenericGraphicResource* base, std::int32_t frame)
	{
		// On a packed sheet each frame records where it begins inside the cell. A frame trimmed of transparent
		// columns reports its first opaque column instead of its original left edge, which is the same for the
		// tightly cropped HUD and font frames
		return base->GetFrameOffset(frame).X - GetOrigin(base).X;
	}

	void BatteryHUD::DrawSprite(std::uint32_t state, std::int32_t frame, float x, float y, std::uint16_t z, std::int32_t clipEnergy)
	{
		auto* res = FindSprite(state);
		if (res == nullptr) {
			return;
		}

		GenericGraphicResource* base = res->Base;
		frame %= base->FrameCount;
		if (frame < 0) {
			frame += base->FrameCount;
		}

		Vector2i origin = GetOrigin(base);
		Recti frameRect = base->GetFrameRect(frame);
		Vector2i frameOffset = base->GetFrameOffset(frame);
		Vector2f pos = Vector2f(std::floor(x) - origin.X + frameOffset.X, std::floor(y) - origin.Y + frameOffset.Y);

		float visibleW = (float)frameRect.W;
		if (clipEnergy >= 0) {
			visibleW = (float)(frameRect.W * clipEnergy / BatterymanPlayer::MaxEnergy);
			if (visibleW <= 0.0f) {
				return;
			}
		}

		Vector2i texSize = base->TextureDiffuse->GetSize();
		Vector4f texCoords = Vector4f(
			visibleW / float(texSize.X),
			float(frameRect.X) / float(texSize.X),
			float(frameRect.H) / float(texSize.Y),
			float(frameRect.Y) / float(texSize.Y)
		);

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		DrawTexture(*base->TextureDiffuse.get(), pos, z, Vector2f(visibleW, (float)frameRect.H), texCoords, Colorf::White, false, 0.0f, paletteOffset);
	}

	void BatteryHUD::DrawText(const char* text, float x, float y)
	{
		// FUN_0041aa50: glyph = character - 0x20 of FONT anim 1, drawn at (x + hotspotX, y + hotspotY - 10);
		// the pen advances by hotspotX + width - 6 + spacing (spacing 1); a space or a missing glyph advances
		// by 7 + spacing
		constexpr std::int32_t Spacing = 1;
		auto* res = FindSprite(FontState + 1);
		if (res == nullptr) {
			return;
		}

		GenericGraphicResource* base = res->Base;
		for (const char* c = text; *c != '\0'; c++) {
			std::int32_t glyph = (std::uint8_t)*c - 0x20;
			if (*c == ' ' || glyph < 0 || glyph >= base->FrameCount) {
				x += 7 + Spacing;
				continue;
			}
			DrawSprite(FontState + 1, glyph, x, y - 10.0f, FontLayer);
			x += GetFrameHotspotX(base, glyph) + base->GetFrameRect(glyph).W - 6 + Spacing;
		}
	}
}
