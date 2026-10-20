#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "../LevelInitialization.h"
#include "Canvas.h"
#include "../Rendering/UpscaleRenderPass.h"

#include "../../nCine/Input/Keys.h"

#include <memory>

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::UI
{
	/**
		@brief One full-screen picture of BatteryCheck's `Data.j2d` (teardown/44 §1.2)

		As the original reads it: a 12-byte header (640, 480, 8), the palette (R, G, B, flag × 256) and the 8-bit
		pixels. The converter stores them as `Animations/BatteryCheck/Pictures/<name>.pic`.
	*/
	class BatteryPicture
	{
	public:
		static constexpr std::int32_t Width = 640;
		static constexpr std::int32_t Height = 480;

		/** @brief Loads `Picture.<name>` (case-insensitive, as the original's lookup) */
		bool Load(StringView name);
		bool IsValid() const {
			return !Pixels.empty();
		}

		std::uint8_t Palette[256 * 4] = {};
		SmallVector<std::uint8_t, 0> Pixels;
	};

	/**
		@brief The original's 640×480 8-bit screen in the front end

		The pixels, the palette (`DAT_004d7900`) and the fade step `k` of `FUN_0040c200` (the hardware palette is
		`rgb · k >> 5`), shown through one texture that is uploaded again only when something changed.
	*/
	class BatteryScreen
	{
	public:
		static constexpr std::int32_t Width = BatteryPicture::Width;
		static constexpr std::int32_t Height = BatteryPicture::Height;

		BatteryScreen();

		/** @brief Copies a whole picture (`FUN_0040c430`'s background blit) */
		void Blit(const BatteryPicture& picture);
		/** @brief Takes a picture's palette (not applied yet: that is the fade's job) */
		void SetPalette(const std::uint8_t* palette);
		/** @brief `FUN_0040e390` - all black */
		void Clear();
		void SetFade(std::int32_t k);
		std::int32_t GetFade() const {
			return _fade;
		}
		void Invalidate() {
			_dirty = true;
		}

		std::uint8_t* GetPixels() {
			return _pixels.data();
		}
		/** @brief The texture with the current pixels through the faded palette */
		Texture* Present();

	private:
		SmallVector<std::uint8_t, 0> _pixels;
		std::uint8_t _palette[256 * 4];
		std::int32_t _fade;
		bool _dirty;
		SmallVector<std::uint8_t, 0> _rgba;
		std::unique_ptr<Texture> _texture;
	};

	/** @brief A canvas that shows a @ref BatteryScreen over the whole 640×480 view */
	class BatteryScreenCanvas : public Canvas
	{
	public:
		BatteryScreenCanvas(BatteryScreen* screen, std::uint16_t layer) : _screen(screen), _layer(layer) {}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		BatteryScreen* _screen;
		std::uint16_t _layer;
	};

	/**
		@brief `FUN_0040c4f0` - the menu, main (`param_1 = 1`) or in-game (0)

		The same full-screen picture for both: `Picture.basisscherm` with the `Picture.texten` palette, its item
		strips and the lamps from `Picture.texten` (`FUN_0040c2f0`). One key-down event is handled per loop
		iteration, i.e. per frame.
	*/
	class BatteryMenu
	{
	public:
		/** @brief What the player chose; the caller fades out and carries it out */
		enum class Action {
			None,
			NewGame,		/**< Main: NIEUW SPEL */
			LoadGame,		/**< Main / in-game: SPEL LADEN, a savegame exists */
			Quit,			/**< Main: STOPPEN, credits and exit; in-game: SPEL STOPPEN, back to the main menu */
			SaveGame,		/**< In-game: SPEL BEWAREN */
			Resume			/**< In-game: ANNULEREN, or Esc on it */
		};

		explicit BatteryMenu(bool mainMenu);

		bool IsValid() const {
			return _texten.IsValid() && _basis.IsValid();
		}
		const std::uint8_t* GetPalette() const {
			return _texten.Palette;
		}

		void OnKeyPressed(Keys key);
		/** @brief One iteration: handles one queued key; returns the action when one was chosen */
		Action Update();
		/** @brief `FUN_0040c430` - the background and the four items, the selected lamp blinking on `clockMs` */
		void Compose(BatteryScreen& screen, std::uint32_t clockMs) const;

		/** @brief Whether `SAVEGAME.000` exists (`FUN_0042af10`) */
		static bool HasSaveGame();

	private:
		bool _main;
		std::int32_t _selected;
		BatteryPicture _texten;
		BatteryPicture _basis;
		SmallVector<Keys, 0> _keys;

		void ComposeItem(BatteryScreen& screen, std::int32_t item, std::uint32_t clockMs) const;
	};

	/** @brief `FUN_0040c200(dir)` - 33 palette steps, one per tick of the real-time 70 Hz clock */
	class BatteryFade
	{
	public:
		void Start(std::int32_t direction, BatteryScreen& screen);
		bool IsActive() const {
			return _direction != 0;
		}
		/** @brief Advances by the 70 Hz ticks since the last call; returns true once the fade is over */
		bool Update(BatteryScreen& screen, float timeMult);

	private:
		std::int32_t _direction = 0;
		float _ticks = 0.0f;
	};

	/** @brief A non-positional sample of the game's set (`FUN_00425da0`), by its global id */
	void PlayBatterySample(std::int32_t id);

	/**
		@brief BatteryCheck's front end - the main menu (`FUN_0040c4f0(1)`) and the credits (`FUN_0040c840`)

		There is no intro: the program starts here. NIEUW SPEL starts a new game at `binnen`; STOPPEN shows the
		credits and exits (in a browser it comes back to this menu, a page can't close itself).
	*/
	class BatteryFrontEnd : public IStateHandler
	{
	public:
		explicit BatteryFrontEnd(IRootController* root);
		~BatteryFrontEnd() override;

		Vector2i GetViewSize() const override;
		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;

	private:
		enum class State {
			MenuFadeIn, Menu, MenuFadeOut, CreditsFadeIn, Credits, CreditsFadeOut, Done
		};

		IRootController* _root;
		State _state;
		BatteryMenu::Action _action;
		BatteryMenu _menu;
		BatteryPicture _credits;
		BatteryScreen _screen;
		BatteryFade _fade;
		std::uint32_t _clockMs;
		float _clockAccumulator;
		std::uint32_t _creditsStart;
		bool _typed;
		Rendering::UpscaleRenderPass _upscalePass;
		std::unique_ptr<BatteryScreenCanvas> _canvas;

		void Finish();
	};

	/**
		@brief `FUN_0041b010` - the loading picture of a level, shown at once and left on screen while it loads

		`Picture.Level1b` for a name with "BINNEN", `Level2b` with "RAIN", `Level3b` otherwise. After one frame on
		screen the level is loaded (the original keeps the picture up during the load, then clears to black).
	*/
	class BatteryLoadingScreen : public IStateHandler
	{
	public:
		BatteryLoadingScreen(IRootController* root, LevelInitialization&& levelInit);
		~BatteryLoadingScreen() override;

		Vector2i GetViewSize() const override;
		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

	private:
		IRootController* _root;
		LevelInitialization _levelInit;
		BatteryScreen _screen;
		std::int32_t _framesShown;
		Rendering::UpscaleRenderPass _upscalePass;
		std::unique_ptr<BatteryScreenCanvas> _canvas;
	};

	/**
		@brief The in-game menu (`FUN_0040c4f0(0)`) over a level

		The same full-screen menu: it fades in from black, and closing it fades out and cuts back to the level at
		full brightness. Its sounds come from the game's own set (move 0x25, select 0x29), not the MENU set.
	*/
	class BatteryInGameMenu
	{
	public:
		BatteryInGameMenu(LevelHandler* levelHandler, SceneNode* parent);
		~BatteryInGameMenu();

		void OnKeyPressed(Keys key);
		/** @brief One frame; returns the chosen action once its fade-out is over */
		BatteryMenu::Action Update(float timeMult);

	private:
		enum class State {
			FadeIn, Menu, FadeOut, Done
		};

		LevelHandler* _levelHandler;
		State _state;
		BatteryMenu::Action _action;
		BatteryMenu _menu;
		BatteryScreen _screen;
		BatteryFade _fade;
		std::uint32_t _clockMs;
		float _clockAccumulator;
		std::unique_ptr<BatteryScreenCanvas> _canvas;
	};
}
