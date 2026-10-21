#include "Precomp.h"
#include "HP2.h"
#include "KWActor.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include <cmath>

// HP2's stair smoothing (docs/re/hp2/engine.md#stair-smoothing): when a walking pawn's physics moves it up or down by
// about Pawn.StepHeight in one tick, Actor.SavedPrePivotZ holds its skeletal mesh back by that height (GetMeshCoords
// adds it to PrePivot.Z) and gives it back as the pawn moves on, StepHeight per StepWidth of horizontal movement. The
// body glides over the stairs instead of jumping a step at a time.

namespace HP2
{
	namespace
	{
		struct StepProps
		{
			PropertyDataOffset StepHeight, StepWidth;
		};

		const StepProps& Props()
		{
			static StepProps p;
			static bool initialized = false;
			if (!initialized)
			{
				UClass* pawn = engine->packages->FindClass("Engine.Pawn");
				p.StepHeight = pawn->GetPropertyDataOffset("StepHeight");
				p.StepWidth = pawn->GetPropertyDataOffset("StepWidth");
				initialized = true;
			}
			return p;
		}
	}

	// IDA Engine.dll: not exported: sub_103F29A0 [HP2 0x103F29A0], called (through the thunk sub_10302CFC) by
	// ?performPhysics@APawn@@UAEXM@Z right after its physics switch
	void SmoothSteps(UPawn* pawn)
	{
		float& offset = KW::SavedPrePivotZ(pawn);
		if (pawn->Physics() != PHYS_Walking)
		{
			offset = 0.0f;
			return;
		}

		float stepHeight = pawn->Value<float>(Props().StepHeight);
		float stepWidth = pawn->Value<float>(Props().StepWidth);
		vec3 moved = pawn->Location() - pawn->OldLocation();
		float giveBack = std::sqrt(moved.x * moved.x + moved.y * moved.y) * stepHeight / stepWidth;
		if (giveBack > stepHeight)
			giveBack = stepHeight;

		if (offset < 0.0f)
		{
			offset += giveBack;
			if (offset > 0.0f)
				offset = 0.0f;
		}
		else if (offset > 0.0f)
		{
			offset -= giveBack;
			if (offset < 0.0f)
				offset = 0.0f;
		}

		// Stepped down by about StepHeight: the mesh starts that high above and comes down; stepped up: below
		float down = pawn->OldLocation().z - pawn->Location().z;
		if (down < stepHeight + 1.0f && down > stepHeight - 1.0f)
			offset = stepHeight - giveBack;
		float up = pawn->Location().z - pawn->OldLocation().z;
		if (up < stepHeight + 1.0f && up > stepHeight - 1.0f)
			offset = -(stepHeight - giveBack);
	}
}
