#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/StrTools.h"
#include "Packages/Engine/Resources/Level/ULevel.h"

// HP2's tick order (docs/re/hp2/engine.md#tick-order): Actor.TickParent and TickParent2 (HP2 only) name actors that tick
// before this one, ahead of its Owner. CutScript sets the camera's TickParent2 to itself so the cutscene ticks before
// the camera; BaseCamTarget, FlyToController and HPawn's FlyTo use TickParent.

namespace HP2
{
	namespace
	{
		struct TickProps
		{
			PropertyDataOffset TickParent, TickParent2, bCanMoveInSpecialPause, bInSpecialPause;
		};

		const TickProps& Props()
		{
			static TickProps p;
			static bool initialized = false;
			if (!initialized)
			{
				UClass* actor = engine->packages->FindClass("Engine.Actor");
				for (UProperty* prop : actor->Properties)
				{
					if (prop->Name == NameString("TickParent"))
						p.TickParent = prop->DataOffset;
					else if (prop->Name == NameString("TickParent2"))
						p.TickParent2 = prop->DataOffset;
				}
				p.bCanMoveInSpecialPause = actor->GetPropertyDataOffset("bCanMoveInSpecialPause");
				p.bInSpecialPause = actor->GetPropertyDataOffset("bInSpecialPause");
				initialized = true;
			}
			return p;
		}

		UActor*& TickParent(UActor* actor) { return actor->Value<UActor*>(Props().TickParent); }
		UActor*& TickParent2(UActor* actor) { return actor->Value<UActor*>(Props().TickParent2); }

		// ULevel +4380 in the original: the level that is in the special pause, if any
		ULevel* specialPauseLevel = nullptr;
	}

	// IDA Engine.dll: ?Tick@AActor@@UAEHMW4ELevelTick@@@Z [HP2 0x103C0E10] (its start: who ticks before the actor)
	UActor* TickBefore(UActor* actor, int i)
	{
		if (i == 0)
			return TickParent(actor);
		if (i == 1)
			return TickParent2(actor);
		// The owner, unless the actor is the owner's own tick parent (it would tick the actor first, and so on)
		UActor* owner = actor->Owner();
		if (owner && TickParent(owner) != actor && TickParent2(owner) != actor)
			return owner;
		return nullptr;
	}

	// IDA Engine.dll: ?DestroyActor@ULevel@@UAEHPAVAActor@@H@Z [HP2 0x103B0D20] (the loop over the level's actors)
	void ClearTickParents(UActor* destroyed)
	{
		for (UActor* actor : destroyed->XLevel()->Actors)
		{
			if (!actor)
				continue;
			if (TickParent(actor) == destroyed)
				TickParent(actor) = nullptr;
			if (TickParent2(actor) == destroyed)
				TickParent2(actor) = nullptr;
		}
	}

	// IDA Engine.dll: ?InputEvent@UGameEngine@@UAEHPAVUViewport@@W4EInputKey@@W4EInputAction@@M@Z [HP2 0x103AC4C0] (its start)
	// Delete pressed with [HGame.baseConsole] bDebugMode True turns the level's special pause on or off. The key goes on
	// to the console and its bindings as usual. HP1's InputEvent has no such test (its Actor has the same two flags).
	void InputEvent(int key, int action)
	{
		if (key != IK_Delete || action != IST_Press)
			return;
		if (!StrTools::equals_ignore_case(engine->packages->GetIniValue("System", "HGame.baseConsole", "bDebugMode"), "True"))
			return;
		specialPauseLevel = specialPauseLevel == engine->Level ? nullptr : engine->Level;
	}

	// IDA Engine.dll: ?Tick@AActor@@UAEHMW4ELevelTick@@@Z [HP2 0x103C0E10] (after the tick parents and the bTicked test)
	// In the special pause a PlayerPawn with a Player still reads its input (PlayerInput), and only an actor with
	// bCanMoveInSpecialPause ticks, with bInSpecialPause set (BaseCam then flies free on the player's input); outside it
	// bInSpecialPause is cleared. False: the actor doesn't tick.
	bool SpecialPauseTick(UActor* actor, float elapsed)
	{
		BitfieldBool inPause = actor->BoolValue(Props().bInSpecialPause);
		if (!specialPauseLevel || actor->XLevel() != specialPauseLevel)
		{
			inPause = false;
			return true;
		}
		if (UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(actor))
			player->PausedInput(elapsed);
		if (!actor->BoolValue(Props().bCanMoveInSpecialPause))
			return false;
		inPause = true;
		return true;
	}
}
