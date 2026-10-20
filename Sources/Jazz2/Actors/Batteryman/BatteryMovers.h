#pragma once

#include "BatteryObject.h"

namespace Jazz2::Actors::Batteryman
{
	/**
		@name Moving objects, platforms and hazards (teardown/42)

		Each is the behaviour routine of its event type, ported statement by statement from the decompile named in
		its description. Fields are the original record's, by offset.
	*/
	/** @{ */

	/** @brief Event 0x23, DRIJVER - a raft floating on a water volume (`LAB_00404fd0`) */
	class FloatRaft : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Events 0x25 / 0x29 - the pump that fills one water volume while it drains another (`LAB_00405100`) */
	class Pump : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x2e - a water volume, its wavy surface, bubbles and the player's in-water links (`LAB_00404c60`) */
	class WaterVolume : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;

	protected:
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		// The tint entry of the last drawing tick (FUN_0040e210(X, Y, 4, 4, slot, 0)): the surface's view position
		bool _tintQueued = false;
		std::int32_t _tintX = 0, _tintY = 0;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _spanCommands;

		/** @brief `FUN_0041ad70(sx, sy, slot, pass)` - the rows of one pass as remapped spans */
		void DrawTint(RenderQueue& renderQueue, std::int32_t& used, std::int32_t pass);
		void DrawSpan(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::int32_t width,
			std::uint8_t remap, std::uint16_t layer, std::uint16_t order);
	};

	/** @brief Event 0x26, LIFT - a vertical lift started by its switch (`LAB_00406530`) */
	class Lift : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x28, PLATFORM - a horizontal platform started by its switch, tilting as it moves (`LAB_004061c0`) */
	class SwayPlatform : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Events 0x2b STAMPER (`LAB_00405db0`) and 0x2c ZWARESTAMPER (`LAB_004029f0`) - the crushers */
	class Stamper : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		explicit Stamper(bool heavy) : _heavy(heavy) {}
		void Behave() override;

	private:
		bool _heavy;	// ZWARESTAMPER
		/** @brief `FUN_00402940(slot)` - every sucker right under the stamper bursts */
		void CrushSuckers();
	};

	/** @brief Event 0x2f, BAND - a conveyor belt run by its switch (`FUN_00405aa0`) */
	class ConveyorBelt : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x30, ZUIGER - the suction tube that carries the player up while its id is active (`LAB_00405310`) */
	class SuctionTube : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x32 - a dripping point (`LAB_004059f0`) */
	class DripEmitter : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x36 - the rain over the view, and the drops' damage (`LAB_00405890`) */
	class RainGenerator : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x3b - an inert marker that goes away on its first tick (`LAB_00404fc0`) */
	class InertMarker : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x41 - sets the water's remap rows, then goes away (`LAB_00404f50`) */
	class WaterColour : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x46 - now and then a burst of sparks near the player (`LAB_00401e10`) */
	class SparkEmitter : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x37, BAASOGEN - an eye whose pupil follows the player (`LAB_00403e40`) */
	class BossEye : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x3a, PARAPLU - a moving umbrella, a shelter from the rain only (`LAB_00402db0`) */
	class Umbrella : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Events 0x33 SUCKER (`LAB_00401f50`) and 0x34 BIGSUCKER (`LAB_00404250`) - the biting enemies */
	class Sucker : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		explicit Sucker(bool big) : _big(big) {}
		void Behave() override;

	private:
		bool _big;
		std::uint8_t _tracedState = 0;
	};

	/** @} */
}
