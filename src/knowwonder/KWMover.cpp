#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "KWMove.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Math/coords.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

// HP1's Mover.uc re-declares PhysAlpha and PhysRate, shadowing Actor's. Mover script (InterpolateTo etc.) and
// KnowWonder's native mover physics use the Mover copies; upstream's TickMovingBrush uses Actor's, which stay 0.
// KnowWonder's physMovingBrush also lets a mover with bCollideWorld (GridMover, the Flipendo blocks) fall with the zone's
// gravity, and stops a mover whose move was blocked. It moves with ULevel::MoveActor (KWMove.cpp), which collides a
// bCollideWorld brush with the world by its bounding box and pushes encroached actors.

namespace KW
{
	struct MoverProps
	{
		PropertyDataOffset PhysAlpha, PhysRate;
	};

	// UObject::GetPropertyDataOffset returns the first (base-most) property with a name; we want the
	// Mover's own, which comes last in the class property list.
	static PropertyDataOffset LastPropertyOffset(UClass* cls, const NameString& name)
	{
		PropertyDataOffset result;
		for (UProperty* prop : cls->Properties)
			if (prop->Name == name)
				result = prop->DataOffset;
		return result;
	}

	static const MoverProps& GetMoverProps()
	{
		static MoverProps props;
		static bool initialized = false;
		if (!initialized)
		{
			UClass* actorCls = engine->packages->FindClass("Engine.Actor");
			UClass* moverCls = engine->packages->FindClass("Engine.Mover");
			props.PhysAlpha = LastPropertyOffset(moverCls, "PhysAlpha");
			props.PhysRate = LastPropertyOffset(moverCls, "PhysRate");
			if (props.PhysAlpha.DataOffset == actorCls->GetPropertyDataOffset("PhysAlpha").DataOffset)
				Exception::Throw("HP1: expected Mover.PhysAlpha to shadow Actor.PhysAlpha");
			initialized = true;
		}
		return props;
	}

	// IDA Engine.dll: ?physMovingBrush@AActor@@QAEXM@Z [HP1 0x104061F0]
	void PhysMovingBrush(UActor* actor, float deltaTime)
	{
		// Only movers move: any other actor with PHYS_MovingBrush stays put (Lev2_Inc_A's spellTrigger2)
		UMover* mover = UObject::TryCast<UMover>(actor);
		if (!mover)
			return;
		const MoverProps& p = GetMoverProps();
		float& physAlpha = mover->Value<float>(p.PhysAlpha);
		float& physRate = mover->Value<float>(p.PhysRate);
		int key = std::min((int)mover->KeyNum(), 7);

		while (mover->bInterpolating() && deltaTime > 0.0f)
		{
			// Fall: the velocity along the zone's gravity, plus gravity. What it falls is added to the current key and to
			// OldPos, so the rest of the move happens at the new height. Not ported: AActor::FindBase when it can't fall.
			bool fell = false;
			UZoneInfo* zone = mover->Region().Zone;
			if (mover->bCollideWorld() && zone)
			{
				vec3 gravity = zone->ZoneGravity();
				float gravity2 = dot(gravity, gravity);
				if (gravity2 > 0.0f)
				{
					vec3 along = gravity * (dot(gravity, mover->Velocity()) / gravity2);
					vec3 delta = along * deltaTime + gravity * (deltaTime * deltaTime * 0.5f);
					mover->Velocity() += gravity * deltaTime;
					vec3 start = mover->Location();
					CheckResult hit;
					MoveActor(mover, delta, mover->Rotation(), hit);
					if (hit.Time <= 0.0f)
					{
						FindBase(mover);
					}
					else
					{
						vec3 moved = mover->Location() - start;
						mover->KeyPos()[key] += moved;
						mover->OldPos() += moved;
						fell = true;
					}
				}
			}

			float alpha = physAlpha + physRate * deltaTime;
			if (alpha <= 1.0f)
			{
				deltaTime = 0.0f;
			}
			else if (physAlpha >= 1.0f)
			{
				deltaTime = 0.0f;
				alpha = 1.0f;
			}
			else
			{
				deltaTime = (alpha - 1.0f) / (alpha - physAlpha) * deltaTime;
				alpha = 1.0f;
			}

			float t = alpha;
			if (mover->MoverGlideType() == 1/*MV_GlideByTime*/)
				t = t * t * 3.0f - t * t * t * 2.0f;

			Rotator oldRot = mover->OldRot();
			Rotator rotDelta = mover->BaseRot() + mover->KeyRot()[key] - oldRot;
			Rotator targetRot(oldRot.Pitch + (int)(rotDelta.Pitch * t), oldRot.Yaw + (int)(rotDelta.Yaw * t), oldRot.Roll + (int)(rotDelta.Roll * t));
			vec3 oldPos = mover->OldPos();
			vec3 targetPos = oldPos + (mover->BasePos() + mover->KeyPos()[key] - oldPos) * t;

			CheckResult hit;
			if (MoveActor(mover, targetPos - mover->Location(), targetRot, hit))
				physAlpha += (alpha - physAlpha) * hit.Time;
			float time = hit.Time;
			if (time >= 1.0f && physAlpha < 1.0f && !fell && deltaTime > 0.0f)
				break; // refused or not there yet with time left over: try again next tick

			if (!fell)
			{
				if (time < 1.0f)
				{
					mover->bInterpolating() = false; // blocked: GridMover's Tick sees this and finishes the move
				}
				else if (physAlpha >= 1.0f)
				{
					mover->bInterpolating() = false;
					CallEvent(mover, NameString("KeyFrameReached")); // Mover's calls InterpolateEnd(self)
				}
			}
		}
	}
}
