#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Batteryman
{
	/**
		@brief Placeholder for a BatteryCheck ("Batteryman") custom object

		First port-milestone stub for the ~11 Batteryman object families (BATTERIJ, FREDDY, ZUIGER,
		STAMPER, ZWARESTAMPER, WATER, PLATFORM, PICKUPS, PARAPLU, KLEINEPOMP, BAASOGEN). BatteryCheck's
		object layer is compiled C++ in @cpp "Battery.exe" @ce (see @ref Compatibility reverse-engineering,
		`BATTERY-EXE.md`), so it cannot be loaded as data - it has to be reimplemented as native actors.

		This stub spawns an inert, collision-less, invisible actor so a BatteryCheck level loads and renders
		without its custom event IDs (which reuse the stock 0x20-0x4F range) crashing or mis-behaving as
		stock JJ2 pickups. The object *positions* come from the `.j2l` event layer; real per-object behaviour
		(energy meter, battery swap, water short-circuit, crushers, the boss, ...) is ported from the
		Phase C/D findings and will replace this. One class backs every family - the concrete family is
		carried in @ref Actors::ActorActivationDetails::Type.
	*/
	class StubObject : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		StubObject();

		/** @brief Preloads all assets required by this actor (none yet) */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
	};
}
