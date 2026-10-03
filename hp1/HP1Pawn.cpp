#include "Precomp.h"
#include "HP1.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Core/UClass.h"
#include "VM/ScriptCall.h"
#include "Math/coords.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Package/PackageManager.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
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

	// Walking or rolling off a ledge: Velocity.Z = 0 (walking), the Falling event (Pawn.Falling plays the in-air
	// animation, Boulder stops its rolling sound), and PHYS_Falling only if the script left the physics alone.
	// Upstream switched to PHYS_Falling without raising Falling.
	// IDA Engine.dll: ?physWalking@APawn@@QAEXMH@Z [HP1 0x103E6B60] (eventFalling, then setPhysics(PHYS_Falling) if still PHYS_Walking)
	// IDA Engine.dll: ?physRolling@AActor@@QAEXMH@Z [HP1 0x103F3040] (the same for PHYS_Rolling)
	bool StartFalling(UActor* actor)
	{
		uint8_t physics = actor->Physics();
		if (physics == PHYS_Walking)
			actor->Velocity().z = 0.0f;
		CallEvent(actor, EventName::Falling);
		if (actor->bDeleteMe())
			return false;
		if (actor->Physics() == physics)
			actor->SetPhysics(PHYS_Falling);
		return actor->Physics() == PHYS_Falling;
	}

	// No floor ahead while walking: MayFall (once, if bCanJump and probed) lets the script decide; a pawn that
	// can't jump, or walks (bIsWalking), stops dead at the ledge (back to OldLocation, MoveTimer = -1). Upstream
	// let pawns without bCanJump walk off and ignored bIsWalking.
	// IDA Engine.dll: ?physWalking@APawn@@QAEXMH@Z [HP1 0x103E6B60] (the no-floor branch: bCheckedFall, eventMayFall, FarMoveActor(OldLocation))
	static bool PawnAutoJump(UPawn* pawn);

	bool PawnWalkOffLedge(UPawn* pawn)
	{
		if (PawnAutoJump(pawn))
			return true;

		if (pawn->bCanJump() && pawn->IsEventEnabled(EventName::MayFall))
			CallEvent(pawn, EventName::MayFall);
		if (pawn->bDeleteMe() || pawn->Physics() != PHYS_Walking)
			return true;

		if (!pawn->bCanJump() || pawn->bIsWalking())
		{
			pawn->Velocity() = vec3(0.0f);
			pawn->Acceleration() = vec3(0.0f);
			pawn->SetLocation(pawn->OldLocation());
			pawn->MoveTimer() = -1.0f;
			return true;
		}

		if (StartFalling(pawn))
			pawn->SetBase(nullptr, true);
		return false;
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

	// Pawn.MaxMountHeight is an HP1 addition (Harry: 96.5), not in upstream's PropertyOffsets.
	// Destination = MoveTarget's location (flying towards a pawn aims at 0.7 of its height), Focus = Destination.
	// A walking pawn with bAdvancedTactics gets AlterDestination to change Destination for this step, which is
	// then put back. A pawn target keeps DesiredSpeed (moveToward halves it near the target); one in a water
	// zone ends the move for a pawn that can't swim.
	// IDA Engine.dll: ?execPollMoveToward@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D89C0]
	bool PawnPollMoveToward(UPawn* pawn)
	{
		UActor* target = pawn->MoveTarget();
		if (!target)
			return true;

		pawn->Destination() = target->Location();
		if (pawn->Physics() == PHYS_Flying && UObject::TryCast<UPawn>(target))
			pawn->Destination().z += target->CollisionHeight() * 0.7f;
		else if (pawn->Physics() == PHYS_Spider)
			pawn->Destination() -= pawn->Floor() * target->CollisionRadius();

		pawn->Focus() = pawn->Destination();
		pawn->TickRotateTo(pawn->Focus());

		float desiredSpeed = pawn->DesiredSpeed();
		bool alter = pawn->bAdvancedTactics() && pawn->Physics() == PHYS_Walking;
		if (alter)
			CallEvent(pawn, NameString("AlterDestination"));
		bool done = PawnMoveToward(pawn, pawn->Destination());
		if (pawn->bAdvancedTactics() && pawn->Physics() == PHYS_Walking && pawn->MoveTarget())
			pawn->Destination() = pawn->MoveTarget()->Location();

		target = pawn->MoveTarget();
		if (target && UObject::TryCast<UPawn>(target))
		{
			pawn->DesiredSpeed() = desiredSpeed;
			UZoneInfo* zone = target->Region().Zone;
			if (!pawn->bCanSwim() && zone && zone->bWaterZone())
				pawn->MoveTimer() = -1.0f;
		}
		return done;
	}

	// Focus = FaceTarget's location; moves to Destination, which AlterDestination may change for this step.
	// IDA Engine.dll: ?execPollStrafeFacing@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D9010]
	bool PawnPollStrafeFacing(UPawn* pawn)
	{
		UActor* faceTarget = pawn->FaceTarget();
		if (!faceTarget)
			return true;

		pawn->Focus() = faceTarget->Location();
		vec3 destination = pawn->Destination();
		pawn->TickRotateTo(pawn->Focus());
		if (pawn->bAdvancedTactics() && pawn->Physics() == PHYS_Walking)
			CallEvent(pawn, NameString("AlterDestination"));
		bool done = PawnMoveToward(pawn, pawn->Destination());
		pawn->Destination() = destination;
		return done;
	}

	// Done once the pawn stops falling; after LatentFloat (2.5 s from WaitForLanding) runs out, LongFall
	// every tick until then.
	// IDA Engine.dll: ?execPollWaitForLanding@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D6EF0]
	// IDA Engine.dll: ?execWaitForLanding@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D6EB0] (LatentFloat = 2.5, latent only if falling)
	bool PawnPollWaitForLanding(UPawn* pawn, float elapsed)
	{
		if (pawn->Physics() != PHYS_Falling)
			return true;
		pawn->LatentFloat() -= elapsed;
		if (pawn->LatentFloat() < 0.0f)
			CallEvent(pawn, NameString("LongFall"));
		return false;
	}

	static float& MaxMountHeight(UPawn* pawn)
	{
		static PropertyDataOffset offset;
		static bool initialized = false;
		if (!initialized)
		{
			offset = engine->packages->FindClass("Engine.Pawn")->GetPropertyDataOffset("MaxMountHeight");
			initialized = true;
		}
		return pawn->Value<float>(offset);
	}

	// The BSP surface a collision hit landed on. The original walks the hit node's coplanar chain (BspNode.Plane)
	// for the polygon that contains the hit location within `tolerance` of its edges and returns its surface.
	// For mover hits it first moves the hit into the brush's local space; here a mover hit just uses the node
	// the collision code reported.
	// IDA Engine.dll: not exported: sub_103FF1B0 [HP1 0x103FF1B0] (FCheckResult -> FBspSurf*; called from APawn::Mount_0 with the max cylinder extent)
	// IDA Engine.dll: not exported: sub_103FEBD0 [HP1 0x103FEBD0] (FCheckResult -> FBspNode*; the coplanar point-in-polygon search, callee of sub_103FF1B0)
	static const BspSurface* HitSurface(const CollisionHit& hit, const vec3& location, float tolerance)
	{
		if (!hit.Node)
			return nullptr;

		UModel* model = engine->Level->Model;
		if (hit.Node < model->Nodes.data() || hit.Node >= model->Nodes.data() + model->Nodes.size())
		{
			UMover* mover = UObject::TryCast<UMover>(hit.Actor);
			UModel* brush = mover ? mover->Brush() : nullptr;
			if (!brush || hit.Node < brush->Nodes.data() || hit.Node >= brush->Nodes.data() + brush->Nodes.size() || hit.Node->Surf < 0)
				return nullptr;
			return &brush->Surfaces[hit.Node->Surf];
		}

		const BspNode* candidate = nullptr;
		int index = (int)(hit.Node - model->Nodes.data());

		// Upstream's cylinder-vs-BSP collision can report a node of a neighbouring plane (e.g. the top of the
		// bookcase whose front was hit), where UE1's Hit.Item is the node of the face hit. Then find that face
		// with a zero-extent ray from the pawn's centre into the wall.
		const BspNode& reported = model->Nodes[index];
		if (dot(vec3(reported.PlaneX, reported.PlaneY, reported.PlaneZ), hit.Normal) < 0.99f)
		{
			index = -1;
			float best = 2.0f;
			for (const CollisionHit& h : engine->Level->Collision.Trace(location, location - hit.Normal * (2.0f * tolerance), 0.0f, 0.0f, false, true, false))
			{
				if (h.Node && h.Fraction < best && h.Node >= model->Nodes.data() && h.Node < model->Nodes.data() + model->Nodes.size())
				{
					best = h.Fraction;
					index = (int)(h.Node - model->Nodes.data());
				}
			}
		}
		while (index != -1)
		{
			const BspNode& node = model->Nodes[index];
			vec3 planeNormal(node.PlaneX, node.PlaneY, node.PlaneZ);
			if (node.NumVertices != 0 && dot(planeNormal, hit.Normal) >= 0.99f)
			{
				candidate = &node;
				float side = 1.0f;
				int i = 0;
				vec3 prev = model->Points[model->Vertices[node.VertPool + node.NumVertices - 1].Vertex];
				for (; i < node.NumVertices; i++)
				{
					vec3 cur = model->Points[model->Vertices[node.VertPool + i].Vertex];
					vec3 edgeNormal = cross(planeNormal, cur - prev);
					float len2 = dot(edgeNormal, edgeNormal);
					if (len2 >= 1e-8f)
						edgeNormal = edgeNormal * (1.0f / std::sqrt(len2));
					if (-tolerance > dot(edgeNormal, location - cur) * side)
					{
						if (i != 0)
							break;
						side = -1.0f; // the first edge decides the winding
					}
					prev = cur;
				}
				if (i == node.NumVertices)
					return node.Surf >= 0 ? &model->Surfaces[node.Surf] : nullptr;
			}
			index = node.Plane;
		}
		return (tolerance == 0.0f && candidate && candidate->Surf >= 0) ? &model->Surfaces[candidate->Surf] : nullptr;
	}

	// Ledge grabbing. physFalling calls this with (0,0,1) when a falling pawn hits a wall, stepUp with -GravDir
	// when a walking pawn runs into one. Only BSP surfaces flagged PF_SpecialPoly (0x1000, HP1's "mountable")
	// qualify. It looks for a floor at most MaxMountHeight up just behind the wall, checks the way up and over is
	// clear, sets the base and raises Pawn.Mount(ledge - Location); harry.uc's Mounting states do the climb.
	// IDA Engine.dll: ?Mount@APawn@@QAE_NABVFVector@@AAUFCheckResult@@@Z [HP1 0x103EBFB0]
	bool PawnMount(UPawn* pawn, const vec3& delta, const CollisionHit& hit)
	{
		float maxMount = MaxMountHeight(pawn);
		if (maxMount <= 0.0f)
			return false;
		if (hit.Normal.z <= -0.1f || hit.Normal.z >= 0.7f)
			return false;

		// Must be facing the wall.
		vec3 x, y, z;
		Coords::Rotation(pawn->Rotation()).GetAxes(x, y, z);
		if (dot(x, hit.Normal) >= 0.0f)
			return false;

		vec3 extent(pawn->CollisionRadius(), pawn->CollisionRadius(), pawn->CollisionHeight());
		float maxExtent = std::max(std::max(std::abs(extent.x), std::abs(extent.y)), std::abs(extent.z));
		const BspSurface* surf = HitSurface(hit, pawn->Location(), maxExtent);
		if (!surf || (surf->PolyFlags & PF_SpecialPoly) == 0)
			return false;

		// Horizontal direction into the wall.
		vec3 into(-hit.Normal.x, -hit.Normal.y, 0.0f);
		float len2 = into.x * into.x + into.y * into.y;
		if (len2 >= 1e-8f)
			into = into * (1.0f / std::sqrt(len2));

		// Trace down from MaxMountHeight above, just past the wall, to find the ledge.
		vec3 top = pawn->Location() + delta * maxMount;
		vec3 start = top + into * (2.0f * pawn->CollisionRadius() + maxMount * hit.Normal.z);
		vec3 end = start - delta * maxMount - into * (maxMount * hit.Normal.z);
		TraceFlags flags;
		flags.movers = true;
		flags.world = true; // TRACE_Level | TRACE_Movers
		CollisionSystem& collision = pawn->XLevel()->Collision;
		CollisionHit ledgeHit = collision.TraceFirstHit(start, end, pawn, extent, flags);
		if (ledgeHit.Fraction >= 1.0f)
			return false;
		vec3 ledge = start + (end - start) * ledgeHit.Fraction;

		float minHeight = pawn->Physics() == PHYS_Falling ? 0.0f : pawn->MaxStepHeight();
		if (ledge.z - pawn->Location().z < minHeight)
			return false;

		// The way straight up and then over onto the ledge must be clear.
		vec3 dest(ledge.x, ledge.y, ledge.z + 2.0f);
		vec3 up(top.x, top.y, dest.z);
		if (collision.TraceFirstHit(pawn->Location(), up, pawn, extent, flags).Fraction < 1.0f ||
			collision.TraceFirstHit(up, dest, pawn, extent, flags).Fraction < 1.0f)
			return false;

		pawn->SetBase(hit.Actor ? hit.Actor : (UActor*)pawn->Level(), true);
		CallEvent(pawn, NameString("Mount"), { ExpressionValue::VectorValue(dest - pawn->Location()) });
		return true;
	}

	static bool AutoJumpEnabled(UPawn* pawn)
	{
		static PropertyDataOffset offset;
		static bool initialized = false;
		if (!initialized)
		{
			offset = engine->packages->FindClass("Engine.PlayerPawn")->GetPropertyDataOffset("bAutoJump");
			initialized = true;
		}
		return UObject::TryCast<UPlayerPawn>(pawn) && pawn->BoolValue(offset);
	}

	// Where a fall starting at `start` with `velocity` ends: steps of at most 0.2 s for up to maxTime seconds,
	// sweeping the pawn's cylinder; on a wall it slides (the velocity and gravity lose their component along the
	// wall normal). Ends on a floor (normal.z > 0.7) or on a mountable wall the pawn faces (it would grab it),
	// else after maxTime or 10 steps.
	// IDA Engine.dll: not exported: sub_103E6310 [HP1 0x103E6310] (thunk sub_10301929; called twice from APawn::physWalking_0's auto-jump test)
	static vec3 PredictLanding(UPawn* pawn, vec3 start, vec3 velocity, float maxTime)
	{
		vec3 gravity = pawn->Region().Zone ? pawn->Region().Zone->ZoneGravity() : vec3(0.0f, 0.0f, -512.0f);
		vec3 extent(pawn->CollisionRadius(), pawn->CollisionRadius(), pawn->CollisionHeight());
		TraceFlags flags;
		flags.pawns = true;
		flags.movers = true;
		flags.world = true; // TRACE_Pawns | TRACE_Movers | TRACE_Level
		CollisionSystem& collision = pawn->XLevel()->Collision;

		vec3 normal(0.0f);
		float time = 0.0f;
		for (int step = 0; step < 10 && time < maxTime; step++)
		{
			float dt = std::min(maxTime - time, 0.2f);
			vec3 g = gravity - normal * dot(gravity, normal);
			vec3 end = start + velocity * dt + g * (0.5f * dt * dt);
			CollisionHit hit = collision.TraceFirstHit(start, end, pawn, extent, flags);
			float f = std::min(hit.Fraction, 1.0f);
			time += dt * f;
			velocity += g * (dt * f);
			if (f >= 1.0f)
			{
				normal = vec3(0.0f);
				start = end;
				continue;
			}

			start = start + (end - start) * f;
			normal = hit.Normal;
			if (normal.z > 0.7f)
				return start;
			if (MaxMountHeight(pawn) > 0.0f && normal.z > -0.1f)
			{
				vec3 x, y, z;
				Coords::Rotation(pawn->Rotation()).GetAxes(x, y, z);
				if (dot(x, normal) < 0.0f)
				{
					float maxExtent = std::max(extent.x, extent.z);
					const BspSurface* surf = HitSurface(hit, start, maxExtent);
					if (surf && (surf->PolyFlags & PF_SpecialPoly))
						return start;
				}
			}
			velocity -= normal * dot(velocity, normal);
		}
		return start;
	}

	// PlayerPawn.bAutoJump ("Auto Jump" in the options): walking off a ledge, find the edge just behind the feet
	// (a ray from below the feet backwards along the walk direction hitting the ledge's face, facing forward by
	// more than 0.25), predict where a fall from there lands with and without JumpZ, and if jumping lands more
	// than 10 units higher, move to the edge and raise DoJump(1). The original then spends the rest of the step
	// in physFalling; here the next tick does.
	// IDA Engine.dll: ?physWalking@APawn@@QAEXMH@Z [HP1 0x103E6B60] (the bAutoJump block before eventDoJump)
	static bool PawnAutoJump(UPawn* pawn)
	{
		if (!AutoJumpEnabled(pawn))
			return false;

		vec3 accel(pawn->Acceleration().x, pawn->Acceleration().y, 0.0f);
		float len = length(accel);
		if (len <= 0.0f)
			return false;
		vec3 dir = accel / len;

		CollisionSystem& collision = pawn->XLevel()->Collision;
		TraceFlags flags;
		flags.pawns = true;
		flags.movers = true;
		flags.world = true;
		float back = pawn->CollisionRadius() * 1.5f;
		vec3 below(0.0f, 0.0f, -(pawn->CollisionHeight() + 4.0f));
		vec3 start = pawn->Location() + below;
		vec3 end = pawn->Location() - dir * back + below;
		CollisionHit edge = collision.TraceFirstHit(start, end, pawn, vec3(0.0f), flags);
		if (edge.Fraction >= 1.0f)
			return false;
		float facing = dot(edge.Normal, dir);
		if (facing <= 0.25f)
			return false;

		vec3 edgePoint = pawn->Location() * (1.0f - edge.Fraction) + (pawn->Location() - dir * back) * edge.Fraction;
		vec3 dest = edgePoint + dir * (back / facing);

		vec3 fall = PredictLanding(pawn, dest, pawn->Velocity(), 2.0f);
		vec3 jump = PredictLanding(pawn, dest, pawn->Velocity() + vec3(0.0f, 0.0f, pawn->JumpZ()), 2.0f);
		if (jump.z - fall.z <= 10.0f)
			return false;

		pawn->TryMove(dest - pawn->Location());
		CallEvent(pawn, NameString("DoJump"), { ExpressionValue::FloatValue(1.0f) });
		return pawn->Physics() != PHYS_Walking;
	}
}
