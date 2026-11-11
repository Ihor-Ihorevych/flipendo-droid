#include "Precomp.h"
#include "HP1Mods.h"
#include "KWCheck.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Package/PackageManager.h"
#include "VM/ExpressionValue.h"
#include "Engine.h"

// Fire crab ledge fix (HP2, on by default; docs/re/hp2/original_bugs.md, issue #22). hgame.firecrab.OnALedge(Loc)
// calls a spot a ledge when FastTrace finds nothing in the 35 units below it, and FastTrace sees only the level's BSP
// (HP2 Engine.dll execFastTrace: UModel::FastLineCheck), not movers. On a lift floor (Adv6Goyle's crab trap room)
// every spot is a ledge to a flipped crab: DoFlip goes to FallOverLedge, pushDirection (16 OnALedge probes) picks a
// direction once, and the crab slides on its back to the far wall and never turns over. The replacement traces the
// same line against the level and movers, so a mover floor is floor.
namespace HP1::Mods
{
	namespace
	{
		UClass* FireCrabClass = nullptr;

		// function bool OnALedge(Vector Loc): args[0] Loc, args[1] the return value
		void NOnALedge(UObject* self, ExpressionValue* args)
		{
			UActor* crab = UObject::Cast<UActor>(self);
			vec3 loc = args[0].ToVector();
			KW::CheckResult hit;
			bool nothingBelow = KW::SingleLineCheck(hit, crab, loc + vec3(0.0f, 0.0f, -35.0f), loc, KW::TRACE_Level | KW::TRACE_Movers, vec3(0.0f));
			args[1] = ExpressionValue::BoolValue(nothingBelow);
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
			WrapScriptFunction(cls, nullptr, "OnALedge", &NOnALedge);
		}
	}
}
