#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Package/PackageManager.h"
#include "VM/ExpressionValue.h"
#include "VM/Frame.h"
#include "Engine.h"
#include <unordered_map>

// Fire crab ledge fix (HP2, on by default; docs/re/hp2/original_bugs.md, issues #22 and #26). hgame.firecrab.OnALedge
// calls a spot a ledge when FastTrace finds nothing in the 35 units below it, and FastTrace sees only the level's BSP,
// not movers. A crab flipped on a mover floor therefore goes to FallOverLedge and stays on its back for good, as in the
// original; Ch1Rictusempra's lift pit puzzle is built on that (the crab on Mover16 stays flipped while Harry pushes it
// into the pit). What breaks is pushDirection: its 16 OnALedge probes find BSP only where they reach past a side wall,
// so near a wall the crab gets pushed away from it and slides on its back to the far wall (Adv6Goyle's trap room).
// The fix keeps OnALedge as it is and makes pushDirection return (0,0,-1) for a crab standing on a mover, which is what
// the original returns in the middle of such a floor: the crab lies where it landed. It also stops a pushed crab from
// shrinking below its lying radius (NPushDirection).
namespace HP1::Mods
{
	namespace
	{
		UClass* FireCrabClass = nullptr;
		UFunction* OriginalPushDirection = nullptr;
		// Each small crab's radius the first time it lay down on a ledge, for the level they are in
		std::unordered_map<UActor*, float> FullRadius;
		void* FullRadiusLevel = nullptr;

		// function Vector pushDirection(): no parameters, so args[0] is the return value (args holds the Parm properties)
		void NPushDirection(UObject* self, ExpressionValue* args)
		{
			UActor* crab = UObject::Cast<UActor>(self);
			// DoFlip.Tick calls this, then shrinks the crab by 10 (SetCollisionSize(CollisionRadius - 10)) and enters
			// FallOverLedge. Only firecrab.Landed of a large crab gives the size back, so each Flipendo push of a small
			// crab lying on a ledge took another 10 off: 18, 8, -2, and a crab with a negative radius can't be targeted
			// or hit (Ch1Rictusempra's lift pit, firecrabSmall6 after its first push). Back to the size it had the
			// first time it got here (the map's radius, as the crab walked), so it always lies 10 smaller.
			if (crab->GetStateName() == NameString("DoFlip"))
			{
				if (engine->Level != FullRadiusLevel)
				{
					FullRadius.clear();
					FullRadiusLevel = engine->Level;
				}
				auto it = FullRadius.find(crab);
				if (it == FullRadius.end())
					FullRadius[crab] = crab->CollisionRadius();
				else if (crab->CollisionRadius() != it->second)
					crab->SetCollisionSize(it->second, crab->CollisionHeight());
			}
			ExpressionValue result;
			if (crab->ActorBase() && UObject::TryCast<UMover>(crab->ActorBase()))
				result = ExpressionValue::VectorValue(vec3(0.0f, 0.0f, -1.0f));
			else
				result = Frame::Call(OriginalPushDirection, self, ArrayView<ExpressionValue>());
			args[0] = result;
		}
	}

	void TickFireCrabLedgeFix()
	{
		static const bool on = IsOn("firecrab-ledge-fix");
		if (!on)
			return;
		UClass* cls = engine->packages->FindClass("hgame.firecrab");
		if (cls && cls != FireCrabClass)
		{
			FireCrabClass = cls;
			OriginalPushDirection = WrapScriptFunction(cls, nullptr, "pushDirection", &NPushDirection);
		}
	}
}
