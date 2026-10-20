#pragma once

#include "BatteryObject.h"
#include "BatteryParticles.h"

#include <memory>

namespace Jazz2
{
	class ILevelHandler;
}

namespace Jazz2::Actors::Batteryman
{
	class BatterymanPlayer;

	/**
		@brief BatteryCheck's object layer for one level

		The pool and its order (`FUN_00407670`, `FUN_004096e0`, `FUN_004097c0`, `FUN_00409650`), the level's event
		grid as the original holds it (`DAT_00470f74`, from the converted level's `.bcev`), spawning every object at
		level start (`FUN_00408ad0` / `FUN_00408a30` - nothing is spawned or culled by distance later) and the
		per-object step before each behaviour (`FUN_004095a0`). The player's tick drives it, after the player, as
		`FUN_004015b0` does.
	*/
	class BatteryWorld
	{
	public:
		/** @brief Number of pool slots (`DAT_004b00e4`); slot 0 is never used */
		static constexpr std::int32_t PoolSize = 512;

		BatteryWorld(ILevelHandler* levelHandler, BatterymanPlayer* player);
		~BatteryWorld();

		BatteryWorld(const BatteryWorld&) = delete;
		BatteryWorld& operator=(const BatteryWorld&) = delete;

		ILevelHandler* GetLevelHandler() const {
			return _levelHandler;
		}
		BatterymanPlayer* GetPlayer() const {
			return _player;
		}

		/** @brief Loads the level's original event words; returns `false` if the level has none */
		bool LoadEvents(StringView levelName);
		/** @brief Event grid size in tiles (`DAT_004d82ec` × `DAT_004d82f0`) */
		Vector2i GetEventGridSize() const {
			return Vector2i(_eventsWidth, _eventsHeight);
		}
		/** @brief Whether a tile is in the part of the grid the original reads (all but the last column and row) */
		bool IsInEventGrid(std::int32_t tx, std::int32_t ty) const;
		/** @brief The original event word of a tile, 0 outside the grid */
		std::uint32_t GetEvent(std::int32_t tx, std::int32_t ty) const;
		/** @brief `FUN_00408910(tx, ty) + FUN_00408950(tx, ty, 0, 6)` - the link id of an event: 64·bits 8..9 + bits 12..17 */
		std::int32_t GetLinkId(std::int32_t tx, std::int32_t ty) const {
			return (std::int32_t)((GetEvent(tx, ty) >> 2) & 0xc0) + GetEventParam(tx, ty, 0, 6);
		}
		/** @brief `FUN_00408a00` - replaces the event word of a tile */
		void SetEvent(std::int32_t tx, std::int32_t ty, std::uint32_t value);
		/** @brief `FUN_004089d0` - the event type (low byte) of a tile */
		std::uint32_t GetEventType(std::int32_t tx, std::int32_t ty) const {
			return (GetEvent(tx, ty) & 0xff);
		}
		/** @brief `FUN_00408950(tx, ty, offset, bits)` - a parameter field of a tile's event, signed if `bits < 0` */
		std::int32_t GetEventParam(std::int32_t tx, std::int32_t ty, std::int32_t offset, std::int32_t bits) const;

		/** @brief `FUN_00408ad0` - spawns every object of the level, column after column */
		void SpawnAll();
		/**
		 * @brief The world's part of `FUN_0041b240` - every live object's record and the link tables
		 *
		 * The event map is not written: only the spawner changes it (bit 0x800), and a restore doesn't spawn.
		 */
		void Save(Death::IO::Stream& dest) const;
		/** @brief The world's part of `FUN_0041b450(0x100)` - in place of @ref SpawnAll() */
		bool Restore(Death::IO::Stream& src);
		/** @brief `FUN_004096e0(x, y, type, flags)` - spawns an object (16.16 position), returns its slot or 0 */
		std::int32_t Spawn(std::int32_t x, std::int32_t y, std::int32_t type, std::int32_t flags);
		/** @brief `FUN_004097c0(slot)` - frees a slot */
		void Destroy(std::int32_t slot);
		/** @brief Forgets a slot's object without destroying it (the object is going away on its own) */
		void Release(std::int32_t slot) {
			if (slot > 0 && slot < PoolSize) {
				_pool[slot] = nullptr;
			}
		}
		/** @brief The object in a slot, `nullptr` if the slot is free */
		BatteryObject* GetObject(std::int32_t slot) const {
			return (slot > 0 && slot < PoolSize ? _pool[slot] : nullptr);
		}
		/** @brief One past the last live slot (`DAT_004b00e0`) */
		std::int32_t GetLiveEnd() const {
			return _liveEnd;
		}

		/**
		 * @brief `FUN_00408c60(x, y, r², type, state)` - the slot of the nearest live object of an event type
		 *
		 * Distance in whole pixels (truncated toward zero) from a 16.16 point, squared, strictly less than `r²`;
		 * `state < 0` ignores the state. The first slot wins a tie; -1 if none.
		 */
		std::int32_t FindNearest(std::int32_t x, std::int32_t y, std::int32_t radiusSquared, std::int32_t type, std::int32_t state) const;
		/** @brief `FUN_00408da0(slot, r²)` - whether the player is within the radius (stores the distance, `_DAT_004b00ec`) */
		bool IsPlayerWithin(BatteryObject* obj, std::int32_t radiusSquared);
		/** @brief `_DAT_004b00ec` - the squared distance of the last @ref IsPlayerWithin() hit, else its radius */
		std::int32_t GetLastDistance() const {
			return _lastDistance;
		}

		/**
		 * @name Link-id tables (teardown/41 §1.5)
		 *
		 * The receptacles' power levels (`0x4b11e0[id]`, 255 = just powered, decays), their activation counters
		 * (`0x4d7dc0[id]`) and the slot of each id's receptacle (`0x4d7ee0[id]`, not cleared between levels).
		 */
		/** @{ */
		std::uint8_t& Trigger(std::int32_t id) { return _trigger[id & 0xff]; }
		std::uint8_t& ActivationCount(std::int32_t id) { return _activations[id & 0xff]; }
		std::int32_t& LinkOwner(std::int32_t id) { return _linkOwners[id & 0xff]; }
		/** @} */

		/** @brief `DAT_004c1408` - the boss body's slot while the boss fight is on, else 0 */
		std::int32_t& BossSlot() { return _bossSlot; }
		bool IsBossMode() const { return _bossSlot != 0; }

		/** @brief `DAT_004d58e8` - frames rendered since the level started (some animations count these, not ticks) */
		std::uint32_t GetRenderedFrames() const { return _renderedFrames; }
		void CountRenderedFrame() { _renderedFrames++; }

		/** @brief Metadata key of a global sample id (Battery.exe's table at 0x4498a8), empty if out of range */
		static StringView GetSampleName(std::int32_t id);
		/**
		 * @brief The mixer's volume and pan of a channel (`FUN_00419100`)
		 *
		 * `x == 0` is non-positional: the volume as given (0 = 63) and centred. Otherwise the volume drops by d²/16
		 * with d the distance to the player in 8-px units (none within d² < 17), and the pan is volume·dx/128 clamped
		 * to ±31, then 0..64 with 32 centred. A channel quieter than 4 does not start.
		 */
		void ComputeMix(std::int32_t x, std::int32_t y, std::int32_t volume, std::int32_t& outVolume, std::int32_t& outPan) const;

		/** @brief `DAT_00448030` - the 1024-entry sine table, 16.16 (`sin[256] = 0x10000`) */
		static std::int32_t Sine(std::int32_t index);

		/**
		 * @brief One draw of the original's PRNG (teardown/43 §7)
		 *
		 * A lagged XOR over the 0x104-byte buffer `DAT_004db4e0`, index `DAT_004db270`, reseeded with 0 at every level
		 * start (`FUN_00423bb0(0)`), so a level's stream depends only on the order of the draws - which follows the
		 * original's: player, objects in slot order, particles. Callers mask the result as the original does.
		 */
		std::uint32_t Random();
		/** @brief `FUN_00423bb0(seed)` */
		void SeedRandom(std::uint32_t seed);

		/**
		 * @brief The render layer of a sprite of the original's draw list (teardown/44 §3.6)
		 *
		 * The original draws tile layer L and then the sprites of layer L, for L = 7 down to 0; at layer 3 first the
		 * water tint (mode 4) and mode 10, then the normal sprites (objects, then the player), then every mode-9
		 * sprite of any layer, then the particles, then the second water-tint pass. Tile layer L is at depth
		 * 500 - d(L) here (the converter's own mapping, d = 100·(L - 3) compressed past -200 / 300).
		 */
		static std::uint16_t GetSpriteRenderLayer(std::int32_t layer, std::int32_t mode);
		/** @brief Render layer of the player's sprite (layer 3, after the objects' normal sprites) */
		static std::uint16_t GetPlayerRenderLayer();
		/** @brief Render layer of the particles */
		static std::uint16_t GetParticleRenderLayer();
		/** @brief Render layer of the water tint's passes (`FUN_0041ad70(.., 0)` before the sprites, `(.., 1)` after the particles) */
		static std::uint16_t GetTintRenderLayer(std::int32_t pass);
		/**
		 * @brief `DAT_004dac4c` / `DAT_004c5420` - the remap rows of the water's surface band and body
		 *
		 * Set by event 0x41, global and never reset (0 at program start = no tint).
		 */
		static std::uint8_t& WaterSurfaceRemap();
		static std::uint8_t& WaterBodyRemap();
		/** @brief The enqueue order of this frame's sprites (visit order within a render layer) */
		std::uint16_t NextSpriteSequence() {
			return _spriteSequence++;
		}

		/** @brief `FUN_00409650` - one tick of every object; `lastTick` is the rendered tick of a frame */
		void TickObjects(bool lastTick);
		/** @brief `FUN_00416fa0` - one tick of every particle (after the objects, as in `FUN_004015b0`) */
		void TickParticles();
		/**
		 * @brief `FUN_00414e50` - every animated tile shows entry `(ms · Speed / 1000) % length` of its list
		 *
		 * On the wall clock, re-evaluated on each game tick (and at level start), so tiles stand still in a pause.
		 */
		void UpdateTileAnimations(std::uint32_t ms);
		/** @brief The particles' draw bookkeeping, once per rendered frame (see @ref BatteryParticles::MarkDrawn()) */
		void MarkParticlesDrawn(std::int32_t camX, std::int32_t camY);
		BatteryParticles& GetParticles() {
			return *_particles;
		}

		/** @name Particle spawners (`FUN_004161b0`, `FUN_00416120`, `FUN_004162a0`, `FUN_00416360`, `FUN_004165d0`, `FUN_004166b0`, `FUN_00416780`, `FUN_00416850`) */
		/** @{ */
		void SpawnSmoke(std::int32_t x, std::int32_t y) { _particles->SpawnSmoke(x, y); }
		void SpawnSmokeAt(std::int32_t x, std::int32_t y) { _particles->SpawnSmokeAt(x, y); }
		void SpawnSparkle(std::int32_t x, std::int32_t y) { _particles->SpawnSparkle(x, y); }
		void SpawnSpark(std::int32_t x, std::int32_t y, std::int32_t p) { _particles->SpawnSpark(x, y, p); }
		void SpawnRainDrop(std::int32_t x, std::int32_t y) { _particles->SpawnRainDrop(x, y); }
		void SpawnDrip(std::int32_t x, std::int32_t y) { _particles->SpawnDrip(x, y); }
		void SpawnBubble(std::int32_t x, std::int32_t y, std::int32_t waterId) { _particles->SpawnBubble(x, y, waterId); }
		void Shatter(BatteryObject* obj, std::int32_t mode) { _particles->Shatter(obj, mode); }
		/** @} */

		/** @brief The particles' solid test: one pixel of the tile masks (16.16 point); outside the level is solid */
		bool IsPixelSolid(std::int32_t x, std::int32_t y) const;
		/** @brief `FUN_00407990(x, y)` - one pixel of the tile masks: 1 solid, 0 free, `0xfffffffe` outside the level */
		std::uint32_t TestPixel(std::int32_t x, std::int32_t y) const;
		/** @brief Whether the main layer has a tile (any, drawn or solid) at a tile position (the rain's sky test) */
		bool HasMainLayerTile(std::int32_t tx, std::int32_t ty) const;
		/**
		 * @brief `FUN_00407a40(x, n, y)` - a solid mask pixel in row `y` from `x` on, for `n` more pixels (whole px)
		 *
		 * Tile by tile; the event of every non-empty tile it reaches is latched (@ref GetProbeEvent()). As there, a
		 * row that ends exactly on a tile's last column leaves the following pixel out.
		 */
		bool ProbeRow(std::int32_t x, std::int32_t n, std::int32_t y);
		/** @brief `FUN_00407ba0(x, y, n)` - 1 + the offset of the first solid pixel in column `x` from `y`, n + 1 if none */
		std::int32_t ProbeColumn(std::int32_t x, std::int32_t y, std::int32_t n);
		/** @brief `DAT_004b00f0` - the event of the last non-empty tile a probe reached (0 after an off-map column) */
		std::uint32_t GetProbeEvent() const {
			return _probeEvent;
		}
		/** @brief `DAT_004d8300` - the last pixel row of the level, 16.16 */
		std::int32_t GetLevelMaxY16() const;

		/** @brief `FUN_00401da0(x, y)` - the first filled water volume (`LAB_00404c60`) holding a point, its slot or 0 */
		std::int32_t FindWaterAt(std::int32_t x, std::int32_t y) const;
		/** @brief `FUN_004083b0(slot)` - puts an object on the ground below it (first frame of `+0x34`) */
		void SnapToGround(BatteryObject* obj);
		/**
		 * @brief `FUN_0040b800(slot, x, y, w, h, type)` - an object's solid box against the player
		 *
		 * Head bump from below, side push (type 1), a stamper's crush, and standing on top - which makes the
		 * player ride the object (type 1 vertically, 2 also horizontally). The box's top-left is (x, y), 16.16.
		 */
		void SolidBox(BatteryObject* obj, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, std::int32_t type);
		/** @brief `FUN_0040b6b0(slot, w, h, type)` - a top-only platform `w` px wide whose top is `h` px above the object */
		void TopPlatform(BatteryObject* obj, std::int32_t w, std::int32_t h, std::int32_t type);
		/** @brief A sample at a 16.16 point (`x == 0`: non-positional), mixed as @ref ComputeMix() does */
		void PlayWorldSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq);

		/** @brief `0x4d7d20[id]` - the slot of each water volume id (64 entries) */
		std::uint16_t& WaterSlot(std::int32_t id) { return _waterSlots[id & 0x3f]; }
		/** @brief `DAT_00449830` - a drop's damage: 2 at program start, set by the rain (never reset) */
		static std::int32_t& DropDamage();
		/** @brief `DAT_004d8388 == 0` - whether the current tick is the last of a frame, the only one that draws */
		/** @brief Development trace of particle spawns (`BATTERYCHECK_TRACE` containing 0) */
		bool IsTracingParticles() const {
			return _tracedTypes[0];
		}

		bool IsDrawingTick() const {
			return _drawingTick;
		}

	private:
		struct Template {
			std::uint16_t AnimHandle;	// +0x34
			std::uint16_t AnimSet;		// +0x36
			std::uint8_t HitFlags;		// +0x3d
			std::uint32_t Class;		// +0x40
			bool Registered;			// +0x00 != 0
		};

		ILevelHandler* _levelHandler;
		BatterymanPlayer* _player;

		std::unique_ptr<std::uint32_t[]> _events;
		std::int32_t _eventsWidth, _eventsHeight;

		BatteryObject* _pool[PoolSize];
		std::int32_t _liveEnd;		// DAT_004b00e0
		std::int32_t _nextFree;		// DAT_004b00e8
		bool _drawingTick;
		std::int32_t _lastDistance;	// _DAT_004b00ec
		std::uint8_t _trigger[256];		// 0x4b11e0
		std::uint8_t _activations[256];	// 0x4d7dc0
		std::int32_t _linkOwners[256];	// 0x4d7ee0
		std::int32_t _bossSlot;			// DAT_004c1408
		std::uint32_t _renderedFrames;	// DAT_004d58e8
		std::uint16_t _spriteSequence;
		std::uint32_t _nextTraceTick = 0;
		bool _tracedTypes[256] = {};	// development trace, BATTERYCHECK_TRACE (desktop only)
		std::int32_t _traceLines = 0;
		std::uint8_t _random[0x104];	// DAT_004db4e0
		std::uint32_t _randomIndex;		// DAT_004db270
		std::uint16_t _waterSlots[64];	// 0x4d7d20
		std::uint32_t _probeEvent = 0;	// DAT_004b00f0
		std::unique_ptr<BatteryParticles> _particles;

		Template _templates[0x50];

		/** @brief `FUN_00407170` - the per-type templates */
		void RegisterTypes();
		/** @brief The tile animations of `FUN_004106a0`: each expanded at load into a flat list of entries */
		void SetupTileAnimations();

		struct TileAnimation {
			std::uint32_t Speed;		// entries per second
			std::uint32_t Length;		// the list length as the byte the original stores
		};
		SmallVector<TileAnimation, 0> _tileAnimations;
		/** @brief Creates the actor for an event type's behaviour */
		std::shared_ptr<BatteryObject> CreateObject(std::int32_t type);
		/** @brief Puts a created object into a pool slot and into the level */
		void AttachObject(std::shared_ptr<BatteryObject> obj, std::int32_t slot);
		/** @brief `FUN_004095a0` - the step before each behaviour */
		void PreUpdate(BatteryObject* obj);
		/** @brief `FUN_0040be70` - an object whose sprite overlaps the player's by more than 7 pixels is touched */
		void TouchPlayer(BatteryObject* obj);
	};
}
