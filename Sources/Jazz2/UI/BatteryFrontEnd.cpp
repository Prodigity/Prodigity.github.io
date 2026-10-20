#include "BatteryFrontEnd.h"
#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"
#include "../Actors/Batteryman/BatterymanPlayer.h"
#include "../Actors/Batteryman/BatteryWorld.h"

#include "../../nCine/Application.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <cstdlib>
#include <cstring>

using namespace Death::IO;

// Ports of BatteryCheck's front end (teardown/44 §1): FUN_0040c4f0 (menu), FUN_0040c2f0 / FUN_0040c430 (its
// drawing), FUN_0040c200 (fade), FUN_0040c840 (credits) and FUN_0041b010 (loading picture)

namespace Jazz2::UI
{
	namespace
	{
		constexpr std::int32_t ScreenWidth = BatteryScreen::Width;
		constexpr std::int32_t ScreenHeight = BatteryScreen::Height;

		/** @brief Global sample ids (Battery.exe's table at 0x4498a8) */
		constexpr std::int32_t SampleMenuBegin = 0x0f;		// DAT_004af43c
		constexpr std::int32_t SampleMenuEnd = 0x10;		// DAT_004af440
		constexpr std::int32_t SampleMenuMove = 0x11;		// DAT_004af444
		constexpr std::int32_t SampleMenuSelect = 0x12;		// DAT_004af448
		constexpr std::int32_t SampleInGameMove = 0x25;		// DAT_004af494, sSND_BMSTEP02
		constexpr std::int32_t SampleInGameSelect = 0x29;	// DAT_004af4a4, sSND_E001

		/** @brief `FUN_0041fcd0` runs on milliseconds, the fades on the 70 Hz clock; frames come at 60 Hz units */
		constexpr float MsPerFrameUnit = 1000.0f / 60.0f;

		/** @brief The last frame's real duration in 60 Hz units - the original's clocks are real time, while the engine's
			timeMult is capped at 2.5 (slow motion below 24 fps); at most half a second, as after a load */
		float RealTimeMult()
		{
			return std::min(theApplication().GetFrameTimer().GetLastFrameDuration() * 60.0f, 30.0f);
		}

		void FitView(Rendering::UpscaleRenderPass& pass, std::int32_t width, std::int32_t height)
		{
			// The original's 640×480 screen, the largest 4:3 rectangle that fits, centred - as the level's view
			float fitScale = std::min((float)width / ScreenWidth, (float)height / ScreenHeight);
			std::int32_t fitWidth = std::max((std::int32_t)roundf(ScreenWidth * fitScale), 1);
			std::int32_t fitHeight = std::max((std::int32_t)roundf(ScreenHeight * fitScale), 1);
			pass.SetSmoothFractionalScale(true);
			pass.Initialize(ScreenWidth, ScreenHeight, fitWidth, fitHeight);
			pass.SetOutputOffset(Vector2f((float)((width - fitWidth) / 2), (float)((height - fitHeight) / 2)));
		}

		SmallVector<std::shared_ptr<AudioBufferPlayer>, 0>& PlayingSamples()
		{
			static SmallVector<std::shared_ptr<AudioBufferPlayer>, 0> players;
			return players;
		}
	}

	// ---- Data.j2d pictures -----------------------------------------------------------------------------------

	bool BatteryPicture::Load(StringView name)
	{
		String path = fs::CombinePath({ "Animations"_s, "BatteryCheck"_s, "Pictures"_s, String("picture."_s + name + ".pic"_s) });
		StringUtils::lowercaseInPlace(path);
		auto s = ContentResolver::Get().OpenContentFile(path);
		if (s == nullptr || !s->IsValid()) {
			LOGE("Cannot open \"{}\"", path);
			return false;
		}
		std::uint32_t width = s->ReadValueAsLE<std::uint32_t>();
		std::uint32_t height = s->ReadValueAsLE<std::uint32_t>();
		/*std::uint32_t bits =*/ s->ReadValueAsLE<std::uint32_t>();
		if (width != (std::uint32_t)Width || height != (std::uint32_t)Height) {
			LOGE("\"{}\" is {}x{}, not 640x480", path, width, height);
			return false;
		}
		s->Read(Palette, sizeof(Palette));
		Pixels.resize(Width * Height);
		s->Read(Pixels.data(), (std::int64_t)Pixels.size());
		return true;
	}

	// ---- The 8-bit screen ------------------------------------------------------------------------------------

	BatteryScreen::BatteryScreen()
		: _fade(0), _dirty(true)
	{
		_pixels.resize(Width * Height);
		std::memset(_pixels.data(), 0, _pixels.size());
		std::memset(_palette, 0, sizeof(_palette));
	}

	void BatteryScreen::Blit(const BatteryPicture& picture)
	{
		if (picture.IsValid()) {
			std::memcpy(_pixels.data(), picture.Pixels.data(), _pixels.size());
		}
		_dirty = true;
	}

	void BatteryScreen::SetPalette(const std::uint8_t* palette)
	{
		std::memcpy(_palette, palette, sizeof(_palette));
		_dirty = true;
	}

	void BatteryScreen::Clear()
	{
		// FUN_0040e390 clears to colour DAT_004c5434 (0), and the palette is black too
		std::memset(_pixels.data(), 0, _pixels.size());
		std::memset(_palette, 0, sizeof(_palette));
		_dirty = true;
	}

	void BatteryScreen::SetFade(std::int32_t k)
	{
		if (_fade != k) {
			_fade = k;
			_dirty = true;
		}
	}

	Texture* BatteryScreen::Present()
	{
		if (_texture == nullptr) {
			_texture = std::make_unique<Texture>("BatteryScreen", Texture::Format::RGBA8, Width, Height);
			_texture->SetMinFiltering(SamplerFilter::Nearest);
			_texture->SetMagFiltering(SamplerFilter::Nearest);
			_texture->SetWrap(SamplerWrapping::ClampToEdge);
			_rgba.resize(Width * Height * 4);
			_dirty = true;
		}
		if (_dirty) {
			_dirty = false;
			// The hardware palette of FUN_0040c200: (c · k + bias) >> 5 per channel
			std::uint8_t faded[256 * 3];
			for (std::int32_t i = 0; i < 256; i++) {
				for (std::int32_t c = 0; c < 3; c++) {
					faded[i * 3 + c] = (std::uint8_t)((_palette[i * 4 + c] * _fade) >> 5);
				}
			}
			const std::uint8_t* src = _pixels.data();
			std::uint8_t* dst = _rgba.data();
			for (std::int32_t i = 0; i < Width * Height; i++) {
				const std::uint8_t* rgb = &faded[src[i] * 3];
				dst[i * 4 + 0] = rgb[0];
				dst[i * 4 + 1] = rgb[1];
				dst[i * 4 + 2] = rgb[2];
				dst[i * 4 + 3] = 255;
			}
			_texture->LoadFromTexels(_rgba.data(), 0, 0, Width, Height);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			// Development aid: BATTERYCHECK_DUMP_SCREEN=<path> writes every fully faded-in screen as raw RGBA
			if (_fade == 32) {
				if (const char* dump = std::getenv("BATTERYCHECK_DUMP_SCREEN")) {
					auto so = fs::Open(dump, FileAccess::Write);
					if (so->IsValid()) {
						so->Write(_rgba.data(), (std::int64_t)_rgba.size());
					}
				}
			}
#endif
		}
		return _texture.get();
	}

	bool BatteryScreenCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = Vector2i(BatteryScreen::Width, BatteryScreen::Height);
		Texture* texture = _screen->Present();
		if (texture != nullptr) {
			DrawTexture(*texture, Vector2f::Zero, _layer, Vector2f((float)BatteryScreen::Width, (float)BatteryScreen::Height),
				Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White);
		}
		return true;
	}

	// ---- Sounds ----------------------------------------------------------------------------------------------

	void PlayBatterySample(std::int32_t id)
	{
#if defined(WITH_AUDIO)
		auto& players = PlayingSamples();
		for (std::size_t i = 0; i < players.size();) {
			if (players[i]->isStopped()) {
				players.eraseUnordered(i);
			} else {
				i++;
			}
		}
		auto* metadata = ContentResolver::Get().RequestMetadata("Interactive/BatteryObjects"_s);
		StringView name = Actors::Batteryman::BatteryWorld::GetSampleName(id);
		if (metadata == nullptr || name.empty()) {
			return;
		}
		auto it = metadata->Sounds.find(String::nullTerminatedView(name));
		if (it == metadata->Sounds.end() || !ContentResolver::Get().ResolveSound(it->second) || it->second.Buffers.empty()) {
			LOGW("Sample {} (\"{}\") not found", id, name);
			return;
		}
		auto& player = players.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[0]->Buffer));
		player->setPosition(Vector3f(0.0f, 0.0f, 0.0f));
		player->setGain(PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
		player->setSourceRelative(true);
		player->play();
		LOGI("[FrontEnd] sample {}", name);
#endif
	}

	// ---- The menu --------------------------------------------------------------------------------------------

	BatteryMenu::BatteryMenu(bool mainMenu)
		: _main(mainMenu), _selected(0)
	{
		_texten.Load("texten"_s);
		_basis.Load("basisscherm"_s);
	}

	bool BatteryMenu::HasSaveGame()
	{
		return fs::IsReadableFile(fs::CombinePath(PreferencesCache::GetDirectory(), "SAVEGAME.000"_s));
	}

	void BatteryMenu::OnKeyPressed(Keys key)
	{
		if (_keys.size() < 16) {
			_keys.push_back(key);
		}
	}

	BatteryMenu::Action BatteryMenu::Update()
	{
		// FUN_0040c4f0's loop: one key-down event per iteration
		if (_keys.empty()) {
			return Action::None;
		}
		Keys key = _keys.front();
		_keys.erase(_keys.begin());

		std::int32_t last = (_main ? 2 : 3);
		switch (key) {
			case Keys::Up:
				// Clamped at the top, and the sound plays anyway
				_selected = std::max(_selected - 1, 0);
				PlayBatterySample(_main ? SampleMenuMove : SampleInGameMove);
				break;
			case Keys::Down:
				_selected = std::min(_selected + 1, last);
				PlayBatterySample(_main ? SampleMenuMove : SampleInGameMove);
				break;
			case Keys::Escape:
				// To the last item first (silently); on it, it is chosen
				if (_selected != last) {
					_selected = last;
				} else {
					return (_main ? Action::Quit : Action::Resume);
				}
				break;
			case Keys::Return:
			case Keys::NumPadEnter:
				PlayBatterySample(_main ? SampleMenuSelect : SampleInGameSelect);
				if (_main) {
					switch (_selected) {
						case 0: return Action::NewGame;
						case 1: return (HasSaveGame() ? Action::LoadGame : Action::None);
						default: return Action::Quit;
					}
				} else {
					switch (_selected) {
						case 0: return Action::SaveGame;
						case 1: return (HasSaveGame() ? Action::LoadGame : Action::None);
						case 2: return Action::Quit;
						default: return Action::Resume;
					}
				}
			default:
				break;
		}
		return Action::None;
	}

	void BatteryMenu::Compose(BatteryScreen& screen, std::uint32_t clockMs) const
	{
		// FUN_0040c430: the background 1:1, then the four items
		screen.Blit(_basis);
		for (std::int32_t item = 0; item < 4; item++) {
			ComposeItem(screen, item, clockMs);
		}
	}

	void BatteryMenu::ComposeItem(BatteryScreen& screen, std::int32_t item, std::uint32_t clockMs) const
	{
		// FUN_0040c2f0(menu, item, selected): strip (item, +4 in the main menu) of Picture.texten, 46 rows of 47;
		// its text, texten x 0..150, at screen x 399; its lamp, texten x 151..198, at x 550, from row 46 (orange)
		// or, when selected, row 0 / 93 alternating every 128 ms
		if (!_texten.IsValid()) {
			return;
		}
		std::int32_t y = (item == 0 ? 0xa9 : (item == 1 ? 0xe1 : (item == 2 ? 0x119 : 0x150)));
		std::int32_t strip = (_main ? item + 4 : item);
		const std::uint8_t* texten = _texten.Pixels.data();
		std::uint8_t* dst = screen.GetPixels();
		if (_main && item == 3) {
			// The main menu's 4th slot: 202 px of the dark bar (strip 7), no lamp
			for (std::int32_t r = 0; r < 0x2e; r++) {
				std::memcpy(&dst[(y + r) * ScreenWidth + 399], &texten[(strip * 47 + r) * ScreenWidth], 202);
			}
			return;
		}
		std::int32_t lampRow = (_selected == item ? (std::int32_t)((clockMs >> 7) & 1) * 0x5d : 0x2e);
		for (std::int32_t r = 0; r < 0x2e; r++) {
			std::memcpy(&dst[(y + r) * ScreenWidth + 399], &texten[(strip * 47 + r) * ScreenWidth], 151);
			std::memcpy(&dst[(y + r) * ScreenWidth + 550], &texten[(lampRow + r) * ScreenWidth + 151], 48);
		}
		screen.Invalidate();
	}

	// ---- Fade ------------------------------------------------------------------------------------------------

	void BatteryFade::Start(std::int32_t direction, BatteryScreen& screen)
	{
		// k from 0 to 32 (in) or 32 to 0 (out), each step on screen for one tick of the 70 Hz clock
		_direction = direction;
		_ticks = 0.0f;
		screen.SetFade(direction > 0 ? 0 : 32);
	}

	bool BatteryFade::Update(BatteryScreen& screen, float timeMult)
	{
		if (_direction == 0) {
			return true;
		}
		_ticks += timeMult * (70.0f / 60.0f);
		std::int32_t k = screen.GetFade();
		while (_ticks >= 1.0f) {
			_ticks -= 1.0f;
			k += _direction;
			if (k > 32 || k < 0) {
				screen.SetFade(_direction > 0 ? 32 : 0);
				_direction = 0;
				return true;
			}
		}
		screen.SetFade(k);
		return false;
	}

	// ---- Main menu and credits -------------------------------------------------------------------------------

	BatteryFrontEnd::BatteryFrontEnd(IRootController* root)
		: _root(root), _state(State::MenuFadeIn), _action(BatteryMenu::Action::None), _menu(true), _clockMs(0),
			_clockAccumulator(0.0f), _creditsStart(0), _typed(false)
	{
		// Entry: both buffers black, the hardware palette black, then the menu drawn with item 0 and faded in while
		// MENU_BEGIN plays
		_canvas = std::make_unique<BatteryScreenCanvas>(&_screen, 100);
		_screen.Clear();
		_screen.SetPalette(_menu.GetPalette());
		PlayBatterySample(SampleMenuBegin);
		_menu.Compose(_screen, _clockMs);
		_fade.Start(1, _screen);
		LOGI("[FrontEnd] main menu");
	}

	BatteryFrontEnd::~BatteryFrontEnd()
	{
		_canvas->setParent(nullptr);
	}

	Vector2i BatteryFrontEnd::GetViewSize() const
	{
		return Vector2i(BatteryScreen::Width, BatteryScreen::Height);
	}

	void BatteryFrontEnd::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		FitView(_upscalePass, width, height);
		_upscalePass.Register();
		_canvas->setParent(_upscalePass.GetNode());
	}

	void BatteryFrontEnd::OnKeyPressed(const nCine::KeyboardEvent& event)
	{
		if (_state == State::Menu) {
			_menu.OnKeyPressed(event.sym);
		}
	}

	void BatteryFrontEnd::OnTextInput(const nCine::TextInputEvent& event)
	{
		// The credits end on any typed character (WM_CHAR: letters, digits, Space, Enter, Esc)
		if (_state == State::CreditsFadeIn || _state == State::Credits) {
			_typed = true;
		}
	}

	void BatteryFrontEnd::OnBeginFrame()
	{
		float timeMult = RealTimeMult();
		_clockAccumulator += timeMult * MsPerFrameUnit;
		std::uint32_t whole = (std::uint32_t)_clockAccumulator;
		_clockAccumulator -= (float)whole;
		_clockMs += whole;

		switch (_state) {
			case State::MenuFadeIn:
				if (_fade.Update(_screen, timeMult)) {
					_state = State::Menu;
				}
				break;
			case State::Menu: {
				_menu.Compose(_screen, _clockMs);
				BatteryMenu::Action action = _menu.Update();
				if (action != BatteryMenu::Action::None) {
					LOGI("[FrontEnd] chosen: {}", (std::int32_t)action);
					_action = action;
					_fade.Start(-1, _screen);
					_state = State::MenuFadeOut;
				}
				break;
			}
			case State::MenuFadeOut:
				if (_fade.Update(_screen, timeMult)) {
					if (_action == BatteryMenu::Action::Quit) {
						// FUN_0040c840: black, the credits picture with its palette, MENU_END, faded in; up to
						// 8000 ms from here or a typed character
						if (!_credits.IsValid()) {
							_credits.Load("credits"_s);
						}
						_screen.Clear();
						_screen.Blit(_credits);
						_screen.SetPalette(_credits.Palette);
						_typed = false;
						_creditsStart = _clockMs;
						PlayBatterySample(SampleMenuEnd);
						_fade.Start(1, _screen);
						_state = State::CreditsFadeIn;
					} else {
						Finish();
					}
				}
				break;
			case State::CreditsFadeIn:
				if (_fade.Update(_screen, timeMult)) {
					_state = State::Credits;
				}
				break;
			case State::Credits:
				if (_typed || _clockMs > _creditsStart + 8000) {
					_fade.Start(-1, _screen);
					_state = State::CreditsFadeOut;
				}
				break;
			case State::CreditsFadeOut:
				if (_fade.Update(_screen, timeMult)) {
					Finish();
				}
				break;
			case State::Done:
				break;
		}
	}

	void BatteryFrontEnd::Finish()
	{
		_state = State::Done;
		_screen.Clear();
		switch (_action) {
			case BatteryMenu::Action::NewGame: {
				// "binnen.j2l" (DAT_0044a698), then the session's new-game reset (FUN_004094b0)
				Actors::Batteryman::BatterymanPlayer::BeginNewGame();
				LevelInitialization levelInit("unknown/binnen"_s, GameDifficulty::Normal, false, false, PlayerType::Jazz);
				_root->ChangeLevel(std::move(levelInit));
				break;
			}
			case BatteryMenu::Action::LoadGame: {
				LevelInitialization levelInit;
				if (Actors::Batteryman::BatterymanPlayer::PrepareLoadGame(levelInit)) {
					_root->ChangeLevel(std::move(levelInit));
				} else {
					_root->GoToMainMenu(false);
				}
				break;
			}
			default:
#if defined(DEATH_TARGET_EMSCRIPTEN)
				// A page can't close itself: back to the menu
				_root->GoToMainMenu(false);
#else
				theApplication().Quit();
#endif
				break;
		}
	}

	// ---- Loading picture -------------------------------------------------------------------------------------

	BatteryLoadingScreen::BatteryLoadingScreen(IRootController* root, LevelInitialization&& levelInit)
		: _root(root), _levelInit(std::move(levelInit)), _framesShown(0)
	{
		// FUN_0041b010: by a substring of the level name; shown at once, no fade
		String upper = _levelInit.LevelName;
		StringUtils::uppercaseInPlace(upper);
		StringView picture = (upper.contains("BINNEN"_s) ? "level1b"_s : (upper.contains("RAIN"_s) ? "level2b"_s : "level3b"_s));
		BatteryPicture pic;
		pic.Load(picture);
		_screen.Blit(pic);
		_screen.SetPalette(pic.Palette);
		_screen.SetFade(32);
		_canvas = std::make_unique<BatteryScreenCanvas>(&_screen, 100);
		LOGI("[FrontEnd] loading picture {} for \"{}\"", picture, _levelInit.LevelName);
	}

	BatteryLoadingScreen::~BatteryLoadingScreen()
	{
		_canvas->setParent(nullptr);
	}

	Vector2i BatteryLoadingScreen::GetViewSize() const
	{
		return Vector2i(BatteryScreen::Width, BatteryScreen::Height);
	}

	void BatteryLoadingScreen::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		FitView(_upscalePass, width, height);
		_upscalePass.Register();
		_canvas->setParent(_upscalePass.GetNode());
	}

	void BatteryLoadingScreen::OnBeginFrame()
	{
		// Once the picture has been on screen, the level loads behind it
		if (++_framesShown == 3) {
			_levelInit.LoadingScreenShown = true;
			_root->ChangeLevel(std::move(_levelInit));
		}
	}

	// ---- In-game menu ----------------------------------------------------------------------------------------

	BatteryInGameMenu::BatteryInGameMenu(LevelHandler* levelHandler, SceneNode* parent)
		: _levelHandler(levelHandler), _state(State::FadeIn), _action(BatteryMenu::Action::None), _menu(false), _clockMs(0),
			_clockAccumulator(0.0f)
	{
		// FUN_0040c4f0(0): black at once, then the menu with item 0 fades in (no MENU set sound in the game)
		_canvas = std::make_unique<BatteryScreenCanvas>(&_screen, 0xF000);
		_canvas->setParent(parent);
		_screen.Clear();
		_screen.SetPalette(_menu.GetPalette());
		_menu.Compose(_screen, _clockMs);
		_fade.Start(1, _screen);
		LOGI("[FrontEnd] in-game menu");
	}

	BatteryInGameMenu::~BatteryInGameMenu()
	{
		_canvas->setParent(nullptr);
	}

	void BatteryInGameMenu::OnKeyPressed(Keys key)
	{
		if (_state == State::Menu) {
			_menu.OnKeyPressed(key);
		}
	}

	BatteryMenu::Action BatteryInGameMenu::Update(float timeMult)
	{
		timeMult = RealTimeMult();
		_clockAccumulator += timeMult * MsPerFrameUnit;
		std::uint32_t whole = (std::uint32_t)_clockAccumulator;
		_clockAccumulator -= (float)whole;
		_clockMs += whole;

		switch (_state) {
			case State::FadeIn:
				if (_fade.Update(_screen, timeMult)) {
					_state = State::Menu;
				}
				break;
			case State::Menu: {
				_menu.Compose(_screen, _clockMs);
				BatteryMenu::Action action = _menu.Update();
				if (action != BatteryMenu::Action::None) {
					LOGI("[FrontEnd] in-game chosen: {}", (std::int32_t)action);
					_action = action;
					_fade.Start(-1, _screen);
					_state = State::FadeOut;
				}
				break;
			}
			case State::FadeOut:
				if (_fade.Update(_screen, timeMult)) {
					_state = State::Done;
					return _action;
				}
				break;
			case State::Done:
				return _action;
		}
		return BatteryMenu::Action::None;
	}
}
