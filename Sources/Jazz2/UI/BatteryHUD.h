#pragma once

#include "HUD.h"

namespace Jazz2::UI
{
	/**
		@brief BatteryCheck's in-game HUD

		A port of the original's HUD routine (`Battery.exe` `FUN_0040beb0`), as specified in
		`teardown/30-mainloop-camera-hud.md` §4: the energy bar (BORD frame with its fill clipped at
		width · energy / 7000), the FULL and EMPTY battery panels with two-digit counters (hidden during the boss
		fight), and the animated lives and superbattery icons with their `"X %i"` counts. Positions are relative to
		the view's width and height exactly as in the original, which laid them out on its 640×480 screen.

		Every sprite is placed the way the original blits it - with its top-left at (x + hotspotX, y + hotspotY) -
		which @ref HUD::DrawElement (aligned by cell) does not do, so drawing goes through @ref DrawSprite(). The
		stock HUD's own elements are suppressed by overriding their hooks.
	*/
	class BatteryHUD : public HUD
	{
	public:
		/** @brief Creates a new instance */
		BatteryHUD(LevelHandler* levelHandler);

		void OnUpdate(float timeMult) override;

	protected:
		void OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player) override;
		void OnDrawHealth(const Rectf& view, const Rectf& adjustedView, Actors::Player* player) override;
		void OnDrawScore(const Rectf& view, Actors::Player* player) override;
		void OnDrawWeaponAmmo(const Rectf& adjustedView, Actors::Player* player) override;
		void OnDrawActiveBoss(const Rectf& adjustedView) override;
		void OnDrawCoins(const Rectf& view, std::int32_t& charOffset) override;
		void OnDrawGems(const Rectf& view, std::int32_t& charOffset) override;

	private:
		/** @{ @name Metadata states (`UI/BatteryHUD.res`) - base + original animation index */
		/** @brief Anims.j2a set 5, BORD */
		static constexpr std::uint32_t BordState = 1000;
		/** @brief Anims.j2a set 9, FONT */
		static constexpr std::uint32_t FontState = 1100;
		/** @brief Anims.j2a set 16, PICKUPS */
		static constexpr std::uint32_t PickupsState = 1200;
		/** @} */

		/** @brief Rate of the original's global tick counter (`DAT_004b11c0`) that animates the icons */
		static constexpr float TicksPerSecond = 70.0f;

		Metadata* _bcMetadata;
		float _ticks;

		GraphicResource* FindSprite(std::uint32_t state) const;
		/** @brief Returns where the original sprite's origin lies inside the converted frame cell */
		static Vector2i GetOrigin(const GenericGraphicResource* base);
		/** @brief Returns the original frame's hotspot X - the signed offset from its origin to its left edge */
		static std::int32_t GetFrameHotspotX(const GenericGraphicResource* base, std::int32_t frame);
		/**
		 * @brief Draws a frame with its top-left at (x + hotspotX, y + hotspotY), as the original blitter does
		 *
		 * With `clipEnergy` set, only the left `width * clipEnergy / 7000` columns are drawn (the energy fill).
		 */
		void DrawSprite(std::uint32_t state, std::int32_t frame, float x, float y, std::uint16_t z, std::int32_t clipEnergy = -1);
		/** @brief Draws text with the proportional FONT anim 1 (`FUN_0041aa50`) */
		void DrawText(const char* text, float x, float y);
		/** @brief Draws centred wavy text with FONT anim 1 (`FUN_0041abf0`, the PAUZE overlay) */
		void DrawWavyText(const char* text, float left, float viewWidth, float y, std::uint32_t phase, std::int32_t div);
	};
}
