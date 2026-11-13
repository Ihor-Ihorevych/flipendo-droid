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

// Fire crab ledge fix (HP2, on by default; docs/re/hp2/original_bugs.md, issues #22 and #26). hgame.firecrab.OnALedge
// calls a spot a ledge when FastTrace finds nothing in the 35 units below it, and FastTrace sees only the level's BSP,
// not movers. A crab flipped on a mover floor therefore goes to FallOverLedge and stays on its back for good, as in the
// original; Ch1Rictusempra's lift pit puzzle is built on that (the crab on Mover16 stays flipped while Harry pushes it
// into the pit). What breaks is pushDirection: its 16 OnALedge probes find BSP only where they reach past a side wall,
// so near a wall the crab gets pushed away from it and slides on its back to the far wall (Adv6Goyle's trap room).
// The fix keeps OnALedge as it is and makes pushDirection return (0,0,-1) for a crab standing on a mover, which is what
// the original returns in the middle of such a floor: the crab lies where it landed.
namespace HP1::Mods
{
	namespace
	{
		UClass* FireCrabClass = nullptr;
		UFunction* OriginalPushDirection = nullptr;

		// function Vector pushDirection(): no parameters, so args[0] is the return value (args holds the Parm properties)
		void NPushDirection(UObject* self, ExpressionValue* args)
		{
			UActor* crab = UObject::Cast<UActor>(self);
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
