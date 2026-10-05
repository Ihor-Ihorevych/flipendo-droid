#pragma once

// KnowWonder's actor physics (HP1's performPhysics and physics modes), on KWMove.h / KWCheck.h.

#include "KWCheck.h"

class UPawn;

namespace KW
{
	// AActor/APawn::performPhysics: one call per tick with the whole frame time.
	void PerformPhysics(UActor* actor, float deltaTime);
	// AActor::setPhysics: the new mode, the base it implies (newFloor, else FindBase), none/rotating stop dead.
	void SetPhysics(UActor* actor, uint8_t newPhysics, UActor* newFloor = nullptr);

	void PhysWalking(UPawn* pawn, float deltaTime, int iterations);
	void PhysFalling(UActor* actor, float deltaTime, int iterations);
	void PhysFlying(UPawn* pawn, float deltaTime, int iterations);
	void ProcessHitWall(UActor* actor, vec3 hitNormal, UActor* hitActor);
	void ProcessLanded(UActor* actor, const vec3& hitNormal, UActor* hitActor, float remainingTime, int iterations);
	void CalcVelocity(UPawn* pawn, const vec3& accelDir, float deltaTime, float maxSpeed, float friction, bool fluid, bool brake, bool buoyant);
	bool PawnMount(UPawn* pawn, const vec3& delta, const CheckResult& hit);
}
