#pragma once

// KnowWonder's actor movement (HP1's ULevel::MoveActor and friends), on the checks of KWCheck.h.

#include "KWCheck.h"

namespace KW
{
	// ULevel::MoveActor: sweep the actor's collision box by delta, stop at the first actor/world that blocks it, carry
	// what stands on it, Bump and touch. hit.Time is the fraction moved. True when it moved at all.
	bool MoveActor(UActor* actor, const vec3& delta, const Rotator& newRotation, CheckResult& hit, bool test = false, bool ignorePawns = false, bool ignoreBases = false, bool noFail = false);
	// ULevel::FarMoveActor: teleport (FindSpot, encroachment, touch). False when there is no room.
	bool FarMoveActor(UActor* actor, const vec3& dest, bool test = false, bool noCheck = false);
	// ULevel::FindSpot: a free spot for the box at or near location (checkFirst: keep it if already free).
	bool FindSpot(const vec3& extent, vec3& location, bool checkActors, bool checkFirst);
	// ULevel::CheckEncroachment: true when the actor may not move there (an EncroachingOn returned true).
	bool CheckEncroachment(UActor* actor, const vec3& location, const Rotator& rotation, bool touchNotify);
	// ULevel::SetActorZone
	void SetActorZone(UActor* actor, bool test, bool forceRefresh);

	bool IsOverlapping(UActor* actor, UActor* other);
	// AActor::IsBasedOn: actor stands on other (directly or through its base chain)
	bool IsBasedOn(UActor* actor, UActor* other);
	void FindBase(UActor* actor);
	bool MoveSmooth(UActor* actor, const vec3& delta);
	void TwoWallAdjust(const vec3& desiredDir, vec3& delta, const vec3& hitNormal, const vec3& oldHitNormal, float hitTime);
}
