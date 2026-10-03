#include "Precomp.h"
#include "HP1.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Core/UClass.h"
#include "VM/ScriptCall.h"
#include "Math/coords.h"
#include <cmath>

// APawn::moveToward (Engine.dll 0x103D96F0), used by the latent MoveTo / MoveToward / StrafeTo polls.
// Upstream counts a destination as reached at a speed dependent radius and steers with the bare
// direction, which makes walkers overshoot patrol points and circle them. HP1 (stock UE1) instead:
//  - reaches at 16 units horizontally (and |dz| < max(CollisionHeight, 48), dz = 0 when walking),
//  - damps the sideways velocity component when moving fast, so the path bends towards the target,
//  - halves DesiredSpeed once when the next physics step would pass the target.

namespace HP1
{
	// Only physWalking and physFalling test Region.ZoneNumber == 0 and fire FellOutOfWorld; physFlying,
	// physSwimming and physRolling never do. Upstream checks it for every pawn movement mode, which killed
	// Lev_Tut1's flying tut1Peeves0 (parked outside the BSP in state waitforTrigger) on the first tick.
	// Falling has its own check in UActor::TickFalling.
	// IDA Engine.dll: ?physWalking@APawn@@QAEXMH@Z [HP1 0x103E6B60]
	// IDA Engine.dll: ?physFlying@APawn@@QAEXMH@Z [HP1 0x103F13A0] (no zone check)
	// IDA Engine.dll: ?physSwimming@APawn@@QAEXMH@Z [HP1 0x103F20A0] (no zone check)
	// IDA Engine.dll: ?physRolling@AActor@@QAEXMH@Z [HP1 0x103F3040] (no zone check)
	bool PhysicsChecksLeftWorld(UActor* actor)
	{
		return actor->Physics() == PHYS_Walking;
	}

	// IDA Engine.dll: ?performPhysics@APawn@@UAEXM@Z [HP1 0x103E5520]
	void PawnPhysicsTime(UPawn* pawn, float elapsed)
	{
		// APawn::performPhysics keeps a running average of the physics step.
		pawn->AvgPhysicsTime() = 0.8f * pawn->AvgPhysicsTime() + 0.2f * elapsed;
	}

	// APawn::performPhysics turns every pawn towards DesiredRotation after its movement (upstream only does
	// that for non-player pawns, without HP1's pitch/roll handling, and never for the player). HP1's
	// cutscene movement (baseHarry/baseChar CutMovingTo: MoveSmooth + DesiredRotation) depends on it.
	// Yaw and pitch turn at RotationRate; roll is cleared, or with RotationRate.Roll > 0 eased back to
	// level while walking. The flying/swimming bank from lateral acceleration is not ported yet.
	// IDA Engine.dll: ?physicsRotation@APawn@@QAEXMVFVector@@@Z [HP1 0x103E5950]
	// IDA Engine.dll: ?performPhysics@APawn@@UAEXM@Z [HP1 0x103E5520] (calls it unless PHYS_Spider)
	void PawnPhysicsRotation(UPawn* pawn, float elapsed)
	{
		if (pawn->Physics() == PHYS_Spider)
			return;

		pawn->bRotateToDesired() = true;
		pawn->bFixedRotationDir() = false;

		Rotator rot = pawn->Rotation();
		const Rotator& desired = pawn->DesiredRotation();
		if ((desired.Yaw & 0xffff) != (rot.Yaw & 0xffff))
			rot.Yaw = Rotator::TurnToShortest(rot.Yaw, desired.Yaw, (int)std::abs(pawn->RotationRate().Yaw * elapsed));
		if ((desired.Pitch & 0xffff) != (rot.Pitch & 0xffff))
			rot.Pitch = Rotator::TurnToShortest(rot.Pitch, desired.Pitch, (int)std::abs(pawn->RotationRate().Pitch * elapsed));

		if (pawn->RotationRate().Roll <= 0)
		{
			rot.Roll = 0;
		}
		else
		{
			float t = std::min(elapsed * 8.0f, 1.0f);
			int roll = rot.Roll & 0xffff;
			rot.Roll = roll >= 0x8000 ? (int)((0x10000 - roll) * t + roll) : (int)((1.0f - t) * roll);
		}

		pawn->Rotation() = rot;
	}

	// IDA Engine.dll: ?moveToward@APawn@@QAEHABVFVector@@@Z [HP1 0x103D96F0]
	// IDA Engine.dll: ?execPollMoveTo@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D8730], ?execPollMoveToward@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D89C0]
	bool PawnMoveToward(UPawn* pawn, const vec3& dest)
	{
		vec3 direction = dest - pawn->Location();
		EPhysics physics = (EPhysics)pawn->Physics();
		if (physics == PHYS_Walking)
		{
			direction.z = 0.0f;
		}
		else if (physics == PHYS_Falling)
		{
			// Air control only in low gravity zones
			UZoneInfo* zone = pawn->Region().Zone;
			UZoneInfo* defaultZone = zone ? zone->Class->GetDefaultObject<UZoneInfo>() : nullptr;
			if (zone && defaultZone && defaultZone->ZoneGravity().z * 0.9f < zone->ZoneGravity().z)
			{
				vec3 dir2d(direction.x, direction.y, 0.0f);
				float len = length(dir2d);
				pawn->Acceleration() = len > 0.0f ? dir2d / len * pawn->AccelRate() : vec3(0.0f);
				if (pawn->Velocity().z < 0.0f && pawn->Location().z + 100.0f < dest.z)
					return true;
			}
			return false;
		}

		UActor* moveTarget = pawn->MoveTarget();
		if (moveTarget && UObject::TryCast<UInventory>(moveTarget))
		{
			if (std::abs(pawn->Location().z - moveTarget->Location().z) < pawn->CollisionHeight())
				CallEvent(moveTarget, EventName::Touch, { ExpressionValue::ObjectValue(pawn) });
		}

		float distance = length(direction);
		bool glider = !pawn->bCanStrafe() && (physics == PHYS_Flying || physics == PHYS_Swimming);

		if (direction.x * direction.x + direction.y * direction.y < 256.0f && std::abs(direction.z) < std::max(pawn->CollisionHeight(), 48.0f))
		{
			if (!glider)
				pawn->Acceleration() = vec3(0.0f);
			return true;
		}

		if (glider)
			direction = Coords::Rotation(pawn->Rotation()).XAxis;
		else if (distance > 0.0f)
			direction = direction / distance;

		pawn->Acceleration() = direction * pawn->AccelRate();

		if (pawn->MoveTimer() < 0.0f)
			return true;

		if (moveTarget && UObject::TryCast<UPawn>(moveTarget))
		{
			if (moveTarget->CollisionRadius() + pawn->CollisionRadius() + pawn->MeleeRange() * 0.8f > distance)
				return true;
			return false;
		}

		float speed = length(pawn->Velocity());
		if (!glider && speed > 100.0f)
		{
			vec3 velDir = pawn->Velocity() / speed;
			pawn->Acceleration() -= (velDir - direction) * ((1.0f - dot(velDir, direction)) * speed * 0.2f);
		}
		if (pawn->AvgPhysicsTime() * speed * 1.4f > distance)
		{
			if (!pawn->bReducedSpeed())
			{
				pawn->DesiredSpeed() *= 0.5f;
				pawn->bReducedSpeed() = true;
			}
			if (speed > 0.0f)
				pawn->DesiredSpeed() = std::min(pawn->DesiredSpeed(), 200.0f / speed);
			if (glider)
				return true;
		}
		return false;
	}
}
