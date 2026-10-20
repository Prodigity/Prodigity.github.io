#pragma once

#include "BatteryObject.h"

namespace Jazz2::Actors::Batteryman
{
	/**
		@name Freddy and the boss (teardown/43 §2-§6)

		Ported from the disassembly of their pointer-only routines (scratchpad g43 boss.asm / freddy.asm) as the
		spec quotes it. Fields are the original record's, by offset.
	*/
	/** @{ */

	/** @brief Event 0x35, FREDDY - eats the player's empty batteries, one battery back for every 3 (`LAB_004056e0`) */
	class Freddy : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x43 - an empty battery flying from the player into Freddy's mouth (`LAB_004055d0`) */
	class EmptyBatteryIcon : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x38, BAAS - the boss: gestures, volleys, and the shocks of the red button (`LAB_00403580`) */
	class BossBody : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x42 - the red button that shocks the boss with the receptacles' power (`LAB_00403090`) */
	class RedButton : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @brief Event 0x44 - the boss's projectile, aimed once at the player (`LAB_00403310`, class 2) */
	class BossProjectile : public BatteryObject
	{
		DEATH_RUNTIME_OBJECT(BatteryObject);
	public:
		void Behave() override;
	};

	/** @} */
}
