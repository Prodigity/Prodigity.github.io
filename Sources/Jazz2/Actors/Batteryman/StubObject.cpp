#include "StubObject.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Batteryman
{
	StubObject::StubObject()
	{
	}

	void StubObject::Preload(const ActorActivationDetails& details)
	{
		// Nothing to preload yet - sprites/sounds are bound when real behaviour is ported from Phase C/D.
	}

	Task<bool> StubObject::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// Inert placeholder: no gravity, no collisions, no sprite. The level still populates with the
		// correct object *positions* from the .j2l event layer, which is the first-milestone goal; the
		// behaviour (and visuals) come later when each family is reimplemented.
		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects
			| ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		LOGD("Spawned BatteryCheck stub object (EventType {})", (std::int32_t)details.Type);

		async_return true;
	}
}
