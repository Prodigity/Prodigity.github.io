#pragma once

#include "BatteryObject.h"

namespace Jazz2::Actors::Batteryman
{
	/** @brief Pickups - events 0x3c (life), 0x3e (battery), 0x3f (superbattery), 0x45 (`FUN_00406f10`, teardown/41 §2) */
	class Pickup : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);

	public:
		void Behave() override;
	};

	/** @brief Battery receptacle, the power switch of a link id - event 0x21 (`LAB_00406890`, teardown/41 §3) */
	class Receptacle : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);

	public:
		void Behave() override;
	};

	/** @brief Plasma lamp: makes its receptacle a checkpoint and keeps it powered - event 0x3d (`LAB_00406d40`, §4) */
	class PlasmaLamp : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);

	public:
		void Behave() override;
	};

	/** @brief Charging port - event 0x2a (`LAB_00403fd0`, teardown/41 §5) */
	class ChargingPort : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);

	public:
		void Behave() override;
	};

	/** @brief Ambient sound emitter - event 0x01 with parameter bit 0 (`LAB_00401ee0`, teardown/41 §6) */
	class SoundMarker : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);

	public:
		void Behave() override;
	};
}
