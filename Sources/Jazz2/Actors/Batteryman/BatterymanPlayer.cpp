#include "BatterymanPlayer.h"
#include "BatteryWorld.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Base/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;

// All numbers below are the original's (teardown/20-player-controller.md); "+0xNN" names the player-struct field
// the code mirrors, "0x40xxxx" the instruction address the behaviour was read from.

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		/** @brief Energy above which the walk/idle choice switches (0x41a / 0x41b) */
		constexpr std::int32_t EnergyLow = 0x41a;
		/** @brief Energy above which the fast walk / top speed applies (0xdac / 0xdad) */
		constexpr std::int32_t EnergyHigh = 0xdac;

		/** @brief Native rate of the samples played at a given rate (FALLFAR is 22050 Hz, as are all but one) */
		constexpr float SampleRate = 22050.0f;
		/** @brief Full mixer volume (FUN_00418ea0 stores 0 as 0x3f) */
		constexpr float FullVolume = 63.0f;

		/** @brief `(v + (v >> 31 & 0xffff)) >> 16`, i.e. whole pixels rounded toward zero, then absolute */
		std::int32_t AbsPixels(std::int32_t v)
		{
			return std::abs(v / 0x10000);
		}
	}

	BatterymanPlayer::SessionState& BatterymanPlayer::Session()
	{
		static SessionState session;
		return session;
	}

	void BatterymanPlayer::BeginNewGame()
	{
		// FUN_004094b0: the whole player struct zeroed, then energy 7000, 5 lives, 15 batteries, facing +1
		SessionState& session = Session();
		session = SessionState();
		session.Started = true;
		session.Lives = 5;
		session.Batteries = 0xf;
		session.Superbatteries = 0;
		session.Empties = 0;
		session.Facing = 1;
		LOGI("[Batteryman] new game");
	}

	namespace
	{
		/** @brief `SAVEGAME.000`, the game's one slot, next to the configuration */
		String GetSaveGamePath()
		{
			return fs::CombinePath(PreferencesCache::GetDirectory(), "SAVEGAME.000"_s);
		}

		/** @brief "BCSG" and the port's own layout version (the original's file is not read: there is none to read) */
		constexpr std::uint32_t SaveGameMagic = 0x47534342;
		constexpr std::uint32_t SaveGameVersion = 1;

		/** @brief The rest of the savegame once its level is known - read again when that level has loaded */
		SmallVector<std::uint8_t, 0>& PendingRestore()
		{
			static SmallVector<std::uint8_t, 0> data;
			return data;
		}
	}

	// The player struct of the savegame (FUN_0041b240 writes all of 0x4c55e0..+0x1e0): every field the port keeps
#define BATTERYMAN_SAVED_FIELDS(F) \
		F(_x) F(_y) F(_vx) F(_vy) F(_ax) F(_spawnX) F(_spawnY) F(_facing) \
		F(_pose) F(_poseAtTickStart) F(_animPose) F(_frameHandle) F(_prevPose) F(_frame) F(_delay) F(_frameTick) F(_loop) \
		F(_animDone) F(_standing) F(_idleChoice) F(_boredTicks) F(_lookOffset) F(_shownPose) \
		F(_airborne) F(_headClear) F(_leftFree) F(_rightFree) F(_atLedge) \
		F(_energy) F(_prevEnergy) F(_batteries) F(_superbatteries) F(_empties) F(_hurtTimer) F(_blink) F(_lives) \
		F(_rechargeSlot) F(_grabbedBySlot) F(_hazardSlot) F(_transporter) F(_inWaterSlot) F(_waterBelowSlot) F(_ay) \
		F(_pushSlot) F(_freeMove) F(_launch) F(_headBumpSlot) F(_boost) F(_rideSlot) F(_rideType) F(_rideOffX) F(_rideOffY) \
		F(_extVX) F(_extVY) F(_smoking) F(_fallTicks) \
		F(_camX) F(_camY) F(_camPrevX) F(_camPrevY) F(_camSnap) F(_leftLimit) F(_camLeft) F(_invulnerable)

	bool BatterymanPlayer::PrepareLoadGame(LevelInitialization& levelInit)
	{
		// FUN_0041b450(0): the file's level name (and player) first; the full restore follows once that level is
		// loaded (DAT_004b00f8 = 0x100)
		auto s = fs::Open(GetSaveGamePath(), FileAccess::Read);
		if (s == nullptr || !s->IsValid()) {
			return false;
		}
		if (s->ReadValueAsLE<std::uint32_t>() != SaveGameMagic || s->ReadValueAsLE<std::uint32_t>() != SaveGameVersion) {
			LOGW("SAVEGAME.000 has another version, not loaded");
			return false;
		}
		std::uint8_t nameLength = s->ReadValue<std::uint8_t>();
		String levelName(NoInit, nameLength);
		s->Read(levelName.data(), nameLength);
		std::int64_t rest = s->GetSize() - s->GetPosition();
		auto& pending = PendingRestore();
		pending.resize((std::size_t)std::max<std::int64_t>(rest, 0));
		s->Read(pending.data(), (std::int64_t)pending.size());

		levelInit.LevelName = levelName;
		levelInit.Difficulty = GameDifficulty::Normal;
		levelInit.IsReforged = false;
		levelInit.PlayerCarryOvers[0].Type = PlayerType::Jazz;
		for (std::int32_t i = 1; i < LevelInitialization::MaxPlayerCount; i++) {
			levelInit.PlayerCarryOvers[i].Type = PlayerType::None;
		}
		Session().Started = true;
		LOGI("[Batteryman] SPEL LADEN: \"{}\"", levelName);
		return true;
	}

	void BatterymanPlayer::SaveGame()
	{
		// FUN_0041b240(0)
		if (_world == nullptr) {
			return;
		}
		auto so = fs::Open(GetSaveGamePath(), FileAccess::Write);
		if (so == nullptr || !so->IsValid()) {
			LOGE("Cannot write SAVEGAME.000");
			return;
		}
		so->WriteValueAsLE<std::uint32_t>(SaveGameMagic);
		so->WriteValueAsLE<std::uint32_t>(SaveGameVersion);
		StringView levelName = _levelHandler->GetLevelName();
		so->WriteValue<std::uint8_t>((std::uint8_t)levelName.size());
		so->Write(levelName.data(), (std::int64_t)levelName.size());
#define F(field) so->WriteValueAsLE<std::int32_t>((std::int32_t)field);
		BATTERYMAN_SAVED_FIELDS(F)
#undef F
		so->WriteValueAsLE<std::int32_t>(Session().LevelsFinished);
		so->WriteValueAsLE<std::int32_t>(Session().CheatCount);
		_world->Save(*so);
		LOGI("[Batteryman] tick {} SPEL BEWAREN: \"{}\"", _tick, levelName);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Into the storage of the browser (/Persistent is IDBFS), it would be gone with the page otherwise
		so = nullptr;
		fs::SyncToPersistent();
#endif
	}

	bool BatterymanPlayer::RestoreSavedGame()
	{
		auto& pending = PendingRestore();
		if (pending.empty()) {
			return false;
		}
		MemoryStream s(pending.data(), (std::int64_t)pending.size());
#define F(field) field = (decltype(field))s.ReadValueAsLE<std::int32_t>();
		BATTERYMAN_SAVED_FIELDS(F)
#undef F
		Session().LevelsFinished = s.ReadValueAsLE<std::int32_t>();
		Session().CheatCount = s.ReadValueAsLE<std::int32_t>();
		bool restored = _world->Restore(s);
		pending.clear();
		StoreSession();
		LOGI("[Batteryman] restored: pos ({}, {}) energy {} lives {} batteries {}", _x >> 16, _y >> 16, _energy, _lives, _batteries);
		return restored;
	}

	void BatterymanPlayer::StoreSession()
	{
		SessionState& session = Session();
		session.Started = true;
		session.Lives = _lives;
		session.Batteries = _batteries;
		session.Superbatteries = _superbatteries;
		session.Empties = _empties;
		session.Facing = _facing;
		session.FreeMove = _freeMove;
		session.Smoking = _smoking;
	}

	void BatterymanPlayer::LeaveLevel()
	{
		// The level loop ends after this frame (DAT_004daba8 = 6); the session loads the header's NextLevel
		StoreSession();
		LOGI("[Batteryman] tick {} leaves the level", _tick);
		_levelHandler->BeginLevelChange(this, ExitType::Normal | ExitType::FastTransition);
	}

	bool BatterymanPlayer::OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType)
	{
		// No end-of-level animation: the original renders the frame once more and goes to black
		return true;
	}

	void BatterymanPlayer::ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver)
	{
		// Called after the activation, which already took the session's values
		_lives = Session().Lives;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Development aid: BATTERYCHECK_LIVES=n starts the level with n lives
		if (const char* give = std::getenv("BATTERYCHECK_LIVES")) {
			_lives = std::atoi(give);
			LOGW("BATTERYCHECK_LIVES gives {} lives", _lives);
		}
#endif
	}

	BatterymanPlayer::BatterymanPlayer()
		: _x(0), _y(0), _vx(0), _vy(0), _ax(0), _spawnX(0), _spawnY(0), _facing(1),
			_pose(0x22), _poseAtTickStart(-1), _animPose(0x22), _frameHandle(0), _prevPose(0x22), _frame(0), _delay(7), _frameTick(0), _loop(true),
			_animDone(0), _standing(false), _idleChoice(0x22), _boredTicks(0), _lookOffset(0), _shownPose(-1),
			_airborne(true), _headClear(true), _leftFree(true), _rightFree(true), _atLedge(false),
			_energy(MaxEnergy), _prevEnergy(MaxEnergy), _batteries(0), _superbatteries(0), _empties(0),
			_hurtTimer(0), _blink(0), _rechargeSlot(0), _grabbedBySlot(0), _hazardSlot(0), _transporter(0), _inWaterSlot(0),
			_waterBelowSlot(0), _ay(0), _pushSlot(0), _freeMove(0), _launch(0), _headBumpSlot(0), _boost(0), _rideSlot(0),
			_rideType(0), _rideOffX(0), _rideOffY(0), _extVX(0), _extVY(0), _smoking(0), _inputH(0), _inputV(0), _inputJump(false), _inputAction(false), _tickAccumulator(0.0f),
			_tick(0), _clockMs(0.0f), _levelExitTaken(false), _tracedPose(-1), _fallTicks(0), _camX(0), _camY(0),
			_camPrevX(0), _camPrevY(0), _camSnap(0), _leftLimit(0), _camLeft(0), _cheatLength(0), _paused(false),
			_pausePressed(false), _actionLatch(false), _invulnerable(0)
	{
		std::memset(_cheat, 0, sizeof(_cheat));
	}

	BatterymanPlayer::~BatterymanPlayer()
	{
		for (HeldSound* sound : { &_poseSound, &_fallSound }) {
			if (sound->Player != nullptr) {
				sound->Player->stop();
			}
		}
	}

	Task<bool> BatterymanPlayer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await Player::OnActivatedAsync(details);

		// Batteryman's own sprites: one animation per pose (Anims.j2a set 13), see Interactive/PlayerBatteryman.res
		async_await RequestMetadataAsync("Interactive/PlayerBatteryman"_s);

		// The stock movement code must not touch this actor - the controller below does everything
		SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithTilesetReduced |
			ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, false);

		// FUN_00408b50: the player starts at the top-left corner of the start tile (event 0x1d) and falls from
		// there. The event map stores a grid start as (tx * 32, ty * 32 - 8), so the tile is recovered from that.
		std::int32_t tileX = (std::int32_t)std::floor(details.Pos.X / 32.0f);
		std::int32_t tileY = (std::int32_t)std::floor((details.Pos.Y + 8) / 32.0f);

		// What carries over from the previous level, or a new game (a level started on its own is one, as the
		// original's command-line start is)
		SessionState& session = Session();
		if (!session.Started) {
			BeginNewGame();
		}
		_lives = session.Lives;
		_batteries = session.Batteries;
		_superbatteries = session.Superbatteries;
		_empties = session.Empties;
		_facing = session.Facing;
		_freeMove = session.FreeMove;
		_smoking = session.Smoking;
		LOGI("[Batteryman] level start: lives {} batteries {} superbatteries {} empties {}, {} levels finished",
			_lives, _batteries, _superbatteries, _empties, session.LevelsFinished);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Development aid for testing a place in a level: BATTERYCHECK_START=tx,ty starts there instead
		if (const char* start = std::getenv("BATTERYCHECK_START")) {
			std::int32_t sx, sy;
			if (std::sscanf(start, "%d,%d", &sx, &sy) == 2) {
				LOGW("BATTERYCHECK_START overrides the start tile: ({}, {})", sx, sy);
				tileX = sx;
				tileY = sy;
			}
		}
		// ... and BATTERYCHECK_BATTERIES=n starts with n batteries and n superbatteries
		if (const char* give = std::getenv("BATTERYCHECK_BATTERIES")) {
			_batteries = _superbatteries = std::atoi(give);
			LOGW("BATTERYCHECK_BATTERIES gives {} batteries and superbatteries", _batteries);
		}
		// ... and BATTERYCHECK_EMPTIES=n with n empty batteries
		if (const char* give = std::getenv("BATTERYCHECK_EMPTIES")) {
			_empties = std::atoi(give);
			LOGW("BATTERYCHECK_EMPTIES gives {} empty batteries", _empties);
		}
#endif
		_spawnX = (tileX * 32) * One;
		_spawnY = (tileY * 32) * One;
		_x = _spawnX;
		_y = _spawnY;

		// FUN_00409510: 3 s of blinking at level start; the camera's first update places it without smoothing and
		// with no left limit yet (the physics sets it to 16 px on the first tick)
		_blink = -210;
		_invulnerable = -210;
		_energy = _prevEnergy = MaxEnergy;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Development aid: BATTERYCHECK_ENERGY=n starts with that energy
		if (const char* energy = std::getenv("BATTERYCHECK_ENERGY")) {
			_energy = _prevEnergy = std::atoi(energy);
			LOGW("BATTERYCHECK_ENERGY starts with energy {}", _energy);
		}
#endif
		_camSnap = 1;
		_leftLimit = 0;

		_poseAtTickStart = -1;
		SetPose(0x22, 0);
		SyncActor();
		OnUpdateHitbox();

		// The level's objects: all spawned now, in the original's pool order (FUN_00408ad0)
		_world = std::make_unique<BatteryWorld>(_levelHandler, this);
		// The player is drawn after the objects' normal layer-3 sprites and before the mode-9 ones (teardown/44 §3.6)
		_renderer.setLayer(BatteryWorld::GetPlayerRenderLayer());
		if (_world->LoadEvents(_levelHandler->GetLevelName())) {
			// A savegame being loaded brings back its objects and player instead (FUN_00401ad0, DAT_004b00f8 >= 0x100);
			// the tick counter starts again at 0 either way
			if (!RestoreSavedGame()) {
				_world->SpawnAll();
			}
			SyncActor();
			OnUpdateHitbox();
		}
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Development aid: BATTERYCHECK_ACTIVATE=id,id,... powers those link ids at level start, as an insertion
		// does (the switch counter +1, the activity timer at 0xff) - a selective hhzonderhanden
		// (BATTERYCHECK_ACTIVATE_TICK=n: on tick n instead, for objects that latch the counter on their first tick)
		if (const char* at = std::getenv("BATTERYCHECK_ACTIVATE_TICK")) {
			_activateTick = std::atoi(at);
		}
		if (_activateTick == 0) {
			ApplyDevActivation();
		}
#endif

		async_return true;
	}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	void BatterymanPlayer::ApplyDevActivation()
	{
		if (const char* ids = std::getenv("BATTERYCHECK_ACTIVATE")) {
			for (const char* c = ids; *c != '\0';) {
				std::int32_t id = std::atoi(c);
				_world->ActivationCount(id)++;
				_world->Trigger(id) = 0xff;
				LOGW("BATTERYCHECK_ACTIVATE powers link id {}", id);
				while (*c != '\0' && *c != ',') {
					c++;
				}
				if (*c == ',') {
					c++;
				}
			}
		}
	}
#endif

	void BatterymanPlayer::OnUpdate(float timeMult)
	{
		// The original runs its logic at a fixed 70 Hz (FUN_00401860); a backlog of more than 35 ticks
		// collapses to one, as there. timeMult is relative to 60 Hz.
		_tickAccumulator += timeMult * (TicksPerSecond / 60.0f);
		_clockMs += timeMult * (1000.0f / 60.0f);
		std::int32_t ticks = (std::int32_t)_tickAccumulator;
		_tickAccumulator -= (float)ticks;
		if (ticks > 35) {
			ticks = 1;
		}
		for (std::int32_t i = 0; i < ticks; i++) {
			// Only the last tick of a frame draws (DAT_004d8388 == 0); a frame without a tick keeps the last sprites.
			// Paused, no tick runs and none is owed afterwards; the keys are still polled (FUN_00401860)
			if (!_paused) {
				Tick(i == ticks - 1);
			}
			if (_paused) {
				ReadInput();
				PollKeys();
			}
		}
		// One rendered frame (DAT_004d58e8, 0x4019e4). The original never draws without a tick first, so a frame
		// without one (a display faster than 70 Hz) does not count as a draw for the particles' cull rule
		if (_world != nullptr) {
			_world->CountRenderedFrame();
			if (ticks > 0) {
				_world->MarkParticlesDrawn(_camX, _camY);
			}
		}

		SyncActor();
		OnUpdateHitbox();
	}

	bool BatterymanPlayer::IsBossMode() const
	{
		return (_world != nullptr && _world->IsBossMode());
	}

	void BatterymanPlayer::OnUpdateHitbox()
	{
		// The original's collision box: x in [X-21, X+21], y in [Y-16, Y+45] (teardown/20 §4.1)
		float x = (float)(_x >> 16), y = (float)(_y >> 16);
		AABBInner = AABBf(x - 21.0f, y - 16.0f, x + 21.0f, y + 45.0f);
		AABB = AABBInner;
	}

	void BatterymanPlayer::Tick(bool lastTick)
	{
		// FUN_004015b0 / FUN_00401570: input, then camera, physics, controller and the camera again
		_tick++;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (_activateTick != 0 && (std::int32_t)_tick == _activateTick && _world != nullptr) {
			ApplyDevActivation();
		}
#endif
		ReadInput();
		PollKeys();
		UpdateCamera();
		UpdatePhysics();
		UpdateController();
		UpdateCamera();
		// FUN_00409650, after the player and the level-end counter
		if (_world != nullptr) {
			_world->TickObjects(lastTick);
			// FUN_00414e50 (the tile animations on the wall clock), then FUN_00416fa0
			_world->UpdateTileAnimations((std::uint32_t)_clockMs);
			_world->TickParticles();
		}
		ReleaseHeldSounds();

		// Trace of pose changes, for verifying the controller against the original's timings
		if (_pose != _tracedPose) {
			LOGI("[Batteryman] tick {} pose 0x{:.2x} -> 0x{:.2x} frame {} pos ({}, {}) v ({:.3f}, {:.3f}) energy {} airborne {} cam ({}, {}) look {}",
				_tick, _tracedPose, _pose, _frame, _x >> 16, _y >> 16, _vx / 65536.0f, _vy / 65536.0f, _energy, _airborne,
				_camX >> 16, _camY >> 16, _lookOffset);
			_tracedPose = _pose;
		}
	}

	bool BatterymanPlayer::OnDraw(RenderQueue& renderQueue)
	{
		bool result = Player::OnDraw(renderQueue);
		// The particles, in the layer-3 flush after the mode-9 sprites (FUN_004178a0)
		if (_world != nullptr && IsCameraReady()) {
			_world->GetParticles().Draw(renderQueue, _camX, _camY);
		}
		return result;
	}

	void BatterymanPlayer::PlayPositionalSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq)
	{
		if (_world == nullptr) {
			return;
		}
		std::int32_t v, pan;
		_world->ComputeMix(x, y, volume, v, pan);
		if (v < 4) {
			return;
		}
		float pitch = (freq == 0 ? 1.0f : (float)freq / 22050.0f);
		auto player = PlaySfx(BatteryWorld::GetSampleName(id), (float)v / 63.0f, pitch);
		if (player != nullptr) {
			player->setAs2D(true);
			player->setPosition(Vector3f((float)(pan - 0x20) / 32.0f, 0.0f, 0.0f));
		}
	}

	void BatterymanPlayer::PlaySample(StringView id, std::int32_t volume)
	{
		// The player is always where the listener is, so the original's positional calls (the fidgets) and its
		// non-positional ones (x = 0) sound the same: full volume, centred
		float gain = (volume == 0 ? FullVolume : (float)volume) / FullVolume;
		auto player = PlaySfx(id, gain);
		if (player != nullptr) {
			// The original's mixer pans by the offset from the player - none here - not by this engine's listener
			player->setAs2D(true);
			player->setPosition(Vector3f(0.0f, 0.0f, 0.0f));
		}
		LOGI("[Batteryman] tick {} sfx {} volume {}", _tick, id, volume == 0 ? 63 : volume);
	}

	void BatterymanPlayer::HoldSample(HeldSound& sound, StringView id, std::int32_t volume, std::int32_t freq)
	{
		float gain = (volume == 0 ? FullVolume : (float)volume) / FullVolume;
		float pitch = (freq == 0 ? 1.0f : (float)freq / SampleRate);
		if (sound.Player != nullptr && sound.Id == id && !sound.Player->isStopped()) {
			// Still playing: only volume and rate follow (channel state 7)
			sound.Player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			sound.Player->setPitch(pitch);
		} else {
			// Start it (channel state 6); these samples carry the loop flag in Anims.j2a and loop as a whole. The
			// mixer does not start a channel quieter than 4 (FUN_00419100 case 5), it is asked again next tick
			if (sound.Player != nullptr) {
				sound.Player->stop();
				sound.Player = nullptr;
			}
			if ((volume == 0 ? 63 : volume) < 4) {
				return;
			}
			sound.Player = PlaySfx(id, gain, pitch, SfxFlags::Looping);
			if (sound.Player != nullptr) {
				sound.Player->setAs2D(true);
				sound.Player->setPosition(Vector3f(0.0f, 0.0f, 0.0f));
			}
			sound.Id = id;
			LOGI("[Batteryman] tick {} sfx {} (held) volume {} rate {}", _tick, id, volume == 0 ? 63 : volume, freq);
		}
		sound.Requested = true;
	}

	void BatterymanPlayer::ReleaseHeldSounds()
	{
		for (HeldSound* sound : { &_poseSound, &_fallSound }) {
			if (!sound->Requested && sound->Player != nullptr) {
				sound->Player->stop();
				sound->Player = nullptr;
				LOGI("[Batteryman] tick {} sfx {} (held) stopped", _tick, sound->Id);
			}
			sound->Requested = false;
		}
	}

	void BatterymanPlayer::ReadInput()
	{
		// FUN_0040f2a0: left sets H = -1, then right H = +1; up sets V = -1, then down V = +1 (later wins)
		_inputH = 0;
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Left)) {
			_inputH = -1;
		}
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Right)) {
			_inputH = 1;
		}
		_inputV = 0;
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Up)) {
			_inputV = -1;
		}
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Down)) {
			_inputV = 1;
		}
		// Jump (Ctrl) and action (Space) are read as held keys, there is no edge detection
		_inputJump = _levelHandler->PlayerActionPressed(this, PlayerAction::Jump);
		bool action = _levelHandler->PlayerActionPressed(this, PlayerAction::Fire);
		// The action key also ends a pause, and is then ignored until it is released
		if (_actionLatch) {
			if (!action) {
				_actionLatch = false;
			}
			action = false;
		} else if (action && _paused) {
			SetPaused(false);
			_actionLatch = true;
			action = false;
		}
		_inputAction = action;
	}

	void BatterymanPlayer::OnKeyEvent(Keys key)
	{
		// The original's key queue holds Windows virtual-key codes
		std::uint16_t vk = 0;
		if (key >= Keys::A && key <= Keys::Z) {
			vk = (std::uint16_t)('A' + ((std::int32_t)key - (std::int32_t)Keys::A));
		} else if (key >= Keys::D0 && key <= Keys::D9) {
			vk = (std::uint16_t)('0' + ((std::int32_t)key - (std::int32_t)Keys::D0));
		} else {
			switch (key) {
				case Keys::Backspace: vk = 0x08; break;
				case Keys::Tab: vk = 0x09; break;
				case Keys::Return: vk = 0x0d; break;
				case Keys::Escape: vk = 0x1b; break;
				case Keys::Space: vk = 0x20; break;
				case Keys::Pause:
					// The keyboard hook flips the key's state byte once per press, and every change toggles the pause
					LOGI("[Batteryman] tick {} Pause key ({} ms)", _tick, (std::int32_t)_clockMs);
					_pausePressed = true;
					return;
				default: break;
			}
		}
		if (vk != 0 && _keyEvents.size() < 64) {
			_keyEvents.push_back(vk);
		}
	}

	void BatterymanPlayer::OnTypedText(StringView text)
	{
		for (char c : text) {
			if ((std::uint8_t)c < 0x80 && _typedChars.size() < 64) {
				_typedChars.push_back(c);
			}
		}
	}

	void BatterymanPlayer::SetPaused(bool paused)
	{
		if (_paused == paused) {
			return;
		}
		_paused = paused;
		LOGI("[Batteryman] tick {} {}", _tick, paused ? "paused" : "resumed");
		// FUN_00425400 / FUN_00425490: the music and the sounds go silent while paused
		_levelHandler->SetAudioPaused(paused);
	}

	void BatterymanPlayer::PollKeys()
	{
		// FUN_0040f570, after the movement keys of FUN_0040f2a0: one key event (FUN_0041fa20) and one typed
		// character (FUN_0041f9f0) per call
		std::uint16_t key = 0;
		if (!_keyEvents.empty()) {
			key = _keyEvents.front();
			_keyEvents.erase(_keyEvents.begin());
		}
		if (_pausePressed) {
			_pausePressed = false;
			SetPaused(!_paused);
		}
		// (Esc opens the in-game menu through the level; Tab's FPS counter, F1 and F12's screenshot are not gameplay)
		char c = 0;
		if (!_typedChars.empty()) {
			c = _typedChars.front();
			_typedChars.erase(_typedChars.begin());
		}

		auto clear = [this]() {
			std::memset(_cheat, 0, sizeof(_cheat));
			_cheatLength = 0;
		};
		bool lower = ('`' < c && c < '{');
		if (_cheatLength < 2) {
			if (c == 'h') {
				// Appends the key code ('H'), not the character
				_cheat[_cheatLength++] = (char)key;
				_cheat[_cheatLength] = '\0';
				return;
			}
			if (lower) {
				clear();
				return;
			}
			if (key == 0x08 && 0 < _cheatLength) {
				_cheat[--_cheatLength] = '\0';
			}
			return;
		}

		std::int32_t at = _cheatLength;
		if (key == 0x20 || key == 0x0d) {
			clear();
			at = 0;
		} else if (key == 0x08) {
			at = _cheatLength - 1;
			_cheat[_cheatLength - 1] = '\0';
			_cheatLength = at;
		}
		if (!lower) {
			return;
		}
		if (c == 'h' && 0 < at && _cheat[at - 1] == 'h') {
			clear();
			_cheat[0] = 'h';
			_cheat[1] = 'h';
			_cheatLength = 2;
			return;
		}
		// The original has no bound here; the buffer is capped instead
		if (at >= (std::int32_t)sizeof(_cheat) - 1) {
			return;
		}
		_cheat[at] = c;
		_cheatLength = at + 1;
		_cheat[at + 1] = '\0';

		static constexpr const char* Words[] = {
			"hhklm", "hhsterf", "hhjezus", "hhvolgende", "hhsupbat", "hhstroom", "hhstopmaarinmijndoos",
			"hhblowfreddy", "hhzondertanden", "hhzonderhanden", "hhsmokin", "hhpluseen"
		};
		for (std::int32_t i = 0; i < (std::int32_t)arraySize(Words); i++) {
			if (StringUtils::equalsIgnoreCase(StringView(_cheat), StringView(Words[i]))) {
				Session().CheatCount++;
				LOGI("[Batteryman] tick {} cheat \"{}\"", _tick, Words[i]);
				RunCheat(i);
				clear();
			}
		}
	}

	void BatterymanPlayer::RunCheat(std::int32_t index)
	{
		// The effects of FUN_0040f570, in its order
		switch (index) {
			case 0: _freeMove = (_freeMove == 0 ? 1 : 0); break;
			case 1: _energy = -1; break;
			case 2: _invulnerable = (_invulnerable < 1 ? 21000 : 0); break;
			case 3: LeaveLevel(); break;
			case 4: _superbatteries = 4; break;
			case 5:
				_energy = 7000;
				if (_lives < 0) {
					_lives = 0;
				}
				break;
			case 6: _batteries = 0xf; break;
			case 7:
			case 8: {
				// Every Freddy (or every sucker) goes; those within a screen of the player burst with a sound
				if (_world == nullptr) {
					break;
				}
				for (std::int32_t i = 1; i < _world->GetLiveEnd(); i++) {
					BatteryObject* o = _world->GetObject(i);
					if (o == nullptr) {
						continue;
					}
					std::uint8_t type = o->U8(0x44);
					if (index == 7 ? (type != 0x35) : (type != 0x33 && type != 0x34)) {
						continue;
					}
					// FUN_00408360
					std::int32_t x = o->I32(0x0c), y = o->I32(0x10);
					if (_x - 0x2800000 <= x && x <= _x + 0x2800000 && _y - 0x1e00000 <= y && y <= _y + 0x1e00000) {
						o->PlaySample(x, y, 0x26, 100, 0);
						_world->Shatter(o, 2);
					}
					_world->Destroy(i);
				}
				break;
			}
			case 9:
				// hhzonderhanden: every link id powered, every switch counter bumped
				if (_world != nullptr) {
					for (std::int32_t id = 0; id < 0x100; id++) {
						_world->Trigger(id) = 0xff;
						_world->ActivationCount(id)++;
					}
				}
				break;
			case 10: _smoking = (_smoking == 0 ? 1 : 0); break;
			case 11: _lives++; break;
		}
	}

	bool BatterymanPlayer::IsRowSolid(std::int32_t x0, std::int32_t x1, std::int32_t y, bool movingUp)
	{
		// FUN_00407a40(x0, x1 - x0, y), and the one-way rule of the floor and ceiling probes (0x40b2xx): moving up,
		// a hit counts for nothing when the last non-empty tile the probe reached has event 1
		if (_world == nullptr) {
			return false;
		}
		bool solid = _world->ProbeRow(x0, x1 - x0, y);
		if (solid && movingUp && _world->GetProbeEvent() == 1) {
			solid = false;
		}
		return solid;
	}

	bool BatterymanPlayer::IsColumnSolid(std::int32_t x, std::int32_t y, std::int32_t length)
	{
		// FUN_00407ba0(x, y, length), taken as a hit only when its result is 1..length (a pixel in rows
		// y..y+length-1); a column off the map gives 0, free
		if (_world == nullptr) {
			return false;
		}
		std::int32_t r = _world->ProbeColumn(x, y, length);
		return (0 < r && r <= length);
	}

	void BatterymanPlayer::Probe(bool movingUp)
	{
		// 0x40b2xx..0x40b44x
		std::int32_t px = _x >> 16, py = _y >> 16;
		_leftFree = !IsColumnSolid(px - 21, py - 16, 0x3a);
		_rightFree = !IsColumnSolid(px + 21, py - 16, 0x3a);
		_airborne = !IsRowSolid(px - 20, px + 20, py + 45, movingUp);
		_headClear = !IsRowSolid(px - 20, px + 20, py - 16, movingUp);
	}

	void BatterymanPlayer::UpdateCamera()
	{
		// FUN_00401000, the normal-play path. Left out because nothing in Battery.exe ever makes them non-zero: the
		// horizontal lead +0x198 (it targets xvel*8 only while +0xd0 is set, and +0xd0 has no writer), the vertical
		// lead +0x19c (zeroed before use), the shake +0x17c..+0x184 and the scripted pan +0x1c4 (no writers), and
		// the level-end path (DAT_004d8304: the level loop exits on the same tick).
		Vector2i bounds = _levelHandler->TileMap()->GetLevelBounds();
		// DAT_004d82fc / DAT_004d8300: the last pixel of the level (tiles * 32 px - 1 px)
		std::int32_t levelMaxX = (bounds.X - 1) * One;
		std::int32_t levelMaxY = (bounds.Y - 1) * One;
		// DAT_004b0104 / DAT_004b0100, the view (the LevelHandler sets the game view to this size)
		std::int32_t viewW = ViewWidth, viewH = ViewHeight;

		// The player centred horizontally; the view never shows past the level's right end, and not left of the
		// camera's own limit, which creeps 1 px per update toward the player's left limit (0x401390..0x401432)
		std::int32_t camX = _x - viewW * 0x8000;
		std::int32_t maxX = levelMaxX - viewW * One;
		if (_leftLimit == 0) {
			if (camX < 0) {
				camX = 0;
			} else if (camX > maxX + One) {
				camX = maxX + One;
			}
		} else {
			if (maxX < _camLeft) {
				_camLeft = maxX;
			}
			if (_camLeft < _leftLimit - One) {
				_camLeft += One;
			} else {
				_camLeft = _leftLimit;
			}
			if (camX < _camLeft) {
				camX = _camLeft;
			} else if (camX > maxX + One) {
				camX = maxX + One;
			}
		}

		// Vertically centred, moved by the look offset (+0x7c, up to 150 px either way), inside the level (0x40149e)
		std::int32_t camY = _y + (_lookOffset - viewH / 2) * One;
		std::int32_t maxY = levelMaxY - viewH * One + One;
		if (camY < 0) {
			camY = 0;
		} else if (camY > maxY) {
			camY = maxY;
		}

		// Halfway from where it was (C division, truncating toward zero), except right after a spawn (0x4014ef)
		if (_camSnap == 0) {
			camX = _camPrevX / 2 + camX / 2;
			camY = _camPrevY / 2 + camY / 2;
		} else {
			_camSnap--;
		}
		_camX = _camPrevX = camX;
		_camY = _camPrevY = camY;
	}

	bool BatterymanPlayer::IsStamper(BatteryObject* obj)
	{
		return (obj != nullptr && (obj->U8(0x44) == 0x2b || obj->U8(0x44) == 0x2c));
	}

	void BatterymanPlayer::UpdatePhysics()
	{
		// FUN_0040af90, statement by statement. Object links are pool slots (0 = none)
		BatteryWorld* world = _world.get();
		if (_grabbedBySlot != 0) {
			return;
		}

		// Acceleration while left/right is held, otherwise friction of 0.25 px/tick toward zero
		if (_ax == 0) {
			if (_vx > 0x4000) {
				_vx -= 0x4000;
			} else if (_vx < -0x4000) {
				_vx += 0x4000;
			} else {
				_vx = 0;
			}
		} else {
			_vx += _ax;
		}

		// Top speed: by energy (6 / 4 / 2 / 0 px/tick), raised by the launch counter (+0x108) and the belt's launch
		// boost (+0x15c, which runs out while airborne); 2 px/tick in water
		std::int32_t cap;
		if (_inWaterSlot == 0) {
			cap = (_energy >= 0xdad ? 0x60000 : (_energy >= 0x41b ? 0x40000 : (_energy >= 1 ? 0x20000 : 0)));
			if (_launch > 0) {
				cap += (_launch + 0x10) * 0x1000;
			}
			if (_boost > 0) {
				cap += 0x30000;
				if (_airborne) {
					_boost--;
				}
			}
		} else {
			cap = 0x20000;
		}
		if (cap < _vx) {
			_vx = cap;
		} else if (_vx < -cap) {
			_vx = -cap;
		}

		// Free movement (+0x70): moved by the speeds alone, no collision
		if (_freeMove != 0) {
			_x += _vx;
			_y += _vy;
			return;
		}

		// Gravity while airborne (not while launched); standing kills downward speed
		if (_launch == 0) {
			if (!_airborne) {
				if (_vy > 0) {
					_vy = 0;
				}
			} else {
				_vy += 0x4000;
			}
		}
		_vy = std::clamp(_vy, -0xa0000, 0x80000);

		std::int32_t extVX = _extVX;
		std::int32_t fallSpeed = _extVY + _vy;		// iVar9, the landing thud's measure

		// Falling faster than 4 px/tick: after 16 ticks a whistle fades in (volume up to 32) while its rate drops
		// from 44100 by 128 per tick to 11025 Hz (0x40b0xx)
		if (_vy < 0x40001) {
			_fallTicks = 0;
		} else {
			std::int32_t ticks = _fallTicks;
			_fallTicks = ticks + 1;
			if (ticks - 15 > 0) {
				HoldSample(_fallSound, "SND_FALLFAR"_s, std::min(ticks - 15, 32), std::max(0xac44 - 0x80 * (ticks + 1), 0x2b11));
			}
		}

		std::int32_t targetX = extVX + _vx + _x;
		std::int32_t targetY = fallSpeed + _y;

		// Riding (+0x160 / +0x164): type 2 carries the player horizontally (+0x16c is the offset on the object),
		// and any ride holds the player at the object's Y + 0x170 unless jumping
		if (_rideType > 0) {
			BatteryObject* ride = world->GetObject(_rideSlot);
			if (_rideType == 2) {
				std::int32_t offset = _rideOffX;
				_rideOffX = offset + _vx;
				targetX = (ride != nullptr ? ride->I32(0x0c) : 0) + offset + _vx;
			}
			if (-0x10000 < _vy) {
				targetY = (ride != nullptr ? ride->I32(0x10) : 0) + _rideOffY;
			}
		}

		// In water (+0x138): held at the surface unless jumping; drained, the body sinks to the basin bottom with a
		// bob once the death animation is nearly through. While alive, bubbles of smoke at the surface (0x40b284)
		if (_inWaterSlot > 0) {
			BatteryObject* water = world->GetObject(_inWaterSlot);
			if (_energy < 0 && GetPoseFrameCount(_animPose) - 4 <= _frame) {
				std::int32_t sink = (water != nullptr ? water->I32(0x64) : 0) - 0x440000 + BatteryWorld::Sine((std::int32_t)((_tick & 0xff) * 4)) * 4;
				if (targetY < sink) {
					targetY += 0x4000;
				}
				if (targetY > sink) {
					targetY = sink;
				}
			} else if (-0x10000 < _vy) {
				targetY = (water != nullptr ? water->I32(0x10) : 0);
			}
			if (-0x15e < _energy) {
				std::uint32_t r = world->Random();
				world->SpawnSmoke((std::int32_t)((r & 0x7fff) - 0x3fff) * 0x60 + _x, targetY);
			}
		}

		// Clamped to the level: X from the left limit (+0x1a8, at least 16 px) to the last pixel, Y from 0
		Vector2i bounds = _levelHandler->TileMap()->GetLevelBounds();
		std::int32_t levelMaxX = (bounds.X - 1) * One, levelMaxY = (bounds.Y - 1) * One;
		if (_leftLimit < 0x100000) {
			_leftLimit = 0x100000;
		}
		if (targetX < _leftLimit) {
			targetX = _leftLimit;
		} else if (levelMaxX < targetX) {
			targetX = levelMaxX;
		}
		targetY = std::clamp(targetY, 0, levelMaxY);

		// In a transporter (+0x10c) the position is set directly
		if (_transporter > 0) {
			_x = targetX;
			_y = targetY;
			_headClear = true;
			return;
		}

		// Up to 8 sub-steps of an eighth of the move each; the probes are taken before every sub-step
		std::int32_t dx = (targetX - _x) / 8;
		std::int32_t dy = (targetY - _y) / 8;
		for (std::int32_t i = 0; i < 8; i++) {
			Probe(dy < 0);
			if (!_leftFree && dx < 0) {
				dx = 0;
			}
			if (!_rightFree && dx > 0) {
				dx = 0;
			}
			if (!_headClear && dy < 0) {
				dy = 0;
			}
			if (!_airborne && dy > 0) {
				dy = 0;
			}
			if (dx == 0 && dy == 0) {
				break;
			}
			_y += dy;
			_x += dx;
		}
		if (dx == 0) {
			_vx = 0;
		}
		if (dy == 0) {
			_vy = 0;
		}

		// Riding a stamper up into a ceiling is being crushed by it (0x40b5xx)
		std::int32_t rideType = _rideType;
		if (rideType == 1 && dy == 0 && !_headClear) {
			BatteryObject* ride = world->GetObject(_rideSlot);
			if (ride != nullptr && IsStamper(ride)) {
				_grabbedBySlot = _rideSlot;
			}
		}
		// Pushing against a solid object (+0x50) is a wall on that side
		if (_pushSlot > 0) {
			BatteryObject* push = world->GetObject(_pushSlot);
			if (push != nullptr) {
				if (_x < push->I32(0x0c) && _facing > 0) {
					_rightFree = false;
				} else if (push->I32(0x0c) < _x && _facing < 0) {
					_leftFree = false;
				}
			}
		}

		std::int32_t px = _x >> 16, py = (_y >> 16) - 16;
		_atLedge = false;
		if (_headBumpSlot != 0) {
			_headClear = false;
		}
		if (_inWaterSlot < 1) {
			if (rideType > 0) {
				_airborne = false;
			}
			if (!_airborne) {
				// The landing thud, louder the faster the fall
				if (0x10000 < fallSpeed) {
					PlaySample("SND_FALLOPGROND"_s, (fallSpeed >> 15) + 10);
				}
				// The ledge test: FUN_00407ba0 over 0x7c rows from Y-16 of the column 12 px ahead gives more than 0x78
				// (nothing in the first 120 rows), looked at only on the side the player faces and only if that wall
				// column is free; a column off the map gives 0, no ledge
				if (!_airborne && _rideType < 1) {
					bool look = false;
					std::int32_t column = 0;
					if (_facing < 0 && _leftFree) {
						column = px - 12;
						look = true;
					} else if (_facing > 0 && _rightFree) {
						column = px + 12;
						look = true;
					}
					if (look && _world != nullptr && 0x78 < _world->ProbeColumn(column, py, 0x7c)) {
						_atLedge = true;
					}
				}
			}
		} else if (_poseAtTickStart == 0x41 || (_poseAtTickStart == 0x1a && _frame > 2)) {
			// Wading or after the splash, the player stands in the water
			_airborne = false;
		}

		// Tile event 0x11 (end of level) at the position ends the level (DAT_004daba8 = 6)
		if (!_levelExitTaken && world->GetEventType(_x >> 21, _y >> 21) == 0x11) {
			// DAT_004d8304 = 1 (once), the levels-finished counter +0x1d8
			_levelExitTaken = true;
			Session().LevelsFinished++;
			LeaveLevel();
		}
	}

	void BatterymanPlayer::UpdateController()
	{
		// FUN_00409f00, statement by statement. +0x41 still holds the pose the previous tick latched
		BatteryWorld* world = _world.get();
		std::int32_t pose41 = _poseAtTickStart;

		if (pose41 != 0x01 && pose41 != 0x05) {
			if (_transporter < 1) {
				if (_grabbedBySlot == 0) {
					if (_hazardSlot == 0) {
						if (_rechargeSlot == 0) {
							if (_energy < 1 && !_airborne) {
								// Drained on the ground: the death poses (0x40a1xx)
								if (_inWaterSlot < 1) {
									_vx = 0;
									_ax = 0;
									if (pose41 == 0x0c || pose41 == 0x0d) {
										TryRespawn(1);
									} else {
										PlaySample("SND_DIE1"_s, 0);
										SetPose(0x0c, 0);
									}
								} else if (pose41 == 0x0e || pose41 == 0x0f) {
									TryRespawn(0);
								} else {
									SetPose(0x0e, 0);
								}
							} else if (_launch < 1) {
								if (_freeMove == 0) {
									UpdateInput();
								} else {
									// Free movement: 8 px/tick in the held directions
									_facing = _inputH;
									_vx = _inputH << 19;
									_vy = _inputV << 19;
								}
							} else {
								_launch--;
							}
						}
					} else if (_rechargeSlot != 0) {
						// A hit ends the recharge
						_rechargeSlot = 0;
					}
				} else {
					// Crushed (+0x150): -350 energy per tick, held in the crushed pose of the stamper (9 black, 0xb
					// grey, 10 when it was ridden into a ceiling) until respawn
					_energy += -0x15e;
					std::int32_t pose;
					if (_rideSlot == 0) {
						BatteryObject* grab = world->GetObject(_grabbedBySlot);
						pose = (grab != nullptr && grab->U8(0x44) == 0x2c ? 9 : 0x0b);
					} else {
						pose = 10;
					}
					SetPose(pose, 0);
					TryRespawn(1);
				}
			} else {
				UpdateTransporter();
			}
		}

		// Timers (0x40a3ed..0x40a44d): the look offset eases back without vertical input, the blink counter runs
		// toward 0, the hurt lock runs out
		if (_inputV == 0) {
			if (_lookOffset > 8) {
				_lookOffset -= 8;
			} else if (_lookOffset < -8) {
				_lookOffset += 8;
			} else {
				_lookOffset = 0;
			}
		}
		if (_transporter < 0) {
			_transporter = 0;
		}
		if (_blink < 0) {
			_blink++;
		} else if (_blink > 0) {
			_blink--;
		}
		if (_hurtTimer > 0) {
			_hurtTimer--;
		}

		// The pose switch at 0x40a483. The pose is latched first: +0x41 for the selectors' "pose at tick start" and
		// +0x34, the animation the frame advance and the frame handle use - so on a tick that changes the pose, the
		// frame still advances and shows in the animation of the old one
		_delay = 7;
		_poseAtTickStart = _pose;
		_animPose = _pose;
		std::int32_t fc = GetPoseFrameCount(_animPose);
		bool done = (_animDone != 0);
		bool anyInput = (_inputH != 0 || _inputV != 0 || _inputJump || _inputAction);
		std::int32_t energyTerm = 0;	// iVar9: 0 = -1 every second tick, -1 = none, n > 0 = -n every tick
		switch (_pose) {
			case 0x00: // teeter at a ledge
				PickLocomotion();
				if (!_atLedge) {
					SetPose(0x23, 0x10);
				}
				break;
			case 0x01: // battery insert
			case 0x05: // superbattery insert
				_vx = 0;
				_vy = 0;
				_delay = 2;
				if (done) {
					PickLocomotion();
				}
				break;
			case 0x07: { // recharging: +70 per tick up to 7000, the frame shows the charge
				if (_energy < 7000) {
					_energy += 0x46;
				}
				_frame = std::clamp(_energy / 700 + (std::int32_t)((_tick >> 2) & 1), 0, 9);
				_frameHandle = MakeFrameHandle((std::int32_t)AnimSet::Man, _animPose, _frame);
				if (_energy < 7000) {
					return;
				}
				_energy = 7000;
				_rechargeSlot = 0;
				SetPose(0x23, 0x10);
				break;
			}
			case 0x09: // crushed by the black / grey stamper
			case 0x0b:
				_delay = 7;
				break;
			case 0x0a: { // crushed into a ceiling while riding a stamper: held 130 px above its spawn point
				BatteryObject* grab = world->GetObject(_grabbedBySlot);
				_delay = 14;
				_y = (grab != nullptr ? grab->I32(0x08) : 0) - 0x820000;
				break;
			}
			case 0x0c: // death on land, the bang on frame 19
				_delay = 10;
				if (_frame == 0x13 && _frameTick == 0) {
					PlaySample("SND_DIEBOOM"_s, 0);
				}
				TryRespawn(1);
				break;
			case 0x0d: // dead, waiting: back at the respawn point once the animation is through
			case 0x0f:
				if (done) {
					_x = _spawnX;
					_y = _spawnY;
					_grabbedBySlot = 0;
					_energy = 7000;
					_inWaterSlot = 0;
					SetPose(0x3e, 0);
				}
				break;
			case 0x0e: // death in water
				HoldSample(_poseSound, "SND_B012"_s, 0, 0);
				TryRespawn(1);
				break;
			case 0x10: // crouch start
				if (done) {
					SetPose(0x11, 1);
				}
				break;
			case 0x11: // crouch / look down
				if (_lookOffset < 0) {
					_lookOffset += 8;
				} else if (_lookOffset < 0x96) {
					_lookOffset += 4;
				}
				if (_inputV > 0) {
					PickLocomotion();
				} else {
					SetPose(0x22, 0);
					PickLocomotion();
				}
				break;
			case 0x12: // stand up from crouching
				_lookOffset /= 2;
				if (done) {
					_lookOffset = 0;
					SetPose(0x23, 0);
				}
				if (_inputV < 1) {
					SetPose(0x22, 0);
				}
				break;
			case 0x16: // hurt by the boss projectile, until the hurt lock runs out
				if (_hurtTimer == 0) {
					PickLocomotion();
				}
				break;
			case 0x17: // bitten by a sucker / big sucker, until the animation is through
			case 0x18:
				HoldSample(_poseSound, "SND_B018"_s, 0, 0);
				_delay = 7;
				if (done) {
					_hazardSlot = 0;
					SetPose(0x23, 0x10);
				}
				break;
			case 0x19: // falling into water
				_delay = 7;
				if (_inWaterSlot >= 1) {
					SetPose(0x1a, 0);
				} else if (_rideSlot == 0 && _waterBelowSlot != 0) {
					if (!_airborne) {
						PickLocomotion();
					}
				} else {
					PickLocomotion();
				}
				break;
			case 0x1a: // splash-down
				_delay = 3;
				if (done) {
					SetPose(_vx == 0 ? 0x42 : 0x41, 1);
				}
				break;
			case 0x1b: // bored fidget A
			case 0x1e: // bored fidget B
				_delay = 4;
				if (done) {
					_boredTicks = 1;
					SetPose(_pose == 0x1b ? 0x1c : 0x1f, 1);
				}
				break;
			case 0x1c: // fidget hold: 12 ticks per frame of the hold animation, or until any input
			case 0x1f:
				_delay = 4;
				_boredTicks++;
				if (anyInput || _boredTicks >= fc * 12) {
					SetPose(0x1d, 0);
				}
				break;
			case 0x1d: // fidget end, skids
			case 0x35:
			case 0x36:
			case 0x37:
				_delay = 3;
				if (done) {
					PickLocomotion();
				}
				break;
			case 0x20: // tired idle (energy <= 1050), panting on frame 8
			case 0x21:
				if (_frame == 8 && _frameTick == 0) {
					PlaySample("SND_HIJG"_s, 0);
				}
				PickLocomotion();
				break;
			case 0x22: // idle
			case 0x23:
			case 0x24:
				PickLocomotion();
				break;
			case 0x25: // walk start
			case 0x26:
			case 0x27:
				_delay = 3;
				if (done) {
					_standing = false;
					SetPose(0, 4);
				}
				break;
			case 0x28: // jump take-off (directional)
				energyTerm = 3;
				_delay = 3;
				if (done) {
					SetPose(0x29, 1);
				}
				break;
			case 0x29: // rising
				energyTerm = 4;
				if (-0x10001 < _vy) {
					SetPose(0x2a, 0);
				}
				break;
			case 0x2a: // apex
				energyTerm = 1;
				_delay = 3;
				if (done) {
					SetPose(_waterBelowSlot < 1 ? 0x2b : 0x19, 0);
				}
				break;
			case 0x2b: // falling
				energyTerm = -1;
				if (0 < _waterBelowSlot) {
					SetPose(0x19, 0);
				} else if (0 < _inWaterSlot) {
					SetPose(0x1a, 0);
				} else if (!_airborne) {
					SetPose(_vx == 0 ? 0x2c : 0, _vx == 0 ? 0 : 4);
				}
				break;
			case 0x2c: // landing
			case 0x4a:
				if (_pose == 0x4a) {
					_delay = 3;
				}
				energyTerm = -1;
				if (_vx != 0) {
					SetPose(0, 4);
				} else if (done) {
					SetPose(0, 0x10);
				}
				break;
			case 0x32: // walking-backwards fidget, with its two "moonwalk" sounds
				if (_frameTick == 0) {
					if (_frame == 0x0b) {
						PlaySample("SND_MJACKSON1"_s, 0);
					} else if (_frame == 0x0f || _frame == 0x23) {
						PlaySample("SND_MJACKSON2"_s, 0);
					}
				}
				_delay = 3;
				if (!done) {
					if (!anyInput) {
						if (8 < _frame && _frame < 0x28) {
							_vx = _facing * -0x10000;
						}
					} else {
						_vx = 0;
						PickLocomotion();
					}
				} else {
					PickLocomotion();
				}
				break;
			case 0x38: // walk, energy 1051..3500
			case 0x40: // walk, energy > 3500
				energyTerm = 1;
				_delay = 9 - AbsPixels(_vx);
				PickLocomotion();
				break;
			case 0x39: // in a transporter: the transporter branch above moves the player
			case 0x3a:
			case 0x3b:
				break;
			case 0x3c: // walk, energy <= 1050
				_delay = 6;
				energyTerm = 1;
				PickLocomotion();
				break;
			case 0x3e: // respawn
				if (done) {
					SetPose(0x22, 0);
				}
				break;
			case 0x41: // wading / standing in water
			case 0x42:
				HoldSample(_poseSound, "SND_B011"_s, 0, 0);
				energyTerm = 0x19;
				PickLocomotion();
				break;
			case 0x43: // look-up start
				if (done) {
					SetPose(0x44, 1);
				}
				break;
			case 0x44: // look up
				if (0 < _lookOffset || -0x96 < _lookOffset) {
					_lookOffset -= 4;
				}
				if (_inputV < 0) {
					PickLocomotion();
				} else {
					SetPose(0x22, 0);
					PickLocomotion();
				}
				break;
			case 0x45: // stop looking up
				_lookOffset /= 2;
				if (done) {
					_lookOffset = 0;
					SetPose(0x23, 0);
				}
				if (_inputV >= 0) {
					SetPose(0x22, 0);
				}
				break;
			case 0x46: // jump take-off straight up
				energyTerm = 3;
				_delay = 3;
				if (done) {
					SetPose(0x47, 1);
				}
				break;
			case 0x47: // rising (straight)
				energyTerm = 4;
				if (-0x10001 < _vy) {
					SetPose(0x48, 0);
				}
				break;
			case 0x48: // apex (straight)
				energyTerm = 1;
				_delay = 3;
				if (done) {
					SetPose(_waterBelowSlot < 1 ? 0x49 : 0x19, 0);
				}
				break;
			case 0x49: // falling (straight)
				energyTerm = -1;
				if (0 < _waterBelowSlot || 0 < _inWaterSlot) {
					SetPose(0x19, 0);
				} else if (!_airborne) {
					SetPose(_vx == 0 ? 0x4a : 0, _vx == 0 ? 0 : 4);
				}
				break;
			default: // poses the player code never uses
				SetPose(0x22, 0);
				break;
		}

		// Smoke around the player while +0x1dc is set (the hhsmokin cheat): 4..11 puffs (0x40a4ff)
		if (_smoking != 0) {
			std::int32_t n = (std::int32_t)(world->Random() & 7) + 4;
			do {
				std::uint32_t r1 = world->Random();
				std::uint32_t r2 = world->Random();
				world->SpawnSmoke((std::int32_t)((r2 & 0x1f) - 0xf) * 0x10000 + _x, _y - (std::int32_t)(r1 & 7) * 0x10000);
			} while (--n > 0);
		}

		// Energy (0x40a589): default -1 every second tick
		if (energyTerm >= 0) {
			if (energyTerm == 0) {
				if ((_tick & 1) != 0) {
					_energy -= 1;
				}
			} else {
				_energy -= energyTerm;
			}
		}
		// Losing the last energy costs a life (0x40a5a4)
		if (_prevEnergy > 0 && _energy < 1) {
			_energy = -1;
			_lives--;
		}
		_prevEnergy = _energy;

		// Frame advance (0x40a5a4..0x40a6b7) in the latched animation: a frame lasts delay+1 ticks; at the end loop or
		// hold the last
		fc = GetPoseFrameCount(_animPose);
		_frameTick++;
		if (_frameTick <= _delay) {
			if (_frame >= fc) {
				_frame = 0;
			}
		} else {
			_frameTick = 0;
			_frame++;
			// Footsteps of the walk loops on their frames 4 and 8 (0x40a5f0)
			if ((_animPose == 0x40 || _animPose == 0x38 || _animPose == 0x3c) && (_frame == 4 || _frame == 8)) {
				PlaySample(_frame == 4 ? "SND_BMSTEP01"_s : "SND_BMSTEP02"_s, 0x10);
			}
			if (_frame >= fc) {
				_frame = (_loop ? 0 : std::max(fc - 1, 0));
				_animDone++;
			}
		}
		// The frame handle (+0x28) the sprite and every collision test use (0x40a6a1)
		_frameHandle = MakeFrameHandle((std::int32_t)AnimSet::Man, _animPose, _frame);
	}

	void BatterymanPlayer::UpdateInput()
	{
		// The input block of FUN_00409f00 (0x40a0fb..0x40a3d9)
		// Acceleration from the horizontal input; facing follows only once vx has that sign
		if (_inputH < 0) {
			_ax = -0x4000;
			if (_vx < 0) {
				_facing = -1;
			}
		} else if (_inputH > 0) {
			_ax = 0x4000;
			if (_vx > 0) {
				_facing = 1;
			}
		} else {
			_ax = 0;
		}

		// Jump: grounded with the head clear (energy is left here): -8 px/tick, -10 off a running belt, +1 straight
		// up, and in water no run-up and +2; costs 17
		if (_inputJump && !_airborne && _headClear && 0 < _energy) {
			_vy = -0x80000;
			BatteryObject* ride = _world->GetObject(_rideSlot);
			if (ride != nullptr && ride->U8(0x44) == 0x2f && _extVX != 0) {
				_vy = -0xa0000;
			}
			PlaySample("SND_B004"_s, 0x20);
			if (_inputH == 0) {
				SetPose(0x46, 2);
				_vy += 0x10000;
			} else {
				SetPose(0x28, 2);
			}
			if (_inWaterSlot != 0) {
				_vx = 0;
				_vy += 0x20000;
			}
			_energy -= 17;
		}

		// Action (Space, held): grounded with energy left, a battery (a superbattery in the boss fight) goes into the
		// nearest idle receptacle (event 0x21, state 0x1c) within 90 px, if the player faces the same way as it and
		// stands more than 8 px on the far side of it (0x40a2c5..0x40a3d9, teardown/41 §3.7)
		if (_inputAction && !_airborne && _energy > 0) {
			bool boss = _world->IsBossMode();
			if (boss ? (_superbatteries >= 1) : (_batteries >= 1)) {
				std::int32_t slot = _world->FindNearest(_x, _y, 0x1fa4, 0x21, 0x1c);
				BatteryObject* target = _world->GetObject(slot);
				if (target != nullptr && _facing == target->S8(0x45)) {
					std::int32_t tx = target->I32(0x0c);
					if ((_facing > 0 && _x < tx - 0x80000) || (_facing < 0 && tx + 0x80000 < _x)) {
						target->U8(0x41) = 0x10;
						target->I32(0x24) = -40;
						if (!boss) {
							SetPose(0x01, 0);
							_batteries--;
						} else {
							SetPose(0x05, 0);
							_superbatteries--;
						}
						_energy -= 0x8c;
						_empties++;
					}
				}
			}
		}
	}

	void BatterymanPlayer::UpdateTransporter()
	{
		// The transporter branch of FUN_00409f00 (+0x10c is the tube's slot): no input, no ride, facing right; the
		// tube carries the player in, up through it (invisible) and out at its top
		_inputH = _inputV = 0;
		_inputJump = _inputAction = false;
		_vx = 0;
		_ax = 0;
		_ay = 0;
		_rideSlot = 0;
		_rideType = 0;
		_extVY = 0;
		_extVX = 0;
		_facing = 1;
		_poseAtTickStart = _pose;
		BatteryObject* tube = _world->GetObject(_transporter);
		std::int32_t tx = (tube != nullptr ? tube->I32(0x0c) : 0), ty = (tube != nullptr ? tube->I32(0x10) : 0);
		std::int32_t height = (tube != nullptr ? tube->I32(0x54) : 0), speed = (tube != nullptr ? tube->I32(0x5c) : 0);
		if (_pose == 0x3b) {
			_x = tx + 0x100000;
			_y = ty + 0x2c0000;
			if (_animDone != 0) {
				_vy = speed * -0x10000;
				SetPose(0x39, 1);
			}
		} else if (_pose == 0x39) {
			_x = tx + 0x170000;
			_vy = speed * -0x10000;
			if (_y < ty + height * -0x10000) {
				SetPose(0x3a, 0);
				_vy = 0;
			}
		} else if (_pose == 0x3a) {
			_x = tx + 0x170000;
			_y = ty + (height + 0x27) * -0x10000;
			if (_frame == 10 && _frameTick == 0) {
				PlaySample("SND_B017"_s, 0);
			}
			_vy = 0;
			if (_animDone != 0) {
				_launch = 0;
				_vy = speed * -40000;
				SetPose(0x47, 1);
			}
		} else {
			_transporter = 0;
			SetPose(0x23, 0x10);
		}
	}

	void BatterymanPlayer::SetPose(std::int32_t pose, std::uint32_t flags)
	{
		// FUN_00409880 (verbatim in teardown/20 §2.4): a no-op if the pose equals the pose at tick start
		std::int32_t prev = _poseAtTickStart;
		if (pose == prev) {
			return;
		}
		_prevPose = prev;
		_pose = pose;
		_loop = ((flags & 0x01) != 0);
		if ((flags & 0x02) == 0) {
			_frame = 0;
			_frameTick = 0;
			_animDone = 0;
		}

		if ((flags & 0x04) != 0) {
			// Walk selector: from standing a walk-start, else the walk loop - both by energy
			if (!_standing) {
				_pose = (_energy <= EnergyLow ? 0x3c : (_energy > EnergyHigh ? 0x40 : 0x38));
				if (_pose != prev) {
					_frame = 0;
					_frameTick = 0;
				}
				_loop = true;
			} else {
				_pose = (_energy <= EnergyLow ? 0x26 : (_energy > EnergyHigh ? 0x27 : 0x25));
				_standing = false;
				_frame = 0;
				_frameTick = 0;
				_loop = false;
			}
		}

		if ((flags & 0x08) != 0) {
			// Stop selector: skid by speed when grounded, fall when airborne. When already skidding or stopped the
			// pose is left as passed (0), exactly as the original does
			if (!_airborne) {
				if (prev != 0x35 && prev != 0x36 && prev != 0x37 && prev != 0x23) {
					std::int32_t n = AbsPixels(_vx);
					if (n < 2) {
						_standing = true;
						_pose = 0x23;
					} else if (n < 4) {
						_pose = 0x35;
					} else if (n < 6) {
						_pose = 0x36;
					} else {
						// Braking from full speed squeals (0x4099e7)
						PlaySample("SND_REMMEN"_s, 0x14);
						_pose = 0x37;
					}
					_frame = 0;
					_frameTick = 0;
					_loop = false;
				}
			} else {
				SetPose(_vx == 0 ? 0x49 : 0x2b, 0);
			}
		}

		if ((flags & 0x10) != 0) {
			// Idle selector with boredom (teardown/20 §3.2)
			if (!_standing) {
				_idleChoice = 0x21;
				_loop = false;
				if (!_atLedge) {
					if (_energy > EnergyLow) {
						_idleChoice = 0x22;
					}
				} else {
					_idleChoice = 0;
					_loop = true;
				}
				_standing = true;
				_frame = 0;
				_frameTick = 0;
				_boredTicks = 0;
				_pose = _idleChoice;
				return;
			}

			_loop = true;
			_boredTicks++;
			if (_energy <= EnergyLow) {
				_idleChoice = 0x21;
				_pose = 0x21;
				return;
			}
			if (_animDone == 0) {
				if (_atLedge || _idleChoice != 0) {
					_pose = _idleChoice;
					return;
				}
			} else {
				// After each completed idle loop a new idle is drawn (r < 51 of 64: 0x24; otherwise 0x23); once
				// standing for more than 70 ticks also the fidgets
				std::uint32_t r = (_world != nullptr ? _world->Random() : 0) & 0x3f;	// the original PRNG (teardown/43 §7)
				_animDone = 0;
				if (r < 0x33) {
					if (_boredTicks > 0x46) {
						// The two face-pulling fidgets each start with their sound (0x409b4d / 0x409b7d)
						if (r < 10) {
							PlaySample("SND_B007"_s, 0);
							_idleChoice = 0x1b;
							_pose = 0x1b;
							return;
						}
						if (r < 0x14) {
							PlaySample("SND_B009"_s, 0);
							_idleChoice = 0x1e;
							_pose = 0x1e;
							return;
						}
						if (r < 0x1e && CanWalkBackwards()) {
							_idleChoice = 0x32;
							_pose = 0x32;
							return;
						}
					}
					_idleChoice = 0x24;
					_pose = 0x24;
					return;
				}
			}
			_idleChoice = 0x23;
			_pose = _idleChoice;
		}
	}

	void BatterymanPlayer::PickLocomotion()
	{
		// FUN_00409c00 (verbatim structure in teardown/20 §3.1)
		if (_hurtTimer != 0) {
			return;
		}

		if (_inWaterSlot < 1) {
			if (_lookOffset != 0) {
				if (_lookOffset < 0) {
					// Looking up: stays unless jump, a direction, or down
					if (!_inputJump && _inputH == 0 && _inputV <= 0) {
						return;
					}
					SetPose(0x45, 0);
					return;
				}
				// Looking down: stays unless jump, a direction, or up
				if (!_inputJump && _inputH == 0 && _inputV >= 0) {
					return;
				}
				SetPose(0x12, 0);
				return;
			}

			if (!_inputJump) {
				if (_inputH != 0) {
					if (_airborne) {
						SetPose(0x2b, 0);
						return;
					}
					if (_inputH > 0 && _rightFree) {
						if (_vx < -0x20000) {
							SetPose(0, 8);
							return;
						}
					} else if (_inputH < 0 && _leftFree) {
						if (_vx > 0x20000) {
							SetPose(0, 8);
							return;
						}
					} else {
						// Pushing against a wall is standing
						SetPose(0, 0x12);
						return;
					}
					SetPose(0, 6);
					return;
				}
				if (_airborne) {
					SetPose(_vx == 0 ? 0x49 : 0x2b, 0);
					return;
				}
				if (AbsPixels(_vx) > 2) {
					SetPose(0, 10);
					return;
				}
				if (_inputV < 0) {
					SetPose(0x43, 0);
					return;
				}
				if (_inputV > 0) {
					SetPose(0x10, 0);
					return;
				}
				SetPose(0, 0x12);
				return;
			}
		} else {
			if (_vy > 0x10000) {
				SetPose(0x19, 1);
				return;
			}
			if (!_inputJump) {
				SetPose(_vx != 0 ? 0x41 : 0x42, 1);
				return;
			}
		}

		SetPose(_inputH == 0 ? 0x46 : 0x28, 0);
	}

	void BatterymanPlayer::TryRespawn(std::int32_t requireDone)
	{
		// FUN_00409e30: every call stops the player; drained, jump (held) continues once the death animation is
		// through (or at once with requireDone 0) - to "dead, waiting", then the respawn at +0x04/+0x08 - or, with no
		// lives left, game over (DAT_004daba8 = 5)
		_vx = 0;
		_vy = 0;
		_ax = 0;
		_ay = 0;
		if (_energy < 0 && (_animDone != 0 || requireDone == 0) && _inputJump) {
			if (_lives < 0) {
				// DAT_004daba8 = 5: back to the main menu; the next game starts afresh
				if (!_levelExitTaken) {
					_levelExitTaken = true;
					Session().Started = false;
					LOGI("[Batteryman] tick {} game over", _tick);
					_levelHandler->HandleGameOver(this);
				}
				return;
			}
			if (_poseAtTickStart == 0x0e) {
				SetPose(0x0f, 0);
				return;
			}
			if (_poseAtTickStart == 0x0c) {
				SetPose(0x0d, 0);
				return;
			}
			_x = _spawnX;
			_y = _spawnY;
			_grabbedBySlot = 0;
			_energy = 7000;
			_inWaterSlot = 0;
			SetPose(0x3e, 0);
		}
	}

	bool BatterymanPlayer::CanWalkBackwards()
	{
		// FUN_0040bbe0: grounded, and floor (point test at Y+46) for 88 px behind the player
		if (_airborne) {
			return false;
		}
		std::int32_t px = _x >> 16, py = _y >> 16;
		for (std::int32_t d = 1; d <= 88; d++) {
			if (!IsRowSolid(px - _facing * d, px - _facing * d, py + 46, false)) {
				return false;
			}
		}
		return true;
	}

	std::int32_t BatterymanPlayer::GetPoseFrameCount(std::int32_t pose)
	{
		if (_metadata != nullptr && pose >= 0 && pose < PoseCount) {
			if (auto* res = _metadata->FindAnimation((AnimState)(PoseStateBase + pose))) {
				return std::max(res->FrameCount, 1);
			}
		}
		return 1;
	}

	void BatterymanPlayer::SyncActor()
	{
		// The original draws the frame with its top-left at (X + hotspotX, Y + hotspotY), mirrored about X when
		// facing left - which is exactly how this engine draws an actor at its position (teardown/90 §2.2)
		// FUN_004097f0: with +0x70 at -1 the sprite bobs (8 px up, ±8 on the sine); hhklm sets it to 1, so nothing in
		// the game does
		std::int32_t y = _y;
		if (_freeMove == -1) {
			y += -0x80000 + BatteryWorld::Sine((std::int32_t)(((_tick * 3) & 0xff) * 4)) * 8;
		}
		_pos = Vector2f((float)(_x >> 16), (float)(y >> 16));
		SetFacingLeft(_facing < 0);

		// The frame of the frame handle: the latched animation (see UpdateController) at the current frame
		if (_shownPose != _animPose) {
			if (SetAnimation((AnimState)(PoseStateBase + _animPose))) {
				_shownPose = _animPose;
			}
		}

		// Show the controller's frame: one long time slot per frame, with the clock parked mid-slot, so the
		// renderer's own advance between updates can never move it
		if (_currentAnimation != nullptr && _renderer.FrameCount > 0) {
			constexpr float SlotLength = 1000.0f;
			_renderer.LoopMode = AnimationLoopMode::Loop;
			_renderer.AnimPaused = false;
			_renderer.AnimDuration = _renderer.FrameCount * SlotLength;
			_renderer.AnimTime = ((float)std::clamp(_frame, 0, _renderer.FrameCount - 1) + 0.5f) * SlotLength;
		}

		// Pose 0x39 (inside a transporter) is not drawn; at spawn the player blinks with bit 7 of the
		// millisecond clock until the counter runs out (FUN_004097f0)
		bool visible = (_poseAtTickStart != 0x39) && (_blink == 0 || (((std::uint32_t)_clockMs >> 7) & 1) != 0);
		_renderer.setDrawEnabled(visible);
	}
}
