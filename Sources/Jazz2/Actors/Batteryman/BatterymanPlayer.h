#pragma once

#include "../Player.h"
#include "../../../nCine/Input/Keys.h"

#include <memory>

namespace Jazz2::Actors::Batteryman
{
	class BatteryObject;
	class BatteryWorld;

	/**
		@brief BatteryCheck's player, "Batteryman"

		A literal port of the original's player controller (`Battery.exe`, specified in
		`teardown/20-player-controller.md`, summarised in `BATTERY-ENGINE-TEARDOWN.md`):

		- `FUN_0040af90` - physics and tile collision: 16.16 fixed point, ±0.25 px/tick² acceleration and friction,
		  speed capped by energy (6/4/2/0 px/tick), gravity 0.25 px/tick², vertical speed clamped to −10…+8, and an
		  up-to-8-sub-step sweep against line probes - floor row Y+45 and ceiling row Y−16 (columns X−20…X+20),
		  wall columns X±21 (rows Y−16…Y+41). The position is therefore 45 px above the feet.
		- `FUN_00409f00` - the pose state machine. The pose byte is the Anims.j2a set-13 (MAN) animation index;
		  each pose sets a frame delay and a frame lasts delay+1 ticks, looping or holding the last frame. It also
		  runs the energy economy (passive −1 per 2 ticks, walking −1, jumping −17 plus per-tick costs, death below 1).
		- `FUN_00409c00` - the locomotion picker, and `FUN_00409880` with its walk / skid / idle selectors.
		- `FUN_00409e30` - death and respawn.
		- `FUN_00401000` - the camera: the player centred on the 640×480 view, shifted by the look offset, clamped to
		  the level, and averaged with its previous position each time it runs - twice per tick, as in the original.
		- Sounds at the same points as there (`FUN_00418ea0` one-shots, `FUN_00418f90` sustained loops), with the
		  original's volumes and playback rates; the samples are Anims.j2a's, under their original names.

		The controller runs on its own fixed 70 Hz tick (the original's rate) and replaces the stock @ref Player
		controller entirely; the sprite shown is the current pose's animation at the controller's own frame index.
		It carries the battery state the HUD shows (energy `+0x60`, batteries `+0x140`, superbatteries `+0x144`,
		empty batteries `+0x148`, boss mode `DAT_004c1408`). Object interactions (charging port, receptacles,
		water, grabbers, hazards, transporters) are reached through the hooks below as the objects are ported.
	*/
	class BatterymanPlayer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

	public:
		/** @brief Maximum energy (`hhstroom` cheat value; recharge stops here) */
		static constexpr std::int32_t MaxEnergy = 7000;
		/** @brief Width of the game view - the original's whole 640×480 screen, which its camera is laid out in */
		static constexpr std::int32_t ViewWidth = 640;
		/** @brief Height of the game view */
		static constexpr std::int32_t ViewHeight = 480;

		/**
		 * @brief What the original keeps in the player struct from one level to the next (teardown/44 §2.5)
		 *
		 * `FUN_004094b0` fills it at a new game; every level start (`FUN_00409510`) takes it over and resets only the
		 * per-level fields; a level exit writes it back.
		 */
		struct SessionState {
			bool Started = false;
			std::int32_t Lives = 0;				// +0x118
			std::int32_t Batteries = 0;			// +0x140
			std::int32_t Superbatteries = 0;	// +0x144
			std::int32_t Empties = 0;			// +0x148
			std::int32_t Facing = 1;			// +0x45
			std::int32_t FreeMove = 0;			// +0x70 (hhklm)
			std::int32_t Smoking = 0;			// +0x1dc (hhsmokin)
			std::int32_t LevelsFinished = 0;	// +0x1d8
			std::int32_t CheatCount = 0;		// +0x1d4
		};
		/** @brief The session's carried-over player state */
		static SessionState& Session();
		/** @brief `FUN_004094b0` - a new game: 5 lives, 15 batteries, no superbatteries or empties, facing right */
		static void BeginNewGame();
		/** @brief `FUN_0041b450(0)` - reads `SAVEGAME.000`'s player and level name; false if there is none */
		static bool PrepareLoadGame(LevelInitialization& levelInit);
		/** @brief `FUN_0041b240(0)` - writes `SAVEGAME.000` */
		void SaveGame();

		BatterymanPlayer();
		~BatterymanPlayer();

		bool OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType) override;
		/** @brief The engine's own carry-over (lives, score, ammo) is not used, @ref Session() is */
		void ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver) override;

		/** @brief Returns the energy meter value (≤ @ref MaxEnergy; below 1 means drained) */
		std::int32_t GetEnergy() const {
			return _energy;
		}
		/** @brief Returns the number of full batteries carried */
		std::int32_t GetBatteries() const {
			return _batteries;
		}
		/** @brief Returns the number of empty batteries carried */
		std::int32_t GetEmpties() const {
			return _empties;
		}
		/** @brief Returns the number of superbatteries collected */
		std::int32_t GetSuperbatteries() const {
			return _superbatteries;
		}
		/** @brief Returns `true` while the boss fight is active, which hides the battery panels */
		bool IsBossMode() const;
		/** @brief Returns the current pose (= set-13 animation index) */
		std::int32_t GetPose() const {
			return _pose;
		}
		/**
		 * @name The player record, for the objects
		 *
		 * The original's objects read and write the player struct (`0x4c55e0`) directly; these are its fields, named
		 * by offset, for the object ports.
		 */
		/** @{ */
		std::int32_t& X16() { return _x; }						// +0x0c
		std::int32_t& Y16() { return _y; }						// +0x10
		std::int32_t& VX16() { return _vx; }					// +0x14
		std::int32_t& VY16() { return _vy; }					// +0x18
		std::int32_t& SpawnX16() { return _spawnX; }			// +0x04, respawn point
		std::int32_t& SpawnY16() { return _spawnY; }			// +0x08
		std::int8_t GetFacing() const { return (std::int8_t)_facing; }	// +0x45
		std::uint32_t GetFrameHandle() const { return _frameHandle; }	// +0x28
		std::int32_t& Energy() { return _energy; }				// +0x60
		std::int32_t& Lives() { return _lives; }				// +0x118
		std::int32_t& Batteries() { return _batteries; }		// +0x140
		std::int32_t& Superbatteries() { return _superbatteries; }	// +0x144
		std::int32_t& Empties() { return _empties; }			// +0x148
		std::int32_t& GrabbedBySlot() { return _grabbedBySlot; }	// +0x150, object slot or 0
		std::int32_t& HazardSlot() { return _hazardSlot; }		// +0x154
		std::int32_t& RechargeSlot() { return _rechargeSlot; }	// +0x158
		std::int32_t& TransporterTicks() { return _transporter; }	// +0x10c
		std::int32_t& HurtTimer() { return _hurtTimer; }		// +0x11c
		std::int32_t& InWaterSlot() { return _inWaterSlot; }	// +0x138
		std::int32_t& WaterBelowSlot() { return _waterBelowSlot; }	// +0x13c
		std::int32_t& Ax16() { return _ax; }					// +0x1c
		std::int32_t& PushSlot() { return _pushSlot; }			// +0x50
		std::int32_t& HeadBumpSlot() { return _headBumpSlot; }	// +0x14c
		std::int32_t& Boost() { return _boost; }				// +0x15c
		std::int32_t& RideSlot() { return _rideSlot; }			// +0x160
		std::int32_t& RideType() { return _rideType; }			// +0x164
		std::int32_t& RideOffX() { return _rideOffX; }			// +0x16c
		std::int32_t& RideOffY() { return _rideOffY; }			// +0x170
		std::int32_t& ExtVX() { return _extVX; }				// +0x174
		std::int32_t& ExtVY() { return _extVY; }				// +0x178
		std::int32_t& Smoking() { return _smoking; }			// +0x1dc
		std::int32_t& Frame() { return _frame; }				// +0x3c
		std::int32_t GetLatchedPose() const { return _poseAtTickStart; }	// +0x41, the pose at the start of the tick
		std::int32_t GetInputH() const { return _inputH; }		// +0x1d0 low nibble
		bool IsActionHeld() const { return _inputAction; }		// +0x1d1 & 2
		/** @brief `FUN_0041fcd0` - the real-time clock in ms */
		std::uint32_t GetClockMs() const { return (std::uint32_t)_clockMs; }
		bool IsAirborne() const { return _airborne; }			// +0xb0
		/** @brief Camera top-left, 16.16 (`DAT_004c5768` / `DAT_004c576c`) */
		std::int32_t GetCameraX16() const { return _camX; }
		std::int32_t GetCameraY16() const { return _camY; }
		/** @brief The original's tick counter `_DAT_004b11c0`, 0 at level start */
		std::uint32_t GetTickCount() const { return _tick; }
		/** @brief `FUN_00409880(player, pose, flags)`, as objects call it */
		void SetPoseFromObject(std::int32_t pose, std::uint32_t flags) {
			SetPose(pose, flags);
		}
		/** @} */

		/** @brief `FUN_00418ea0(x, y, id, volume, freq)` for what has no object of its own (particles); `x == 0` is non-positional */
		void PlayPositionalSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq);

		/** @brief A key-down event for the key queue of `FUN_0040f570` (the cheats' key codes, the Pause key) */
		void OnKeyEvent(nCine::Keys key);
		/** @brief Typed characters for the character queue of `FUN_0040f570` (the cheats) */
		void OnTypedText(StringView text);
		/** @brief `DAT_004d58e4` - paused: no ticks, "PAUZE" over the frozen game */
		bool IsPaused() const { return _paused; }
		/** @brief `FUN_0041fd20` - the real-time 70 Hz clock */
		std::uint32_t GetClock70() const { return (std::uint32_t)(_clockMs * 0.07f); }

		/** @brief Returns `true` once the camera has run (the first tick), before that the view has no position yet */
		bool IsCameraReady() const {
			return _tick != 0;
		}
		/** @brief Returns the top-left corner of the view in whole pixels, as the original's camera places it */
		Vector2i GetCameraView() const {
			// The renderer takes `camX >> 16` (FUN_0040e080); the camera is never negative
			return Vector2i(_camX >> 16, _camY >> 16);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		/** @brief 1 px in the original's 16.16 fixed point */
		static constexpr std::int32_t One = 0x10000;
		/** @brief The original's logic rate */
		static constexpr float TicksPerSecond = 70.0f;
		/** @brief Metadata state of pose 0 (`Interactive/PlayerBatteryman.res`: state = base + pose) */
		static constexpr std::uint32_t PoseStateBase = 2000;
		/** @brief Number of poses (set-13 animations) */
		static constexpr std::int32_t PoseCount = 75;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Position and motion, 16.16 fixed point (+0x0c/+0x10, +0x14/+0x18, +0x1c)
		std::int32_t _x, _y;
		std::int32_t _vx, _vy;
		std::int32_t _ax;
		std::int32_t _spawnX, _spawnY;		// +0x04/+0x08
		std::int32_t _facing;				// +0x45, +1 / -1

		// Pose and animation
		std::int32_t _pose;					// +0x58
		std::int32_t _poseAtTickStart;		// +0x41
		std::int32_t _animPose;				// +0x34, pose whose animation the frame belongs to (latched)
		std::uint32_t _frameHandle;			// +0x28
		std::int32_t _prevPose;				// +0x47
		std::int32_t _frame;				// +0x3c
		std::int32_t _delay;				// +0xf8, ticks - 1 per frame
		std::int32_t _frameTick;			// +0xfc
		bool _loop;							// +0x100
		std::int32_t _animDone;				// +0x104, times the end of the animation was reached
		bool _standing;						// +0x5c
		std::int32_t _idleChoice;			// +0xd8
		std::int32_t _boredTicks;			// +0xd4
		std::int32_t _lookOffset;			// +0x7c, px
		std::int32_t _shownPose;

		// Probe results of the last physics step
		bool _airborne;						// +0xb0, floor line free
		bool _headClear;					// +0xac, ceiling line free
		bool _leftFree, _rightFree;			// +0xb4/+0xb8, wall columns free
		bool _atLedge;						// +0xbc

		// Economy and timers
		std::int32_t _energy, _prevEnergy;	// +0x60, +0x64
		std::int32_t _batteries;			// +0x140
		std::int32_t _superbatteries;		// +0x144
		std::int32_t _empties;				// +0x148
		std::int32_t _hurtTimer;			// +0x11c
		std::int32_t _blink;				// +0x124, counts up to 0 from -210

		// Object links: pool slots of the objects holding the player, 0 = none (set by the objects)
		std::int32_t _rechargeSlot;			// +0x158, charging port
		std::int32_t _grabbedBySlot;		// +0x150
		std::int32_t _hazardSlot;			// +0x154
		std::int32_t _transporter;			// +0x10c
		std::int32_t _inWaterSlot;			// +0x138, the water volume the player is in
		std::int32_t _waterBelowSlot;		// +0x13c, the water volume just below
		std::int32_t _ay;					// +0x20
		std::int32_t _pushSlot;				// +0x50, solid object being pushed against
		std::int32_t _freeMove;				// +0x70, free movement (no collision)
		std::int32_t _launch;				// +0x108, launch ticks (no gravity, higher top speed)
		std::int32_t _headBumpSlot;			// +0x14c, object the head bumped
		std::int32_t _boost;				// +0x15c, the belt's launch boost ticks
		std::int32_t _rideSlot;				// +0x160, object ridden
		std::int32_t _rideType;				// +0x164, 1 = stands on it, 2 = also carried horizontally
		std::int32_t _rideOffX;				// +0x16c, offset on the ridden object
		std::int32_t _rideOffY;				// +0x170
		std::int32_t _extVX, _extVY;		// +0x174/+0x178, external speed (belt)
		std::int32_t _smoking;				// +0x1dc, smoke around the player

		// Input of the current tick (+0x1d0/+0x1d1)
		std::int32_t _inputH, _inputV;
		bool _inputJump, _inputAction;

		// Fixed-rate clock
		float _tickAccumulator;
		std::uint32_t _tick;				// DAT_004b11c0
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		std::int32_t _activateTick = 0;		// development aid, BATTERYCHECK_ACTIVATE_TICK
		void ApplyDevActivation();
#endif
		float _clockMs;						// FUN_0041fcd0, drives the spawn blink
		bool _levelExitTaken;
		std::int32_t _tracedPose;

		// Sustained sounds (FUN_00418f90): kept playing while re-requested every tick, stopped after the first
		// tick without a request, as the original's mixer does with channel states 6/7/8
		struct HeldSound {
			std::shared_ptr<AudioBufferPlayer> Player;
			StringView Id;
			bool Requested = false;
		};
		HeldSound _poseSound;				// +0xe4, the pose's loop (water, hazard)
		HeldSound _fallSound;				// +0xe8, the falling whistle
		std::int32_t _fallTicks;			// +0xec, ticks of falling faster than 4 px/tick

		// Camera (FUN_00401000), 16.16 world coordinates of the view's top-left corner
		std::int32_t _camX, _camY;			// +0x188/+0x18c
		std::int32_t _camPrevX, _camPrevY;	// +0x190/+0x194
		std::int32_t _camSnap;				// +0x1a0, updates left without smoothing
		std::int32_t _leftLimit;			// +0x1a8, lowest X of the player (and of the camera, see below)
		std::int32_t _camLeft;				// +0x1ac, the camera's left limit, creeping toward _leftLimit

		// The level's objects
		std::unique_ptr<BatteryWorld> _world;

		// FUN_0040f570's queues and state
		SmallVector<std::uint16_t, 0> _keyEvents;	// FUN_0041fa20, as the original's virtual-key codes
		SmallVector<char, 0> _typedChars;			// FUN_0041f9f0
		char _cheat[33];							// DAT_004af520 (32 bytes there, no bound check)
		std::int32_t _cheatLength;					// DAT_004af540
		bool _paused;								// DAT_004d58e4
		bool _pausePressed;							// a press of the Pause key not yet seen by a tick
		bool _actionLatch;							// DAT_00452280: the action that unpaused, until released
		std::int32_t _invulnerable;					// +0x120, toggled by hhjezus and never read (xrefs: 0x409546, 0x40f79f/b7)
#endif

		void Tick(bool lastTick);
		void ReadInput();
		/** @brief `FUN_00401000` - the camera, run before the physics and again after the controller */
		void UpdateCamera();
		/** @brief `FUN_0040af90` */
		void UpdatePhysics();
		/** @brief Probes of `FUN_0040af90` at the current position; `movingUp` makes one-way tiles passable */
		void Probe(bool movingUp);
		/** @brief `FUN_00409f00` */
		void UpdateController();
		/** @brief `FUN_00409880(player, pose, flags)` */
		void SetPose(std::int32_t pose, std::uint32_t flags);
		/** @brief `FUN_00409c00` */
		void PickLocomotion();
		/** @brief `FUN_00409e30` */
		void TryRespawn(std::int32_t requireDone);
		/** @brief The input block of `FUN_00409f00` */
		void UpdateInput();
		/** @brief The transporter branch of `FUN_00409f00` */
		void UpdateTransporter();
		/** @brief Whether an object is a stamper (`LAB_00405db0` / `LAB_004029f0`, events 0x2b / 0x2c) */
		static bool IsStamper(BatteryObject* obj);
		/** @brief `FUN_0040bbe0` - whether the walk-backwards fidget (pose 0x32) has room behind the player */
		bool CanWalkBackwards();

		std::int32_t GetPoseFrameCount(std::int32_t pose);
		bool IsRowSolid(std::int32_t x0, std::int32_t x1, std::int32_t y, bool movingUp);
		bool IsColumnSolid(std::int32_t x, std::int32_t y, std::int32_t length);
		void SyncActor();

		/** @brief `FUN_00418ea0(x, y, id, volume, 0)` - a one-shot sample; volume 1..63, 0 is the default 63 */
		void PlaySample(StringView id, std::int32_t volume);
		/** @brief `FUN_00418f90(x, y, id, volume, freq, handle)` - a looped sample at a playback rate (0 = native) */
		void HoldSample(HeldSound& sound, StringView id, std::int32_t volume, std::int32_t freq);
		/** @brief Stops the sustained sounds that were not requested this tick */
		void ReleaseHeldSounds();
		/** @brief `FUN_0040f570` after `FUN_0040f2a0`: the Pause toggle, and one key event and one character for the cheats */
		void PollKeys();
		/** @brief Enters or leaves the pause, silencing the audio meanwhile */
		void SetPaused(bool paused);
		/** @brief The effect of cheat word `index` of the table at `0x44a9b8` */
		void RunCheat(std::int32_t index);
		/** @brief Writes what carries over into @ref Session() */
		void StoreSession();
		/** @brief Takes the player and the objects from a savegame being loaded; false if none is */
		bool RestoreSavedGame();
		/** @brief `DAT_004daba8 = 6` with the header's NextLevel - the end tile and `hhvolgende` */
		void LeaveLevel();
	};
}
