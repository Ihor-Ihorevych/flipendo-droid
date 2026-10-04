#include "Precomp.h"
#include "KW.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"

// Touch between two actors (docs/re/engine/physics.md "Touch"). KnowWonder's AActor::BeginTouch links and notifies one
// side at a time: first the moving actor, then the actor it moved into. The second side is only skipped when the first
// side's Touch event broke the link again, not when it destroyed the first actor: ULevel::DestroyActor only unlinks the
// actors that list the destroyed one, and the second side doesn't yet. So a spell that explodes in its own Touch still
// touches the spellTrigger it hit (Lev_Tut1b's Flipendo wall symbol). SurrealEngine linked both sides up front, and the
// spell's Destroy unlinked the trigger before it was told.

namespace KW
{
	namespace
	{
		bool IsPawn(UActor* actor)
		{
			return UObject::TryCast<UPawn>(actor) != nullptr;
		}

		// Links Other into Actor's Touching array and raises Actor.Touch(Other). A full array drops an actor without
		// physics, else (only to make room for a pawn) a non-pawn, else (only for a player) a pawn that isn't one.
		// Returns false when Actor's Touch event removed Other from the slot it got.
		// IDA Engine.dll: not exported: sub_1037A0A0 [HP1 0x1037A0A0] (called twice by ?BeginTouch@AActor@@QAEXPAV1@@Z, assert "Actor!=Other" in UnActor.cpp)
		bool TouchOneSide(UActor* actor, UActor* other)
		{
			auto touching = actor->Touching();
			int slot = -1;
			for (int i = 0; i < UActor::TouchingArraySize; i++)
			{
				if (touching[i] == other)
					return true;
				if (!touching[i])
					slot = i;
			}

			if (slot == -1)
			{
				for (int i = 0; i < UActor::TouchingArraySize; i++)
				{
					if (touching[i] && touching[i]->Physics() == 0)
					{
						actor->UnTouch(touching[i]);
						slot = i;
					}
				}
			}
			if (slot == -1)
			{
				if (!IsPawn(other))
					return true;
				for (int i = 0; i < UActor::TouchingArraySize; i++)
				{
					if (touching[i] && !IsPawn(touching[i]))
					{
						actor->UnTouch(touching[i]);
						slot = i;
						break;
					}
				}
			}
			if (slot == -1)
			{
				if (!BoolProperty(other, "bIsPlayer"))
					return true;
				for (int i = 0; i < UActor::TouchingArraySize; i++)
				{
					if (touching[i] && !BoolProperty(touching[i], "bIsPlayer"))
					{
						actor->UnTouch(touching[i]);
						slot = i;
						break;
					}
				}
			}
			if (slot == -1)
				return true;

			touching[slot] = other;
			actor->TouchEventSent[slot] = true;
			CallEvent(actor, EventName::Touch, { ExpressionValue::ObjectValue(other) });
			return touching[slot] == other;
		}
	}

	// IDA Engine.dll: ?BeginTouch@AActor@@QAEXPAV1@@Z [HP1 0x10379FE0]
	void BeginTouch(UActor* actor, UActor* other)
	{
		if (!TouchOneSide(actor, other))
			return;
		TouchOneSide(other, actor);

		// The original keeps the destroyed actor in Other's array until ULevel::CleanupDestroyed clears references to
		// destroyed actors at the end of the tick, without UnTouch. SurrealEngine has no such pass: clear it now.
		if (actor->bDeleteMe())
		{
			auto touching = other->Touching();
			for (int i = 0; i < UActor::TouchingArraySize; i++)
			{
				if (touching[i] == actor)
				{
					touching[i] = nullptr;
					other->TouchEventSent[i] = false;
				}
			}
		}
	}
}
