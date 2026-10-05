#include "Precomp.h"
#include "KWPhysics.h"
#include "KWMove.h"
#include "KW.h"
#include "KWActor.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/UProjectile.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Decoration/UCarcass.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Math/coords.h"
#include "Utils/Logger.h"
#include <cmath>
#include <random>

// HP1's actor physics (AActor/APawn::performPhysics and the physics modes it calls), on KnowWonder's own movement
// (KWMove.h) and collision (KWCheck.h). One call per actor per tick with the whole frame time; the modes split it into
// their own steps. docs/re/engine/physics.md

namespace KW
{
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Helpers

	static vec3 SafeNormal(const vec3& v)
	{
		float len2 = dot(v, v);
		return len2 == 0.0f ? vec3(0.0f) : v * (1.0f / std::sqrt(len2));
	}

	static bool IsZero(const vec3& v)
	{
		return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
	}

	static UPawn* AsPawn(UActor* actor) { return UObject::TryCast<UPawn>(actor); }
	static bool IsPlayerPawn(UActor* actor) { return UObject::TryCast<UPlayerPawn>(actor) != nullptr; }
	static bool IsDecoration(UActor* actor) { return UObject::TryCast<UDecoration>(actor) != nullptr; }

	static float Frand()
	{
		static std::mt19937 rng(12345);
		return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
	}

	static PropertyDataOffset PropertyOffset(const char* cls, const char* name)
	{
		return engine->packages->FindClass(NameString(cls))->GetPropertyDataOffset(NameString(name));
	}

	// Pawn.MaxMountHeight (KnowWonder): how high a ledge a pawn can climb (0: none)
	static float MaxMountHeight(UPawn* pawn)
	{
		static PropertyDataOffset offset = PropertyOffset("Engine.Pawn", "MaxMountHeight");
		return pawn->Value<float>(offset);
	}

	// PlayerPawn.bAutoJump (KnowWonder, "Auto Jump" in the options)
	static bool AutoJump(UPawn* pawn)
	{
		static PropertyDataOffset offset = PropertyOffset("Engine.PlayerPawn", "bAutoJump");
		return IsPlayerPawn(pawn) && pawn->BoolValue(offset);
	}

	static bool MoveBy(UActor* actor, const vec3& delta, CheckResult& hit)
	{
		return MoveActor(actor, delta, actor->Rotation(), hit);
	}

	static vec3 CylinderExtent(UActor* actor)
	{
		return vec3(actor->CollisionRadius(), actor->CollisionRadius(), actor->CollisionHeight());
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// setPhysics, rotation

	// Walking, none, rotating, rolling, spider: on the given floor (or FindBase); anything else drops its base. None
	// and rotating stop dead.
	// IDA Engine.dll: ?setPhysics@AActor@@QAEXEPAV1@@Z [HP1 0x103E5140]
	void SetPhysics(UActor* actor, uint8_t newPhysics, UActor* newFloor)
	{
		if (actor->Physics() == newPhysics)
			return;
		actor->Physics() = newPhysics;
		if (newPhysics < PHYS_Falling || newPhysics == PHYS_Rolling || newPhysics == PHYS_Rotating || newPhysics == PHYS_Spider)
		{
			if (newFloor)
			{
				if (actor->ActorBase() != newFloor)
					actor->SetBase(newFloor, true);
			}
			else
			{
				FindBase(actor);
			}
		}
		else if (actor->ActorBase())
		{
			actor->SetBase(nullptr, true);
		}
		if (actor->Physics() == PHYS_None || actor->Physics() == PHYS_Rotating)
		{
			actor->Velocity() = vec3(0.0f);
			actor->Acceleration() = vec3(0.0f);
		}
	}

	// A step of a turn from current to desired (16-bit angles). bFixedRotationDir: by rate, toward desired only with
	// bRotateToDesired and only up to it; else bRotateToDesired: the short way, at most |rate|.
	// IDA Engine.dll: ?fixedTurn@AActor@@QAEHHHH@Z [HP1 0x103E57E0]
	static int FixedTurn(UActor* actor, int current, int desired, int deltaRate)
	{
		if (deltaRate == 0)
			return current & 0xFFFF;
		int cur = current & 0xFFFF, des = desired & 0xFFFF;
		int rate = deltaRate;
		if (actor->bFixedRotationDir())
		{
			if (!actor->bRotateToDesired())
				return (rate + cur) & 0xFFFF;
			if (rate > 0)
			{
				int target = cur > des ? des + 0x10000 : des;
				int diff = target - cur;
				return rate <= diff ? (rate + cur) & 0xFFFF : (diff + cur) & 0xFFFF;
			}
			int from = cur < des ? cur + 0x10000 : cur;
			int diff = des - from;
			if (rate >= diff)
				diff = rate;
			return (diff + cur) & 0xFFFF;
		}
		if (!actor->bRotateToDesired())
			return cur;
		if (rate < 0)
			rate = -rate;
		if (cur <= des)
		{
			int diff = des - cur;
			if (diff < 0x8000)
				return (std::min(diff, rate) + cur) & 0xFFFF;
			int back = cur - des + 0x10000;
			return (cur - std::min(back, rate)) & 0xFFFF;
		}
		int diff = cur - des;
		if (diff < 0x8000)
			return (cur - std::min(diff, rate)) & 0xFFFF;
		int fwd = des - cur + 0x10000;
		return (std::min(fwd, rate) + cur) & 0xFFFF;
	}

	// IDA Engine.dll: ?physicsRotation@AActor@@QAEXM@Z [HP1 0x103E5FB0]
	static void ActorPhysicsRotation(UActor* actor, float deltaTime)
	{
		bool rotateToDesired = actor->bRotateToDesired();
		if (!rotateToDesired && !actor->bFixedRotationDir())
			return;
		if (rotateToDesired && actor->Rotation() == actor->DesiredRotation())
			return;

		Rotator rot = actor->Rotation();
		const Rotator& desired = actor->DesiredRotation();
		int rollRate = (int)(actor->RotationRate().Roll * deltaTime);
		int yawRate = (int)(actor->RotationRate().Yaw * deltaTime);
		int pitchRate = (int)(actor->RotationRate().Pitch * deltaTime);
		int yaw = rot.Yaw, pitch = rot.Pitch, roll = rot.Roll;
		if (yawRate != 0 && (!rotateToDesired || desired.Yaw != rot.Yaw))
			yaw = FixedTurn(actor, rot.Yaw, desired.Yaw, yawRate);
		if (pitchRate != 0 && (!rotateToDesired || desired.Pitch != rot.Pitch))
			pitch = FixedTurn(actor, rot.Pitch, desired.Pitch, pitchRate);
		if (rollRate != 0 && (!rotateToDesired || desired.Roll != rot.Roll))
			roll = FixedTurn(actor, rot.Roll, desired.Roll, rollRate);
		if (pitch != rot.Pitch || yaw != rot.Yaw || roll != rot.Roll)
		{
			CheckResult hit;
			MoveActor(actor, vec3(0.0f), Rotator(pitch, yaw, roll), hit);
		}
		if (actor->bRotateToDesired() && actor->Rotation() == actor->DesiredRotation() && actor->IsEventEnabled(EventName::EndedRotation))
			CallEvent(actor, EventName::EndedRotation);
	}

	// Yaw and pitch turn towards DesiredRotation at RotationRate (bRotateToDesired, not bFixedRotationDir). Roll: none
	// without RotationRate.Roll; eased back to level when walking slower than 200 or accelerating less than 100; else
	// banked by the sideways acceleration (x28000, x4096 walking, over AccelRate) up to RotationRate.Roll.
	// IDA Engine.dll: ?physicsRotation@APawn@@QAEXMVFVector@@@Z [HP1 0x103E5950]
	static void PawnPhysicsRotation(UPawn* pawn, float deltaTime, const vec3& oldVelocity)
	{
		Rotator rot = pawn->Rotation();
		int yaw = rot.Yaw, pitch = rot.Pitch, roll = rot.Roll;
		pawn->bRotateToDesired() = true;
		pawn->bFixedRotationDir() = false;
		if (pawn->DesiredRotation().Yaw != yaw)
			yaw = FixedTurn(pawn, yaw, pawn->DesiredRotation().Yaw, (int)(pawn->RotationRate().Yaw * deltaTime));
		if (pawn->DesiredRotation().Pitch != pitch)
			pitch = FixedTurn(pawn, pitch, pawn->DesiredRotation().Pitch, (int)(pawn->RotationRate().Pitch * deltaTime));

		auto easeToLevel = [&](int r)
		{
			float t = std::min(deltaTime * 8.0f, 1.0f);
			return r >= 0x8000 ? (int)((double)(0x10000 - r) * t + (double)r) : (int)((1.0f - t) * (double)r);
		};

		if (pawn->RotationRate().Roll <= 0)
		{
			roll = 0;
		}
		else if (pawn->Physics() == PHYS_Walking && dot(pawn->Velocity(), pawn->Velocity()) < 40000.0f)
		{
			roll = easeToLevel(roll);
		}
		else
		{
			vec3 accel = (pawn->Velocity() - oldVelocity) * (1.0f / deltaTime);
			if (dot(accel, accel) <= 10000.0f)
			{
				roll = easeToLevel(roll);
			}
			else
			{
				float scale = pawn->Physics() == PHYS_Walking ? 4096.0f : 28000.0f;
				Coords c = Coords::InverseRotation(Rotator(pitch, yaw, 0));
				float side = dot(accel, c.YAxis);
				float bank = side * scale / pawn->AccelRate();
				int target;
				if (side <= 0.0f)
					target = std::max((int)(bank + 65536.0f), 0x10000 - pawn->RotationRate().Roll);
				else
					target = std::min((int)bank, pawn->RotationRate().Roll);
				int current = pawn->Rotation().Roll & 0xFFFF;
				if (target <= 0x8000)
				{
					if (current > 0x8000)
						current -= 0x10000;
				}
				else if (current < 0x8000)
				{
					current += 0x10000;
				}
				pawn->Rotation().Roll = current;
				float t = std::min(deltaTime * 5.0f, 1.0f);
				roll = (int)((1.0f - t) * (double)current + (double)target * t);
			}
		}

		if (pitch != pawn->Rotation().Pitch || yaw != pawn->Rotation().Yaw || roll != pawn->Rotation().Roll)
		{
			CheckResult hit;
			MoveActor(pawn, vec3(0.0f), Rotator(pitch, yaw, roll), hit);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Walls, landing

	// No HitWall against pawns. A pawn steering into the wall (towards Destination, flat when walking, within
	// MinHitWall) gets HitWall only if its script probes it (falling: always); otherwise its move ends (MoveTimer -1,
	// bFromWall). Other actors get HitWall if they probe it.
	// IDA Engine.dll: ?processHitWall@AActor@@QAEXVFVector@@PAV1@@Z [HP1 0x103ECF20]
	void ProcessHitWall(UActor* actor, vec3 hitNormal, UActor* hitActor)
	{
		if (hitActor && AsPawn(hitActor))
			return;
		UPawn* pawn = AsPawn(actor);
		if (pawn)
		{
			if (IsZero(pawn->Acceleration()))
				return;
			vec3 dir = SafeNormal(pawn->Destination() - pawn->Location());
			if (pawn->Physics() == PHYS_Walking)
			{
				dir.z = 0.0f;
				hitNormal.z = 0.0f;
			}
			if (dot(dir, hitNormal) > pawn->MinHitWall())
				return;
			if (!pawn->IsEventEnabled(EventName::HitWall) && pawn->Physics() != PHYS_Falling)
			{
				pawn->MoveTimer() = -1.0f;
				pawn->bFromWall() = true;
				return;
			}
		}
		else if (!actor->IsEventEnabled(EventName::HitWall))
		{
			return;
		}
		CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hitNormal), ExpressionValue::ObjectValue(hitActor) });
	}

	void PhysWalking(UPawn* pawn, float deltaTime, int iterations);
	void PhysFalling(UActor* actor, float deltaTime, int iterations);
	static void StartSwimming(UPawn* pawn, const vec3& oldVelocity, float timeTick, float remainingTime, int iterations);
	void PhysSwimming(UPawn* pawn, float deltaTime, int iterations);
	static void StepUp(UPawn* pawn, const vec3& gravDir, const vec3& desiredDir, vec3 delta, CheckResult& hit);

	// A decoration landing over an edge rolls off it (a ray below its centre finds nothing; four short cylinder traces
	// under its corners pick the direction), at most 5 times in a row; a carcass on a slope slides now and then. A
	// non-pawn landing in a bBounceVelocity zone with ZoneVelocity bounces. Then Landed, and walking (pawn) or none.
	// IDA Engine.dll: ?processLanded@AActor@@QAEXVFVector@@PAV1@MH@Z [HP1 0x103ED210]
	void ProcessLanded(UActor* actor, const vec3& hitNormal, UActor* hitActor, float remainingTime, int iterations)
	{
		UZoneInfo* zone = actor->Region().Zone;
		if (!AsPawn(actor) && zone && zone->bBounceVelocity() && !IsZero(zone->ZoneVelocity()))
		{
			actor->Velocity() = zone->ZoneVelocity() + vec3(0.0f, 0.0f, 80.0f);
			return;
		}

		if (UDecoration* deco = UObject::TryCast<UDecoration>(actor))
		{
			if (deco->numLandings() >= 5)
			{
				deco->numLandings() = 0;
			}
			else
			{
				CheckResult hit;
				vec3 loc = actor->Location();
				float h = actor->CollisionHeight(), r = actor->CollisionRadius();
				SingleLineCheck(hit, actor, loc - vec3(0.0f, 0.0f, h + r + 8.0f), loc - vec3(0.0f, 0.0f, 0.8f * h), TRACE_Blocking | TRACE_Movers | TRACE_Level, vec3(0.0f));
				if (!hit.Actor)
				{
					vec3 ext(r * 0.5f, r * 0.5f, h);
					auto corner = [&](float sx, float sy)
					{
						vec3 start = loc + vec3(sx * 0.5f * r, sy * 0.5f * r, 0.0f);
						CheckResult h2;
						return SingleLineCheck(h2, actor, start + vec3(0.0f, 0.0f, -8.0f), start, TRACE_Blocking | TRACE_Movers | TRACE_Level, ext) ? 1 : 0;
					};
					int pp = corner(1.0f, 1.0f), mp = corner(-1.0f, 1.0f), mm = corner(-1.0f, -1.0f), pm = corner(1.0f, -1.0f);
					if (pm + mm + mp + pp > 1 && mm + pp != 0 && pm + mp != 0)
					{
						deco->numLandings()++;
						vec3 dir((float)(pm + pp - mp - mm), (float)(mp + pp - mm - pm), 0.5f);
						float fall = -actor->Velocity().z;
						float speed = fall >= 30.0f ? std::min(fall, actor->CollisionRadius() + 30.0f) : 30.0f;
						actor->Velocity() = dir * (2.0f * speed);
						return;
					}
				}
				static PropertyDataOffset carcassFlags = PropertyOffset("Engine.Carcass", "bSlidingCarcass");
				if (UObject::TryCast<UCarcass>(actor) && hitNormal.z < 0.9f && actor->BoolValue(carcassFlags))
				{
					if (Frand() < 0.2f)
						deco->numLandings()++;
					actor->Velocity() = hitNormal * 120.0f;
					actor->Velocity().z = 70.0f;
					return;
				}
				deco->numLandings() = 0;
			}
		}

		CallEvent(actor, EventName::Landed, { ExpressionValue::VectorValue(hitNormal) });
		if (actor->bDeleteMe())
			return;
		if (actor->Physics() == PHYS_Falling)
		{
			if (AsPawn(actor))
			{
				SetPhysics(actor, PHYS_Walking, hitActor);
			}
			else
			{
				SetPhysics(actor, PHYS_None, hitActor);
				actor->Velocity() = vec3(0.0f);
			}
		}
		if (actor->Physics() == PHYS_Walking)
		{
			if (UPawn* pawn = AsPawn(actor))
			{
				pawn->Acceleration() = SafeNormal(pawn->Acceleration());
				if (remainingTime > 0.0099999998f)
					PhysWalking(pawn, remainingTime, iterations);
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Ledge grab

	// The node a hit landed on: from Hit.Item along its coplanar chain, the first polygon facing the hit normal (>= 0.99)
	// that contains the hit location within tolerance of its edges (with tolerance 0: the last one facing it). A
	// mover's hit is taken into the brush's own space first.
	// IDA Engine.dll: not exported: sub_103FEBD0 [HP1 0x103FEBD0] (FCheckResult -> FBspNode*; the coplanar point-in-polygon search)
	static const BspNode* HitNode(const CheckResult& hit, float tolerance)
	{
		if (hit.Item == -1 || !hit.Model)
			return nullptr;
		UModel* model = hit.Model;
		vec3 location = hit.Location;
		vec3 normal = hit.Normal;
		if (hit.Actor && UObject::TryCast<UMover>(hit.Actor))
		{
			ModelFrame frame = ModelFrame::Brush(hit.Actor);
			location = (frame.WorldToLocal * vec4(location, 1.0f)).xyz();
			normal = SafeNormal((mat4::transpose(frame.LocalToWorld) * vec4(normal, 0.0f)).xyz());
		}

		const BspNode* candidate = nullptr;
		int index = hit.Item;
		while (index != -1 && index < (int)model->Nodes.size())
		{
			const BspNode& node = model->Nodes[index];
			vec3 planeNormal(node.PlaneX, node.PlaneY, node.PlaneZ);
			if (node.NumVertices != 0 && dot(planeNormal, normal) >= 0.99000001f)
			{
				candidate = &node;
				float side = 1.0f;
				int i = 0;
				vec3 prev = model->Points[model->Vertices[node.VertPool + node.NumVertices - 1].Vertex];
				for (; i < node.NumVertices; i++)
				{
					vec3 cur = model->Points[model->Vertices[node.VertPool + i].Vertex];
					vec3 edge = cur - prev;
					vec3 edgeNormal(edge.y * planeNormal.z - edge.z * planeNormal.y, edge.z * planeNormal.x - edge.x * planeNormal.z, edge.x * planeNormal.y - edge.y * planeNormal.x);
					float len2 = dot(edgeNormal, edgeNormal);
					if (len2 >= 0.0000000099999999f)
						edgeNormal = edgeNormal * (1.0f / std::sqrt(len2));
					if (-tolerance > (dot(edgeNormal, location) - dot(edgeNormal, cur)) * side)
					{
						if (i != 0)
							break;
						side = -1.0f;
					}
					prev = cur;
				}
				if (i == node.NumVertices)
					return &node;
			}
			if (node.Plane == index)
				break;
			index = node.Plane;
		}
		return tolerance == 0.0f ? candidate : nullptr;
	}

	// The flags of the surface a hit landed on. A mover keeps them on its brush polygons (its brush BSP's surfaces
	// have none in SurrealEngine's data); the original adds a mover's polygons to the level's BSP with their flags.
	// IDA Engine.dll: not exported: sub_103FF1B0 [HP1 0x103FF1B0] (FCheckResult -> FBspSurf*: the node's surface)
	static bool HitSurfaceFlags(const CheckResult& hit, float tolerance, uint32_t& flags)
	{
		const BspNode* node = HitNode(hit, tolerance);
		if (!node || node->Surf < 0)
			return false;
		const BspSurface& surf = hit.Model->Surfaces[node->Surf];
		flags = surf.PolyFlags;
		if (hit.Model->Polys && surf.BrushPoly >= 0 && (size_t)surf.BrushPoly < hit.Model->Polys->Polys.size() && hit.Model != engine->Level->Model)
			flags |= hit.Model->Polys->Polys[surf.BrushPoly].PolyFlags;
		return true;
	}

	static bool IsMountable(const CheckResult& hit, UPawn* pawn)
	{
		vec3 e = CylinderExtent(pawn);
		float maxExtent = std::max(std::max(std::abs(e.x), std::abs(e.y)), std::abs(e.z));
		uint32_t flags;
		return HitSurfaceFlags(hit, maxExtent, flags) && (flags & PF_SpecialPoly);
	}

	// Ledge grabbing: a mountable (PF_SpecialPoly) wall the pawn faces, with a floor at most MaxMountHeight up just
	// behind it and the way up and over clear: base on the wall's actor and Mount(ledge - Location).
	// IDA Engine.dll: ?Mount@APawn@@QAE_NABVFVector@@AAUFCheckResult@@@Z [HP1 0x103EBFB0]
	bool PawnMount(UPawn* pawn, const vec3& delta, const CheckResult& hit)
	{
		float maxMount = MaxMountHeight(pawn);
		if (maxMount <= 0.0f)
			return false;
		if (hit.Normal.z <= -0.1f || hit.Normal.z >= 0.7f)
			return false;
		vec3 x, y, z;
		Coords::Rotation(pawn->Rotation()).GetAxes(x, y, z);
		if (dot(x, hit.Normal) >= 0.0f)
			return false;
		if (!IsMountable(hit, pawn))
			return false;

		vec3 into(-hit.Normal.x, -hit.Normal.y, 0.0f);
		float len2 = into.x * into.x + into.y * into.y;
		if (len2 >= 0.0000000099999999f)
			into = into * (1.0f / std::sqrt(len2));

		vec3 extent = CylinderExtent(pawn);
		vec3 top = pawn->Location() + delta * maxMount;
		vec3 start = top + into * (2.0f * pawn->CollisionRadius() + maxMount * hit.Normal.z);
		vec3 end = start - delta * maxMount - into * (maxMount * hit.Normal.z);
		CheckResult ledgeHit;
		if (SingleLineCheck(ledgeHit, pawn, end, start, TRACE_Movers | TRACE_Level, extent))
			return false;
		float minHeight = pawn->Physics() == PHYS_Falling ? 0.0f : pawn->MaxStepHeight();
		if (ledgeHit.Location.z - pawn->Location().z < minHeight)
			return false;

		vec3 dest(ledgeHit.Location.x, ledgeHit.Location.y, ledgeHit.Location.z + 2.0f);
		vec3 up(top.x, top.y, dest.z);
		CheckResult clear;
		if (!SingleLineCheck(clear, pawn, up, pawn->Location(), TRACE_Movers | TRACE_Level, extent) ||
			!SingleLineCheck(clear, pawn, dest, up, TRACE_Movers | TRACE_Level, extent))
			return false;

		pawn->SetBase(hit.Actor, true);
		CallEvent(pawn, NameString("Mount"), { ExpressionValue::VectorValue(dest - pawn->Location()) });
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Falling

	// Steps of at most 0.1 s (half the time left), up to 8 per call. Velocity is integrated with half the gravity
	// and acceleration (then averaged with the step's displacement): in water with buoyancy and fluid friction, a bobbing
	// decoration with half gravity, a player whose feet are in water (falling) with that zone's friction. A step is split
	// where the vertical velocity changes sign. Hits: bounce (HitWall), land on a floor (normal.z > 0.7), else a ledge
	// grab (Mount), HitWall, and slide along the wall(s). Air control: AirControl * AccelRate, up to GroundSpeed.
	// IDA Engine.dll: ?physFalling@AActor@@QAEXMH@Z [HP1 0x103EEA20]
	void PhysFalling(UActor* actor, float deltaTime, int iterations)
	{
		UPawn* pawn = AsPawn(actor);
		if (actor->Region().ZoneNumber == 0)
		{
			if (actor->Role() == ROLE_Authority && (UObject::TryCast<UInventory>(actor) || IsDecoration(actor) || pawn))
				LogMessage(actor->Name.ToString() + " fell out of the world! at " + std::to_string(actor->Location().x) + "," + std::to_string(actor->Location().y) + "," + std::to_string(actor->Location().z));
			CallEvent(actor, EventName::FellOutOfWorld);
			return;
		}

		vec3 savedAcceleration = actor->Acceleration();
		float maxHorizontalSpeed = 0.0f;
		if (pawn)
		{
			float airControl = pawn->AirControl();
			if (airControl > 0.15000001f)
			{
				// Less air control when a wall is just ahead
				vec3 push = SafeNormal(pawn->Acceleration()) * (airControl * pawn->AccelRate());
				vec3 ahead(pawn->Location().x + (push.x + pawn->Velocity().x) * deltaTime, pawn->Location().y + (push.y + pawn->Velocity().y) * deltaTime, pawn->Location().z);
				CheckResult hit;
				SingleLineCheck(hit, pawn, ahead, pawn->Location(), TRACE_Movers | TRACE_Level, CylinderExtent(pawn));
				if (hit.Actor)
					airControl = 0.050000001f;
			}
			float maxAccel = airControl * pawn->AccelRate();
			pawn->Acceleration().z = 0.0f;
			float speed2D = std::sqrt(pawn->Velocity().x * pawn->Velocity().x + pawn->Velocity().y * pawn->Velocity().y);
			if (speed2D < 10.0f)
				maxAccel += (10.0f - speed2D) / deltaTime;
			else if (speed2D >= pawn->GroundSpeed())
			{
				if (airControl > 0.050000001f)
					maxHorizontalSpeed = speed2D;
				else
					maxAccel = 1.0f;
			}
			if (maxAccel * maxAccel < dot(pawn->Acceleration(), pawn->Acceleration()))
				pawn->Acceleration() = SafeNormal(pawn->Acceleration()) * maxAccel;
		}

		float remaining = deltaTime;
		int bounces = 0;
		bool split = false;
		CheckResult hit;
		while (remaining > 0.0f && iterations < 8)
		{
			iterations++;
			float timeTick = remaining <= 0.1f ? remaining : std::min(remaining * 0.5f, 0.1f);
			actor->OldLocation() = actor->Location();
			actor->bJustTeleported() = false;
			vec3 oldVelocity = actor->Velocity();
			remaining -= timeTick;

			auto integrate = [&](float dt)
			{
				UZoneInfo* z = actor->Region().Zone;
				if (z->bWaterZone())
				{
					float mass = std::max(actor->Mass(), 1.0f);
					vec3 g = z->ZoneGravity() * (1.0f - actor->Buoyancy() / mass);
					actor->Velocity() = oldVelocity * (1.0f - 2.0f * dt * z->ZoneFluidFriction()) + (g + actor->Acceleration()) * 0.5f * dt;
					return;
				}
				UDecoration* deco = UObject::TryCast<UDecoration>(actor);
				if (deco && deco->bBobbing())
				{
					actor->Velocity() = oldVelocity + (z->ZoneGravity() * 0.5f + actor->Acceleration()) * 0.5f * dt;
					return;
				}
				if (IsPlayerPawn(actor))
				{
					UZoneInfo* feet = static_cast<UPawn*>(actor)->FootRegion().Zone;
					if (feet && feet->bWaterZone() && oldVelocity.z < 0.0f)
					{
						actor->Velocity() = oldVelocity * (1.0f - dt * feet->ZoneFluidFriction()) + (z->ZoneGravity() + actor->Acceleration()) * 0.5f * dt;
						return;
					}
				}
				actor->Velocity() = oldVelocity + (z->ZoneGravity() + actor->Acceleration()) * 0.5f * dt;
			};
			integrate(timeTick);

			// Split the step at the top of the arc
			if (!split && (oldVelocity.z > 0.0f) != (actor->Velocity().z > 0.0f) && std::abs(oldVelocity.z) > 5.0f && std::abs(actor->Velocity().z) > 5.0f)
			{
				split = true;
				float part = std::abs(oldVelocity.z) / (std::abs(actor->Velocity().z) + std::abs(oldVelocity.z));
				float first = part * timeTick;
				float rest = (1.0f - part) * timeTick;
				if (first > 0.015f && rest > 0.015f)
				{
					timeTick = first;
					remaining += rest;
					integrate(timeTick);
				}
			}
			else
			{
				split = false;
			}

			if (maxHorizontalSpeed != 0.0f)
			{
				vec3 v2(actor->Velocity().x, actor->Velocity().y, 0.0f);
				if (maxHorizontalSpeed * maxHorizontalSpeed < dot(v2, v2))
				{
					v2 = SafeNormal(v2) * maxHorizontalSpeed;
					actor->Velocity() = vec3(v2.x, v2.y, actor->Velocity().z);
				}
			}

			vec3 zoneVelocity(0.0f);
			if (!actor->bIsPawn() || !IsPlayerPawn(actor) || dot(actor->Region().Zone->ZoneVelocity(), actor->Region().Zone->ZoneVelocity()) > 40000.0f)
				zoneVelocity = actor->Region().Zone->ZoneVelocity();
			vec3 adjusted = (actor->Velocity() + zoneVelocity) * timeTick;

			MoveBy(actor, adjusted, hit);
			if (actor->bDeleteMe())
				return;
			if (pawn && actor->Physics() == PHYS_Swimming)
			{
				remaining += (1.0f - hit.Time) * timeTick;
				StartSwimming(pawn, oldVelocity, timeTick, remaining, iterations);
				return;
			}

			if (hit.Time < 1.0f)
			{
				if (hit.Actor && IsPlayerPawn(hit.Actor))
				{
					if (UDecoration* deco = UObject::TryCast<UDecoration>(actor))
						deco->numLandings() = std::max(deco->numLandings() - 1, 0);
				}

				if (actor->bBounce())
				{
					CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor) });
					if (actor->bDeleteMe() || actor->Physics() == PHYS_None)
						return;
					if (bounces < 2)
						remaining += (1.0f - hit.Time) * timeTick;
					bounces++;
				}
				else if (hit.Normal.z > 0.7f)
				{
					remaining += (1.0f - hit.Time) * timeTick;
					if (!actor->bJustTeleported() && hit.Time > 0.1f && hit.Time * timeTick > 0.003f)
						actor->Velocity() = (actor->Location() - actor->OldLocation()) / (hit.Time * timeTick);
					ProcessLanded(actor, hit.Normal, hit.Actor, remaining, iterations);
					return;
				}
				else
				{
					if (pawn)
						PawnMount(pawn, vec3(0.0f, 0.0f, 1.0f), hit);
					ProcessHitWall(actor, hit.Normal, hit.Actor);
					if (actor->bDeleteMe())
						return;
					vec3 oldHitNormal = hit.Normal;
					vec3 delta = (adjusted - hit.Normal * dot(adjusted, hit.Normal)) * (1.0f - hit.Time);
					if (dot(delta, adjusted) >= 0.0f)
					{
						MoveBy(actor, delta, hit);
						if (actor->bDeleteMe())
							return;
						if (hit.Time < 1.0f)
						{
							if (hit.Normal.z > 0.69999999f)
							{
								ProcessLanded(actor, hit.Normal, hit.Actor, 0.0f, iterations);
								return;
							}
							ProcessHitWall(actor, hit.Normal, hit.Actor);
							if (actor->bDeleteMe())
								return;
							vec3 desiredDir = SafeNormal(adjusted);
							TwoWallAdjust(desiredDir, delta, hit.Normal, oldHitNormal, hit.Time);
							bool ditch = oldHitNormal.z > 0.0f && hit.Normal.z > 0.0f && delta.z == 0.0f && dot(oldHitNormal, hit.Normal) < 0.0f;
							MoveBy(actor, delta, hit);
							if (actor->bDeleteMe())
								return;
							if (ditch || hit.Normal.z > 0.7f)
							{
								ProcessLanded(actor, hit.Normal, hit.Actor, 0.0f, iterations);
								return;
							}
						}
					}
					vec3 moved = (actor->Location() - actor->OldLocation()) * (1.0f / timeTick);
					oldVelocity = vec3(moved.x, moved.y, oldVelocity.z);
				}
			}

			if (!actor->bBounce() && !actor->bJustTeleported())
			{
				actor->Velocity() = (actor->Location() - actor->OldLocation()) / timeTick - zoneVelocity;
				if (actor->Velocity().z < oldVelocity.z || oldVelocity.z >= 0.0f)
					actor->Velocity() = actor->Velocity() * 2.0f - oldVelocity;
				float terminal = actor->Region().Zone->ZoneTerminalVelocity();
				if (dot(actor->Velocity(), actor->Velocity()) > terminal * terminal)
					actor->Velocity() = SafeNormal(actor->Velocity()) * terminal;
			}
		}
		actor->Acceleration() = savedAcceleration;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Walking

	// Velocity from acceleration and friction. Accelerating (or not braking): acceleration capped at AccelRate (a walking
	// player: 30% of it), velocity turned towards the acceleration by friction. Braking: friction in 0.03 s steps,
	// stopping dead under 10 or on reversal. Max speed: MaxSpeed (non-players times DesiredSpeed); a walking player
	// slows towards 30% of it.
	// IDA Engine.dll: ?calcVelocity@APawn@@AAEXVFVector@@MMMHHH@Z [HP1 0x103EB3E0]
	void CalcVelocity(UPawn* pawn, const vec3& accelDir, float deltaTime, float maxSpeed, float friction, bool fluid, bool brake, bool buoyant)
	{
		float effectiveFriction = std::max(friction, fluid ? 1.0f : 0.0f);
		bool walkingPlayer = IsPlayerPawn(pawn) && pawn->bIsWalking();
		vec3& velocity = pawn->Velocity();

		if (!brake || !IsZero(pawn->Acceleration()))
		{
			float speed = length(velocity);
			float accelRate = pawn->AccelRate();
			float accel2 = dot(pawn->Acceleration(), pawn->Acceleration());
			if (walkingPlayer)
			{
				if (accelRate * accelRate * 0.090000004f < accel2)
					pawn->Acceleration() = accelDir * accelRate * 0.30000001f;
			}
			else if (accelRate * accelRate < accel2)
			{
				pawn->Acceleration() = accelDir * accelRate;
			}
			velocity = velocity - (velocity - accelDir * speed) * deltaTime * effectiveFriction;
		}
		else
		{
			vec3 oldVelocity = velocity;
			vec3 sum(0.0f);
			float remaining = deltaTime;
			while (remaining > 0.029999999f)
			{
				velocity = velocity - velocity * 2.0f * 0.029999999f * effectiveFriction;
				if (dot(velocity, oldVelocity) > 0.0f)
					sum += velocity * 0.029999999f * (1.0f / deltaTime);
				remaining -= 0.029999999f;
			}
			velocity = velocity - velocity * 2.0f * remaining * effectiveFriction;
			if (dot(velocity, oldVelocity) > 0.0f)
				sum += velocity * remaining * (1.0f / deltaTime);
			velocity = sum;
			if (dot(velocity, oldVelocity) < 0.0f || dot(velocity, velocity) < 100.0f)
				velocity = vec3(0.0f);
		}

		velocity = velocity * (1.0f - (fluid ? 1.0f : 0.0f) * deltaTime * friction) + pawn->Acceleration() * deltaTime;
		if (!IsPlayerPawn(pawn))
			maxSpeed *= pawn->DesiredSpeed();
		if (buoyant)
			velocity += pawn->Region().Zone->ZoneGravity() * deltaTime * (1.0f - pawn->Buoyancy() / pawn->Mass());

		if (walkingPlayer && maxSpeed * maxSpeed * 0.090000004f < dot(velocity, velocity))
		{
			float speed = length(velocity);
			velocity = velocity / speed;
			float newSpeed = std::max(maxSpeed * 0.30000001f, (1.0f - 2.0f * effectiveFriction * deltaTime) * speed);
			velocity = velocity * newSpeed;
			return;
		}
		if (maxSpeed * maxSpeed < dot(velocity, velocity))
			velocity = SafeNormal(velocity) * maxSpeed;
	}

	// Up MaxStepHeight, the move, and back down. A player pushes a pushable decoration hit head on (velocity shared by
	// mass); a steep or long hit slides along the wall(s), a short shallow one steps up again; down, a steep floor
	// (normal.z < 0.5) slides it off. A ledge grab first.
	// IDA Engine.dll: ?stepUp@APawn@@AAEXVFVector@@00AAUFCheckResult@@@Z [HP1 0x103EC690]
	static void StepUp(UPawn* pawn, const vec3& gravDir, const vec3& desiredDir, vec3 delta, CheckResult& hit)
	{
		if (PawnMount(pawn, -gravDir, hit))
			return;

		vec3 down = gravDir * pawn->MaxStepHeight();
		MoveBy(pawn, -down, hit);
		MoveBy(pawn, delta, hit);
		if (pawn->bDeleteMe())
			return;
		if (hit.Time < 1.0f)
		{
			UDecoration* deco = hit.Actor ? UObject::TryCast<UDecoration>(hit.Actor) : nullptr;
			if (IsPlayerPawn(pawn) && deco && deco->bPushable() && dot(desiredDir, hit.Normal) < -0.9f)
			{
				pawn->bJustTeleported() = true;
				pawn->Velocity() = pawn->Velocity() * (pawn->Mass() / (deco->Mass() + pawn->Mass()));
				ProcessHitWall(pawn, hit.Normal, hit.Actor);
				if (pawn->Physics() == PHYS_Falling || pawn->bDeleteMe())
					return;
			}
			else if (std::abs(hit.Normal.z) >= 0.2f || dot(delta, delta) * hit.Time <= 144.0f)
			{
				ProcessHitWall(pawn, hit.Normal, hit.Actor);
				if (pawn->bDeleteMe())
					return;
				vec3 oldHitNormal = hit.Normal;
				vec3 slide = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Time);
				if (dot(slide, delta) >= 0.0f)
				{
					delta = slide;
					MoveBy(pawn, delta, hit);
					if (hit.Time < 1.0f)
					{
						ProcessHitWall(pawn, hit.Normal, hit.Actor);
						if (pawn->Physics() == PHYS_Falling || pawn->bDeleteMe())
							return;
						TwoWallAdjust(desiredDir, delta, hit.Normal, oldHitNormal, hit.Time);
						MoveBy(pawn, delta, hit);
					}
				}
			}
			else
			{
				StepUp(pawn, gravDir, desiredDir, delta * (1.0f - hit.Time), hit);
				if (pawn->Physics() == PHYS_Falling || pawn->bDeleteMe())
					return;
			}
		}

		MoveBy(pawn, down, hit);
		if (hit.Time < 1.0f && hit.Normal.z < 0.5f)
		{
			vec3 slide = (down - hit.Normal * dot(down, hit.Normal)) * (1.0f - hit.Time);
			if (dot(slide, down) >= 0.0f)
				MoveBy(pawn, slide, hit);
		}
	}

	// Where a fall from start with velocity ends within maxTime: steps of at most 0.2 s (10 at most) sweeping the
	// pawn's cylinder, sliding along walls; stops on a floor (normal.z > 0.7) or a mountable wall the pawn faces.
	// IDA Engine.dll: not exported: sub_103E6310 [HP1 0x103E6310] (thunk sub_10301929; called twice from APawn::physWalking_0's auto-jump test)
	static vec3 PredictLanding(UPawn* pawn, vec3 start, vec3 velocity, const vec3& extent, float maxTime)
	{
		vec3 normal(0.0f);
		float time = 0.0f;
		for (int step = 0; step < 10 && time < maxTime; step++)
		{
			float dt = std::min(maxTime - time, 0.2f);
			vec3 g = pawn->Region().Zone->ZoneGravity();
			g = g - normal * dot(g, normal);
			vec3 end = start + velocity * dt + g * (0.5f * dt * dt);
			CheckResult hit;
			SingleLineCheck(hit, pawn, end, start, TRACE_Blocking | TRACE_Movers | TRACE_Level, extent);
			float t = dt * hit.Time;
			time += t;
			velocity += g * t;
			if (hit.Time >= 1.0f)
			{
				normal = vec3(0.0f);
				start = end;
				continue;
			}
			start = start * (1.0f - hit.Time) + end * hit.Time;
			normal = hit.Normal;
			if (normal.z > 0.69999999f)
				return start;
			if (MaxMountHeight(pawn) > 0.0f && normal.z > -0.1f)
			{
				vec3 x, y, z;
				Coords::Rotation(pawn->Rotation()).GetAxes(x, y, z);
				CheckResult at = hit;
				at.Location = start;
				if (dot(x, normal) < 0.0f && IsMountable(at, pawn))
					return start;
			}
			velocity -= normal * dot(velocity, normal);
		}
		return start;
	}

	// Steps of at most 0.05 s (players: whole steps when slow), up to 8 per call. calcVelocity with ground friction;
	// ZoneVelocity x25 (players only when over 300). AI pawns (and walking players) look ahead for ledges: back off,
	// slide along the edge, MayFall, or stop. Players test for sloped walls (bHitSlopedWall) and slide along them. A
	// hit steps up (stepUp, with ledge grab); then the floor (MaxStepHeight + 2 down): kept, snapped to, slid down a
	// slope, or lost (Falling, physFalling; a pawn that can't jump or walks is put back, MoveTimer -1). PlayerPawn.bAutoJump
	// jumps off an edge when jumping lands more than 10 higher.
	// IDA Engine.dll: ?physWalking@APawn@@QAEXMH@Z [HP1 0x103E6B60]
	void PhysWalking(UPawn* pawn, float deltaTime, int iterations)
	{
		if (pawn->Region().ZoneNumber == 0)
		{
			if (pawn->Role() == ROLE_Authority)
				LogMessage(pawn->Name.ToString() + " fell out of the world! at " + std::to_string(pawn->Location().x) + "," + std::to_string(pawn->Location().y) + "," + std::to_string(pawn->Location().z) + " from " + std::to_string(pawn->OldLocation().x) + "," + std::to_string(pawn->OldLocation().y) + "," + std::to_string(pawn->OldLocation().z));
			CallEvent(pawn, EventName::FellOutOfWorld);
			return;
		}

		pawn->Acceleration().z = 0.0f;
		vec3 accelDir = IsZero(pawn->Acceleration()) ? pawn->Acceleration() : SafeNormal(pawn->Acceleration());
		UZoneInfo* zone = pawn->Region().Zone;
		CalcVelocity(pawn, accelDir, deltaTime, pawn->GroundSpeed(), zone->ZoneGroundFriction(), false, true, false);
		vec3 desiredMove = pawn->Velocity();
		if (!IsPlayerPawn(pawn) || dot(zone->ZoneVelocity(), zone->ZoneVelocity()) > 90000.0f)
			desiredMove += zone->ZoneVelocity() * 25.0f * deltaTime;

		vec3 gravDir(0.0f, 0.0f, zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f);
		vec3 down = gravDir * (pawn->MaxStepHeight() + 2.0f);
		vec3 extent = CylinderExtent(pawn);
		vec3 floorNormal(0.0f, 0.0f, 1.0f);
		CheckResult hit;
		pawn->OldLocation() = pawn->Location();
		pawn->bJustTeleported() = false;
		bool checkedFall = false;
		bool mustJump = false;
		float remaining = deltaTime;

		while (true)
		{
			if (remaining <= 0.0f || iterations >= 8)
			{
				if (!pawn->bJustTeleported())
				{
					pawn->Velocity() = (pawn->Location() - pawn->OldLocation()) * (1.0f / deltaTime);
					float speed = length(pawn->Velocity());
					if (speed > 0.0f && floorNormal.z > 0.0f)
					{
						pawn->Velocity().z -= dot(floorNormal, pawn->Velocity()) / floorNormal.z;
						float newSpeed = length(pawn->Velocity());
						if (newSpeed > 0.0f)
							pawn->Velocity() *= speed / newSpeed;
					}
				}
				return;
			}
			iterations++;

			float timeTick;
			if (remaining > 0.050000001f && (!IsPlayerPawn(pawn) || dot(desiredMove, desiredMove) * remaining * remaining > 400.0f))
				timeTick = std::min(remaining * 0.5f, 0.050000001f);
			else
				timeTick = remaining;
			remaining -= timeTick;

			vec3 delta = desiredMove * timeTick;
			vec3 oldDelta = delta;
			vec3 stepStart = pawn->Location();
			bool zeroDelta = std::abs(delta.x) < 0.000099999997f && std::abs(delta.y) < 0.000099999997f && std::abs(delta.z) < 0.000099999997f;

			if (zeroDelta)
			{
				pawn->bHitSlopedWall() = false;
				remaining = 0.0f;
			}
			else
			{
				// Ledges (AI pawns that can't fly, and walking players)
				float radius = pawn->CollisionRadius();
				vec3 checkDir = accelDir * radius;
				if (!pawn->bAvoidLedges())
					checkDir *= 0.5f;
				bool ledgeLogic = (!IsPlayerPawn(pawn) || pawn->bIsWalking()) && !pawn->bCanFly();
				if (ledgeLogic)
				{
					CheckResult h;
					vec3 probe = pawn->Location() + delta + checkDir;
					SingleLineCheck(h, pawn, probe, pawn->Location(), TRACE_Movers | TRACE_Level, vec3(0.0f));
					float deltaLen = length(delta);
					float stepRadius = std::min(radius * 0.5f, 14.0f);
					if (h.Time == 1.0f)
					{
						float dropTest = std::max(pawn->CollisionHeight() + radius + deltaLen + 4.0f, pawn->CollisionHeight() + pawn->MaxStepHeight() + 4.0f);
						SingleLineCheck(h, pawn, probe + gravDir * dropTest, probe, TRACE_Movers | TRACE_Level, vec3(0.0f));
						if (h.Time == 1.0f || (h.Normal.z > 0.7f && pawn->CollisionHeight() + pawn->MaxStepHeight() + 4.0f < dropTest * h.Time &&
							(deltaLen + radius) * std::sqrt(1.0f - h.Normal.z * h.Normal.z) / h.Normal.z + pawn->CollisionHeight() + 4.0f < dropTest * h.Time))
						{
							SingleLineCheck(h, pawn, probe + gravDir * (pawn->MaxStepHeight() + 4.0f), probe, TRACE_Movers | TRACE_Level, vec3(stepRadius, stepRadius, pawn->CollisionHeight()));
						}
					}
					if (h.Time == 1.0f)
					{
						// A ledge ahead
						vec3 deltaDir = delta / deltaLen;
						probe = pawn->Location() + accelDir * deltaLen + checkDir;
						vec3 under = probe + gravDir * (pawn->CollisionHeight() + 6.0f);
						vec3 back = pawn->Location() + gravDir * (pawn->CollisionHeight() + 6.0f) - accelDir * (2.0f * radius);
						SingleLineCheck(h, pawn, back, under, TRACE_Movers | TRACE_Level, vec3(0.0f));
						vec3 stepDown = gravDir * (pawn->MaxStepHeight() + 6.0f);
						bool sideOk = false;
						bool wantFall = false;
						if (h.Time >= 1.0f)
						{
							// No edge face: try a side step
							vec3 side(deltaDir.y, -deltaDir.x, 0.0f);
							wantFall = true;
							float speed = std::min(pawn->DesiredSpeed(), 0.80000001f) * pawn->GroundSpeed() * timeTick;
							delta = side * speed;
							vec3 sideProbe = pawn->Location() + delta;
							SingleLineCheck(h, pawn, sideProbe, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
							if (h.Time == 1.0f)
							{
								SingleLineCheck(h, pawn, sideProbe + stepDown, sideProbe, TRACE_Movers | TRACE_Level, extent);
								if (h.Time == 1.0f)
									delta = -delta;
								else
									sideOk = true;
							}
							else
							{
								sideOk = true;
							}
						}
						else if (pawn->bAvoidLedges())
						{
							vec3 away(h.Normal.x, h.Normal.y, 0.0f);
							delta = away * (pawn->GroundSpeed() * pawn->DesiredSpeed() * timeTick * -1.0f);
							if (pawn->bStopAtLedges())
								pawn->MoveTimer() = -1.0f;
							else
								pawn->MoveTimer() -= 0.25f;
						}
						else
						{
							vec3 side = SafeNormal(vec3(h.Normal.y, -h.Normal.x, 0.0f));
							if (dot(side, accelDir) < 0.0f)
								side = -side;
							float along = dot(side, accelDir);
							wantFall = along < 0.5f || (pawn->bCanJump() && along < 0.7f);
							float speed = along >= 0.7f ? pawn->GroundSpeed() * pawn->DesiredSpeed() * timeTick : std::min(pawn->DesiredSpeed(), 0.80000001f) * pawn->GroundSpeed() * timeTick;
							delta = side * speed;
						}

						if (IsPlayerPawn(pawn))
						{
							// A walking player stops at the edge
							wantFall = false;
							if (!sideOk)
							{
								vec3 p = pawn->Location() + delta + checkDir;
								SingleLineCheck(h, pawn, p, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
								if (h.Time == 1.0f)
								{
									SingleLineCheck(h, pawn, p + stepDown, p, TRACE_Movers | TRACE_Level, vec3(stepRadius, stepRadius, pawn->CollisionHeight()));
									if (h.Time == 1.0f)
									{
										pawn->Acceleration() = vec3(0.0f);
										delta = vec3(0.0f);
									}
								}
							}
						}

						bool fall = wantFall;
						bool skipToMove = false;
						if (pawn->bCanJump() && wantFall)
						{
							if (pawn->IsEventEnabled(EventName::MayFall))
							{
								if (!checkedFall)
								{
									fall = false;
									checkedFall = true;
									CallEvent(pawn, EventName::MayFall);
									if (pawn->bDeleteMe())
										return;
									if (pawn->bCanJump())
									{
										mustJump = true;
										delta = accelDir * deltaLen;
									}
								}
							}
							else
							{
								delta = accelDir * deltaLen;
							}
						}
						if (pawn->bCanJump())
							skipToMove = true;

						if (!skipToMove)
						{
							auto stopAtLedge = [&]()
							{
								if (2.0f * timeTick <= Frand())
									pawn->MoveTimer() -= 0.1f;
								else
									pawn->MoveTimer() = -1.0f;
								if (sideOk)
									return;
								vec3 p = pawn->Location() + delta + checkDir;
								SingleLineCheck(h, pawn, p, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
								if (h.Time == 1.0f)
									SingleLineCheck(h, pawn, p + stepDown, p, TRACE_Movers | TRACE_Level, extent);
								else if (dot(deltaDir, h.Normal) < pawn->MinHitWall())
									pawn->MoveTimer() = -1.0f;
								if (h.Time != 1.0f)
									return;
								SingleLineCheck(h, pawn, pawn->Location() + stepDown, pawn->Location(), TRACE_Movers | TRACE_Level, vec3(stepRadius, stepRadius, pawn->CollisionHeight()));
								bool noFloorBelow = h.Time == 1.0f;
								pawn->Acceleration() = vec3(0.0f);
								remaining = 0.0f;
								pawn->MoveTimer() = -1.0f;
								delta = noFloorBelow ? deltaDir * (timeTick * pawn->GroundSpeed() * -1.0f) : vec3(0.0f);
							};

							if (fall)
							{
								vec3 p = pawn->Location() + deltaDir * (deltaLen + radius);
								SingleLineCheck(h, pawn, p, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
								if (h.Time == 1.0f)
								{
									SingleLineCheck(h, pawn, p + stepDown, p, TRACE_Movers | TRACE_Level, extent);
									if (h.Time >= 1.0f)
									{
										stopAtLedge();
									}
									else
									{
										vec3 p2 = pawn->Location() + deltaDir * deltaLen;
										SingleLineCheck(h, pawn, p2 + stepDown, p2, TRACE_Movers | TRACE_Level, extent);
										if (h.Time < 1.0f)
											delta = deltaDir * deltaLen;
										else
											stopAtLedge();
									}
								}
								else
								{
									delta = deltaDir * deltaLen;
								}
							}
							else
							{
								// stopAtLedge without the MoveTimer roll
								if (!sideOk)
								{
									vec3 p = pawn->Location() + delta + checkDir;
									SingleLineCheck(h, pawn, p, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
									if (h.Time == 1.0f)
										SingleLineCheck(h, pawn, p + stepDown, p, TRACE_Movers | TRACE_Level, extent);
									else if (dot(deltaDir, h.Normal) < pawn->MinHitWall())
										pawn->MoveTimer() = -1.0f;
									if (h.Time == 1.0f)
									{
										SingleLineCheck(h, pawn, pawn->Location() + stepDown, pawn->Location(), TRACE_Movers | TRACE_Level, vec3(stepRadius, stepRadius, pawn->CollisionHeight()));
										bool noFloorBelow = h.Time == 1.0f;
										pawn->Acceleration() = vec3(0.0f);
										remaining = 0.0f;
										pawn->MoveTimer() = -1.0f;
										delta = noFloorBelow ? deltaDir * (timeTick * pawn->GroundSpeed() * -1.0f) : vec3(0.0f);
									}
								}
							}
						}
					}
					oldDelta = delta;
				}

				// Sloped walls
				if (pawn->bHitSlopedWall())
				{
					float len = length(delta);
					vec3 dir = delta * (1.0f / len);
					float probeLen = std::max(len + 4.0f, 30.0f);
					CheckResult h;
					SingleLineCheck(h, pawn, pawn->Location() + dir * probeLen, pawn->Location(), TRACE_Movers | TRACE_Level, extent);
					auto sloped = [&]() { return h.Time < 1.0f && h.Normal.z > 0.0099999998f && h.Normal.z < 0.69999999f; };
					pawn->bHitSlopedWall() = sloped();
					if (pawn->bHitSlopedWall())
					{
						vec3 n = SafeNormal(vec3(h.Normal.x, h.Normal.y, 0.0f));
						delta = delta - n * dot(delta, n);
					}
					else if (IsPlayerPawn(pawn))
					{
						vec3 from(pawn->Location().x, pawn->Location().y, pawn->Location().z - pawn->CollisionHeight() + pawn->MaxStepHeight() + 4.0f);
						SingleLineCheck(h, pawn, from + dir * 100.0f, from, TRACE_Movers | TRACE_Level, vec3(0.0f));
						pawn->bHitSlopedWall() = sloped();
						if (!pawn->bHitSlopedWall())
						{
							vec3 d1 = SafeNormal(vec3(dir.x + dir.y, dir.y - dir.x, dir.z));
							SingleLineCheck(h, pawn, from + d1 * 100.0f, from, TRACE_Movers | TRACE_Level, vec3(0.0f));
							pawn->bHitSlopedWall() = sloped();
						}
						if (!pawn->bHitSlopedWall())
						{
							vec3 d2 = SafeNormal(vec3(dir.x - dir.y, dir.x + dir.y, dir.z));
							SingleLineCheck(h, pawn, from + d2 * 100.0f, from, TRACE_Movers | TRACE_Level, vec3(0.0f));
							pawn->bHitSlopedWall() = sloped();
						}
					}
					MoveBy(pawn, delta, hit);
				}
				else
				{
					MoveBy(pawn, delta, hit);
					pawn->bHitSlopedWall() = hit.Time < 1.0f && hit.Normal.z > 0.0099999998f && hit.Normal.z < 0.69999999f;
				}
				if (pawn->bDeleteMe())
					return;

				if (hit.Time < 1.0f)
				{
					if (hit.Normal.z > 0.7f)
						floorNormal = hit.Normal;
					StepUp(pawn, gravDir, SafeNormal(delta), delta * (1.0f - hit.Time), hit);
					if (pawn->bDeleteMe())
						return;
					if (pawn->Physics() == PHYS_Falling)
					{
						float oldLen = length(oldDelta);
						vec3 moved = pawn->Location() - stepStart;
						float moved2D = std::sqrt(moved.x * moved.x + moved.y * moved.y);
						remaining += (1.0f - std::min(moved2D / oldLen, 1.0f)) * timeTick;
						CallEvent(pawn, EventName::Falling);
						if (pawn->bDeleteMe())
							return;
						if (pawn->Physics() == PHYS_Falling)
						{
							if (remaining > 0.0099999998f)
								PhysFalling(pawn, remaining, iterations);
						}
						else if (pawn->Physics() == PHYS_Flying)
						{
							pawn->Velocity() = vec3(0.0f, 0.0f, pawn->AirSpeed());
							pawn->Acceleration() = vec3(0.0f, 0.0f, pawn->AccelRate());
							if (remaining > 0.0099999998f)
								PhysFlying(pawn, remaining, iterations);
						}
						return;
					}
					if (pawn->Physics() == PHYS_Projectile)
						return;
				}
				if (pawn->Physics() == PHYS_Swimming)
				{
					StartSwimming(pawn, pawn->Velocity(), timeTick, remaining, iterations);
					return;
				}
			}

			// The floor
			bool skipFloor = false;
			if (AutoJump(pawn))
			{
				float below = -pawn->CollisionHeight() - 4.0f;
				if (zeroDelta)
				{
					// Standing still on the same base 4.1 to 4.6 below the feet: nothing to do
					vec3 feet(pawn->Location().x, pawn->Location().y, pawn->Location().z - pawn->CollisionHeight());
					CheckResult h;
					SingleLineCheck(h, pawn, feet - vec3(0.0f, 0.0f, 20.0f), feet, TRACE_Movers | TRACE_Level, vec3(0.0f));
					float dist = h.Time * 20.0f;
					skipFloor = pawn->ActorBase() == h.Actor && dist <= 4.5999999f && dist >= 4.0999999f;
				}
				else
				{
					CheckResult h;
					vec3 feetDown = vec3(pawn->Location().x, pawn->Location().y, pawn->Location().z - pawn->CollisionHeight()) + down;
					if (SingleLineCheck(h, pawn, feetDown, pawn->Location(), TRACE_Blocking | TRACE_Movers | TRACE_Level, vec3(0.0f)))
					{
						float back = pawn->CollisionRadius() * 1.5f;
						vec3 behind = pawn->Location() - accelDir * back;
						vec3 from(pawn->Location().x, pawn->Location().y, pawn->Location().z + below);
						vec3 to(behind.x, behind.y, behind.z + below);
						if (!SingleLineCheck(h, pawn, to, from, TRACE_Blocking | TRACE_Movers | TRACE_Level, vec3(0.0f)))
						{
							float facing = dot(h.Normal, accelDir);
							if (facing > 0.25f)
							{
								float t = h.Time;
								vec3 edge = pawn->Location() * (1.0f - t) + behind * t;
								vec3 dest = edge + accelDir * (back / facing);
								vec3 fall = PredictLanding(pawn, dest, pawn->Velocity(), extent, 2.0f);
								vec3 jump = PredictLanding(pawn, dest, pawn->Velocity() + vec3(0.0f, 0.0f, pawn->JumpZ()), extent, 2.0f);
								if (jump.z - fall.z > 10.0f)
								{
									CheckResult moveHit;
									MoveBy(pawn, dest - pawn->Location(), moveHit);
									CallEvent(pawn, NameString("DoJump"), { ExpressionValue::FloatValue(1.0f) });
									if (pawn->bDeleteMe())
										return;
									remaining += t * timeTick;
									PhysFalling(pawn, remaining, iterations);
									return;
								}
							}
						}
					}
				}
			}
			if (skipFloor)
				continue;

			SingleLineCheck(hit, pawn, pawn->Location() + down, pawn->Location(), TRACE_Blocking | TRACE_Movers | TRACE_Level, extent);
			float floorDist = (pawn->MaxStepHeight() + 2.0f) * hit.Time;
			if (hit.Time < 1.0f && hit.Normal.z > 0.69999999f)
				floorNormal = hit.Normal;
			if (hit.Time >= 1.0f || (hit.Actor == pawn->ActorBase() && floorDist <= 2.4000001f))
			{
				if (floorDist < 1.9f)
				{
					vec3 keepNormal = hit.Normal;
					MoveBy(pawn, vec3(0.0f, 0.0f, 2.0999999f - floorDist), hit);
					hit.Time = 0.0f;
					hit.Normal = keepNormal;
				}
			}
			else
			{
				MoveBy(pawn, down, hit);
				if (pawn->bDeleteMe())
					return;
				if (hit.Actor != pawn->ActorBase())
					pawn->SetBase(hit.Actor, true);
				if (pawn->Physics() == PHYS_Swimming)
				{
					StartSwimming(pawn, pawn->Velocity(), timeTick, remaining, iterations);
					return;
				}
			}

			if (mustJump || hit.Time >= 1.0f || hit.Normal.z < 0.69999999f)
			{
				if (!mustJump)
				{
					if (pawn->bCanJump() && !checkedFall && pawn->IsEventEnabled(EventName::MayFall))
					{
						checkedFall = true;
						CallEvent(pawn, EventName::MayFall);
						if (pawn->bDeleteMe())
							return;
					}
					if (!pawn->bCanJump() || pawn->bIsWalking())
					{
						pawn->Velocity() = vec3(0.0f);
						pawn->Acceleration() = vec3(0.0f);
						FarMoveActor(pawn, pawn->OldLocation(), false, false);
						pawn->MoveTimer() = -1.0f;
						return;
					}
				}

				if (hit.Time < 1.0f)
					pawn->bHitSlopedWall() = true;
				float oldLen = length(oldDelta);
				vec3 moved = pawn->Location() - stepStart;
				float moved2D = std::sqrt(moved.x * moved.x + moved.y * moved.y);
				if (oldLen == 0.0f)
					remaining = 0.0f;
				else
					remaining += (1.0f - std::min(moved2D / oldLen, 1.0f)) * timeTick;
				pawn->Velocity().z = 0.0f;
				CallEvent(pawn, EventName::Falling);
				if (pawn->bDeleteMe())
					return;
				if (pawn->Physics() == PHYS_Walking)
					SetPhysics(pawn, PHYS_Falling, nullptr);
				if (!mustJump && pawn->Physics() == PHYS_Falling)
				{
					float savedZ = pawn->Velocity().z;
					if (!pawn->bJustTeleported() && deltaTime > remaining)
						pawn->Velocity() = (pawn->Location() - pawn->OldLocation()) * (1.0f / (deltaTime - remaining));
					pawn->Velocity().z = savedZ;
					if (remaining > 0.0099999998f)
						PhysFalling(pawn, remaining, iterations);
					return;
				}
				CheckResult moveHit;
				MoveBy(pawn, desiredMove * remaining, moveHit);
				remaining = 0.0f;
			}
			else if (hit.Normal.z < 1.0f && hit.Normal.z * zone->ZoneGroundFriction() < 3.3f)
			{
				// Slide down a slope
				float friction = std::max(zone->ZoneGroundFriction(), 0.5f);
				vec3 slide = zone->ZoneGravity() * deltaTime * (1.0f / (friction * 2.0f)) * deltaTime;
				vec3 slideDelta = slide - hit.Normal * dot(slide, hit.Normal);
				if (dot(slideDelta, slide) >= 0.0f)
				{
					MoveBy(pawn, slideDelta, hit);
					if (pawn->bDeleteMe())
						return;
				}
				if (pawn->Physics() == PHYS_Swimming)
				{
					StartSwimming(pawn, pawn->Velocity(), timeTick, remaining, iterations);
					return;
				}
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Flying, swimming, projectiles, rolling

	static vec3 GravDir(UActor* actor)
	{
		return vec3(0.0f, 0.0f, actor->Region().Zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f);
	}

	// A hit while flying or swimming: a steep wall, or moving mostly up or down, slides along it (HitWall, a second wall);
	// otherwise it steps up like walking. Returns the time the slides used up (for swimming).
	template<typename MoveFunc>
	static void SlideOrStepUp(UPawn* pawn, const vec3& adjusted, CheckResult& hit, MoveFunc move)
	{
		vec3 gravDir = GravDir(pawn);
		vec3 desiredDir = SafeNormal(adjusted);
		float upDown = dot(gravDir, SafeNormal(pawn->Velocity()));
		if (std::abs(hit.Normal.z) >= 0.2f || upDown >= 0.5f || upDown <= -0.2f)
		{
			ProcessHitWall(pawn, hit.Normal, hit.Actor);
			if (pawn->bDeleteMe())
				return;
			vec3 oldHitNormal = hit.Normal;
			vec3 delta = (adjusted - hit.Normal * dot(adjusted, hit.Normal)) * (1.0f - hit.Time);
			if (dot(delta, adjusted) >= 0.0f)
			{
				move(delta);
				if (hit.Time < 1.0f && !pawn->bDeleteMe())
				{
					ProcessHitWall(pawn, hit.Normal, hit.Actor);
					if (pawn->bDeleteMe())
						return;
					TwoWallAdjust(desiredDir, delta, hit.Normal, oldHitNormal, hit.Time);
					move(delta);
				}
			}
		}
		else
		{
			float z = pawn->Location().z;
			StepUp(pawn, gravDir, desiredDir, adjusted * (1.0f - hit.Time), hit);
			pawn->OldLocation().z += pawn->Location().z - z;
		}
	}

	// calcVelocity with AirSpeed and fluid friction, ZoneVelocity (a player only over 300), the move; a hit slides or
	// steps up. A flyer that collides with the world and isn't a player is destroyed outside it.
	// IDA Engine.dll: ?physFlying@APawn@@QAEXMH@Z [HP1 0x103F13A0]
	void PhysFlying(UPawn* pawn, float deltaTime, int iterations)
	{
		if (pawn->bCollideWorld() && pawn->Region().ZoneNumber == 0)
		{
			if (!pawn->bIsPlayer())
			{
				LogMessage(pawn->Name.ToString() + " flew out of the world!");
				pawn->Destroy();
			}
			return;
		}

		vec3 accelDir = IsZero(pawn->Acceleration()) ? pawn->Acceleration() : SafeNormal(pawn->Acceleration());
		CalcVelocity(pawn, accelDir, deltaTime, pawn->AirSpeed(), pawn->Region().Zone->ZoneFluidFriction(), true, false, false);
		pawn->OldLocation() = pawn->Location();
		pawn->bJustTeleported() = false;

		UZoneInfo* zone = pawn->Region().Zone;
		vec3 zoneVelocity = (IsPlayerPawn(pawn) && dot(zone->ZoneVelocity(), zone->ZoneVelocity()) <= 90000.0f) ? vec3(0.0f) : zone->ZoneVelocity();
		vec3 adjusted = (pawn->Velocity() + zoneVelocity) * deltaTime;
		CheckResult hit;
		MoveBy(pawn, adjusted, hit);
		if (pawn->bDeleteMe())
			return;
		if (hit.Time < 1.0f)
			SlideOrStepUp(pawn, adjusted, hit, [&](const vec3& d) { MoveBy(pawn, d, hit); });
		if (!pawn->bDeleteMe() && !pawn->bJustTeleported())
			pawn->Velocity() = (pawn->Location() - pawn->OldLocation()) * (1.0f / deltaTime);
	}

	// The water line between start and end (bisected until they are within a unit): end moves to the last point
	// on the same side (water or not) as the pawn's zone.
	// IDA Engine.dll: ?findWaterLine@APawn@@QAEXVFVector@@AAV2@@Z [HP1 0x103F1ED0]
	static void FindWaterLine(UPawn* pawn, vec3 start, vec3& end)
	{
		while (true)
		{
			vec3 d = start - end;
			if (dot(d, d) < 1.0f)
				return;
			vec3 mid = (start + end) * 0.5f;
			UZoneInfo* zone = engine->Level->Model->FindRegion(mid, engine->LevelInfo).Zone;
			bool midWater = zone && zone->bWaterZone();
			bool myWater = pawn->Region().Zone && pawn->Region().Zone->bWaterZone();
			if (midWater == myWater)
				end = mid;
			else
				start = mid;
		}
	}

	// A swim move: out of the water, back to the water line. Returns the fraction of the move that was undone.
	// IDA Engine.dll: ?Swim@APawn@@AAEMVFVector@@AAUFCheckResult@@@Z [HP1 0x103F1C10]
	static float Swim(UPawn* pawn, const vec3& delta, CheckResult& hit)
	{
		vec3 start = pawn->Location();
		float undone = 0.0f;
		MoveBy(pawn, delta, hit);
		if (pawn->bDeleteMe())
			return 0.0f;
		vec3 end = pawn->Location();
		if (!pawn->Region().Zone->bWaterZone())
		{
			FindWaterLine(pawn, start, end);
			if (end != pawn->Location())
			{
				undone = length(end - pawn->Location()) / length(delta);
				MoveBy(pawn, end - pawn->Location(), hit);
			}
		}
		return undone;
	}

	// The head out of the water slows a rise; calcVelocity with WaterSpeed, fluid friction and buoyancy; ZoneVelocity
	// x25 (a player only over 300); Swim; a hit slides or steps up. Out of the water: falling, with a hop up.
	// IDA Engine.dll: ?physSwimming@APawn@@QAEXMH@Z [HP1 0x103F20A0]
	void PhysSwimming(UPawn* pawn, float deltaTime, int iterations)
	{
		UZoneInfo* head = pawn->HeadRegion().Zone;
		if (head && !head->bWaterZone() && pawn->Velocity().z > 100.0f)
			pawn->Velocity().z = (1.0f - deltaTime) * pawn->Velocity().z;
		iterations++;
		pawn->OldLocation() = pawn->Location();
		pawn->bJustTeleported() = false;
		vec3 accelDir = IsZero(pawn->Acceleration()) ? pawn->Acceleration() : SafeNormal(pawn->Acceleration());
		CalcVelocity(pawn, accelDir, deltaTime, pawn->WaterSpeed(), pawn->Region().Zone->ZoneFluidFriction(), true, false, true);
		float velocityZ = pawn->Velocity().z;

		UZoneInfo* zone = pawn->Region().Zone;
		vec3 zoneVelocity = (IsPlayerPawn(pawn) && dot(zone->ZoneVelocity(), zone->ZoneVelocity()) <= 90000.0f) ? vec3(0.0f) : zone->ZoneVelocity() * 25.0f * deltaTime;
		vec3 adjusted = (pawn->Velocity() + zoneVelocity) * deltaTime;
		CheckResult hit;
		float remaining = Swim(pawn, adjusted, hit) * deltaTime;
		if (pawn->bDeleteMe())
			return;
		if (hit.Time < 1.0f)
		{
			SlideOrStepUp(pawn, adjusted, hit, [&](const vec3& d)
			{
				float undone = Swim(pawn, d, hit);
				remaining = (1.0f - hit.Time) * undone * remaining;
			});
		}
		if (pawn->bDeleteMe())
			return;

		if (!pawn->bJustTeleported() && remaining < deltaTime)
		{
			bool keepZ = velocityZ != pawn->Velocity().z;
			velocityZ = pawn->Velocity().z;
			pawn->Velocity() = (pawn->Location() - pawn->OldLocation()) * (1.0f / (deltaTime - remaining));
			if (keepZ)
				pawn->Velocity().z = velocityZ;
		}
		if (!pawn->Region().Zone->bWaterZone())
		{
			if (pawn->Physics() == PHYS_Swimming)
				SetPhysics(pawn, PHYS_Falling, nullptr);
			if (pawn->Velocity().z < 160.0f && pawn->Velocity().z > 0.0f)
			{
				float speed2D = std::sqrt(pawn->Velocity().x * pawn->Velocity().x + pawn->Velocity().y * pawn->Velocity().y);
				pawn->Velocity().z = speed2D * 0.40000001f + 40.0f;
			}
		}
		if (remaining > 0.0099999998f)
		{
			if (pawn->Physics() == PHYS_Falling)
				PhysFalling(pawn, remaining, iterations);
			else if (pawn->Physics() == PHYS_Flying)
				PhysFlying(pawn, remaining, iterations);
		}
	}

	// Into the water: back to the water line on the way in, the velocity from the way in (averaged like falling, capped
	// at 4000), a dive slowed to at least 80 down, then physSwimming with the time left.
	// IDA Engine.dll: ?startSwimming@APawn@@QAEXVFVector@@MMH@Z [HP1 0x103F0EA0]
	static void StartSwimming(UPawn* pawn, const vec3& oldVelocity, float timeTick, float remainingTime, int iterations)
	{
		vec3 end = pawn->Location();
		FindWaterLine(pawn, pawn->OldLocation(), end);
		float waterTime = 0.0f;
		if (end != pawn->Location())
		{
			waterTime = length(end - pawn->Location()) * timeTick / length(pawn->Location() - pawn->OldLocation());
			remainingTime += waterTime;
			CheckResult hit;
			MoveBy(pawn, end - pawn->Location(), hit);
		}
		if (!pawn->bBounce() && !pawn->bJustTeleported())
		{
			pawn->Velocity() = (pawn->Location() - pawn->OldLocation()) * (1.0f / (timeTick - waterTime));
			pawn->Velocity() = pawn->Velocity() * 2.0f - oldVelocity;
			if (dot(pawn->Velocity(), pawn->Velocity()) > 16000000.0f)
				pawn->Velocity() = SafeNormal(pawn->Velocity()) * 4000.0f;
		}
		if (pawn->Velocity().z > -160.0f && pawn->Velocity().z < 0.0f)
		{
			float speed2D = std::sqrt(pawn->Velocity().x * pawn->Velocity().x + pawn->Velocity().y * pawn->Velocity().y);
			pawn->Velocity().z = -80.0f - speed2D * 0.69999999f;
		}
		if (remainingTime > 0.0099999998f)
			PhysSwimming(pawn, remainingTime, iterations);
	}

	// Velocity + acceleration (water friction), capped at MaxSpeed, moved with the whole frame time; a hit raises
	// HitWall (with the level actor for the world); a bouncing projectile goes on with the time left (twice at most).
	// Outside the world it is destroyed.
	// IDA Engine.dll: ?physProjectile@AActor@@QAEXMH@Z [HP1 0x103F2AB0]
	static void PhysProjectile(UActor* actor, float deltaTime, int iterations)
	{
		if (actor->Region().ZoneNumber == 0)
		{
			actor->Destroy();
			return;
		}
		actor->OldLocation() = actor->Location();
		actor->bJustTeleported() = false;
		float remaining = deltaTime;
		int bounces = 0;
		while (remaining > 0.0f && iterations < 8)
		{
			iterations++;
			UZoneInfo* zone = actor->Region().Zone;
			if (zone->bWaterZone())
				actor->Velocity() *= 1.0f - remaining * zone->ZoneFluidFriction() * 0.2f;
			actor->Velocity() += actor->Acceleration() * remaining;
			float step = remaining;
			remaining = 0.0f;
			if (UProjectile* projectile = UObject::TryCast<UProjectile>(actor))
			{
				float maxSpeed = projectile->MaxSpeed();
				if (dot(actor->Velocity(), actor->Velocity()) > maxSpeed * maxSpeed)
					actor->Velocity() = SafeNormal(actor->Velocity()) * maxSpeed;
			}
			CheckResult hit;
			MoveBy(actor, actor->Velocity() * deltaTime, hit);
			if (hit.Time < 1.0f && !actor->bDeleteMe() && !actor->bJustTeleported())
			{
				CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor) });
				if (actor->bDeleteMe())
					return;
				if (actor->bBounce())
				{
					if (bounces < 2)
						remaining = (1.0f - hit.Time) * step;
					bounces++;
					if (actor->Physics() == PHYS_Falling)
						PhysFalling(actor, remaining, iterations);
				}
			}
		}
		if (!actor->bDeleteMe() && !actor->bBounce() && !actor->bJustTeleported())
			actor->Velocity() = (actor->Location() - actor->OldLocation()) * (1.0f / deltaTime);
	}

	// Like walking without the AI: friction turns the velocity towards the acceleration, steps of at most 0.1 s, a
	// probe 16 down for the floor (a slope slides it down); losing the floor: Falling, physFalling.
	// IDA Engine.dll: ?physRolling@AActor@@QAEXMH@Z [HP1 0x103F3040]
	static void PhysRolling(UActor* actor, float deltaTime, int iterations)
	{
		UZoneInfo* zone = actor->Region().Zone;
		vec3& velocity = actor->Velocity();
		float speed = length(velocity);
		velocity -= (SafeNormal(velocity) - SafeNormal(actor->Acceleration())) * speed * deltaTime * zone->ZoneGroundFriction();
		velocity = velocity * (1.0f - deltaTime * zone->ZoneFluidFriction()) + actor->Acceleration() * deltaTime;
		vec3 desiredMove = velocity + zone->ZoneVelocity();
		actor->OldLocation() = actor->Location();
		actor->bJustTeleported() = false;
		vec3 down = GravDir(actor) * 16.0f;
		float remaining = deltaTime;
		int bounces = 0;
		CheckResult hit;
		while (remaining > 0.0f && iterations < 8)
		{
			iterations++;
			float timeTick = remaining <= 0.1f ? remaining : std::min(remaining * 0.5f, 0.1f);
			remaining -= timeTick;
			vec3 delta = desiredMove * timeTick;
			vec3 stepStart = actor->Location();
			if (std::abs(delta.x) >= 0.000099999997f || std::abs(delta.y) >= 0.000099999997f || std::abs(delta.z) >= 0.000099999997f)
			{
				MoveBy(actor, delta, hit);
				if (actor->bDeleteMe())
					return;
				if (hit.Time < 1.0f)
				{
					CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor) });
					if (actor->bDeleteMe())
						return;
					if (actor->bBounce())
					{
						if (bounces < 2)
							remaining += (1.0f - hit.Time) * timeTick;
						bounces++;
					}
					else
					{
						vec3 oldDelta = delta;
						vec3 oldHitNormal = hit.Normal;
						vec3 slide = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Time);
						if (dot(slide, oldDelta) >= 0.0f)
						{
							MoveBy(actor, slide, hit);
							if (hit.Time < 1.0f && !actor->bDeleteMe())
							{
								CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor) });
								if (actor->bDeleteMe())
									return;
								TwoWallAdjust(SafeNormal(desiredMove), slide, hit.Normal, oldHitNormal, hit.Time);
								MoveBy(actor, slide, hit);
							}
						}
					}
				}
			}
			if (actor->bDeleteMe())
				return;

			MoveBy(actor, down, hit);
			float floorTime = hit.Time;
			float floorZ = hit.Normal.z;
			if (hit.Time < 1.0f && hit.Normal.z < 1.0f && hit.Normal.z * zone->ZoneGroundFriction() < 3.3f)
			{
				float friction = std::max(zone->ZoneGroundFriction(), 0.5f);
				vec3 slide = zone->ZoneGravity() * deltaTime * (1.0f / (friction * 2.0f)) * deltaTime;
				vec3 slideDelta = slide - hit.Normal * dot(slide, hit.Normal);
				if (dot(slideDelta, slide) >= 0.0f)
				{
					MoveBy(actor, slideDelta, hit);
					if (floorZ < hit.Normal.z)
						floorZ = hit.Normal.z;
				}
			}
			if (floorTime == 1.0f || floorZ < 0.69999999f)
			{
				MoveBy(actor, -down * floorTime, hit);
				float len = length(delta);
				vec3 moved = actor->Location() - stepStart;
				float moved2D = std::sqrt(moved.x * moved.x + moved.y * moved.y);
				remaining += (1.0f - std::min(moved2D / len, 1.0f)) * timeTick;
				CallEvent(actor, EventName::Falling);
				if (actor->bDeleteMe())
					return;
				if (actor->Physics() == PHYS_Rolling)
					SetPhysics(actor, PHYS_Falling, nullptr);
				if (actor->Physics() == PHYS_Falling)
				{
					if (!actor->bJustTeleported() && deltaTime > remaining)
						actor->Velocity() = (actor->Location() - actor->OldLocation()) * (1.0f / (deltaTime - remaining));
					actor->Velocity().z = 0.0f;
					if (remaining > 0.0049999999f)
						PhysFalling(actor, remaining, iterations);
					return;
				}
				MoveBy(actor, desiredMove * remaining, hit);
			}
			else if (hit.Actor != actor->ActorBase())
			{
				actor->SetBase(hit.Actor, true);
			}
		}
		if (!actor->bDeleteMe() && !actor->bJustTeleported())
			actor->Velocity() = (actor->Location() - actor->OldLocation()) * (1.0f / deltaTime);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// performPhysics

	// IDA Engine.dll: ?performPhysics@AActor@@UAEXM@Z [HP1 0x103E52C0]
	// IDA Engine.dll: ?performPhysics@APawn@@UAEXM@Z [HP1 0x103E5520]
	void PerformPhysics(UActor* actor, float deltaTime)
	{
		if (IsInterpolationManager(actor))
		{
			if (actor->Physics() == PHYS_Interpolating)
				InterpolationManagerPhysics(actor, deltaTime);
			return;
		}

		UPawn* pawn = AsPawn(actor);
		vec3 oldVelocity = actor->Velocity();
		switch (actor->Physics())
		{
		case PHYS_Walking: if (pawn) PhysWalking(pawn, deltaTime, 0); break;
		case PHYS_Falling: PhysFalling(actor, deltaTime, 0); break;
		case PHYS_Swimming: if (pawn) PhysSwimming(pawn, deltaTime, 0); break;
		case PHYS_Flying: if (pawn) PhysFlying(pawn, deltaTime, 0); break;
		case PHYS_Projectile: PhysProjectile(actor, deltaTime, 0); break;
		case PHYS_Rolling: PhysRolling(actor, deltaTime, 0); break;
		case PHYS_MovingBrush:
			if (!pawn)
			{
				actor->OldLocation() = actor->Location();
				PhysMovingBrush(actor, deltaTime);
				actor->Velocity() = (actor->Location() - actor->OldLocation()) * (1.0f / deltaTime);
			}
			break;
		case PHYS_Spider: if (pawn) actor->TickSpider(deltaTime); break;
		case PHYS_Trailer: actor->TickTrailer(deltaTime); break;
		default: break;
		}
		if (actor->bDeleteMe())
			return;

		if (pawn)
		{
			if (pawn->Physics() != PHYS_Spider && (!IsPlayerPawn(pawn) || pawn->Rotation() != pawn->DesiredRotation() || pawn->RotationRate().Roll > 0))
				PawnPhysicsRotation(pawn, deltaTime, oldVelocity);
			pawn->MoveTimer() -= deltaTime;
			pawn->AvgPhysicsTime() = pawn->AvgPhysicsTime() * 0.80000001f + deltaTime * 0.2f;
		}
		else if ((actor->RotationRate().Pitch & 0xFFFF) != 0 || (actor->RotationRate().Yaw & 0xFFFF) != 0 || (actor->RotationRate().Roll & 0xFFFF) != 0)
		{
			ActorPhysicsRotation(actor, deltaTime);
		}
		if (actor->bDeleteMe())
			return;

		if (UActor* pending = actor->PendingTouch())
		{
			CallEvent(pending, EventName::PostTouch, { ExpressionValue::ObjectValue(actor) });
			if (UActor* p = actor->PendingTouch())
			{
				actor->PendingTouch() = p->PendingTouch();
				p->PendingTouch() = nullptr;
			}
		}
	}
}

namespace KW
{
	bool UseKWPhysics()
	{
		static bool use = []() { const char* s = getenv("FLIPENDO_SE_PHYSICS"); return !(s && *s && *s != '0'); }();
		return use;
	}

	// AActor::Tick calls performPhysics when the actor has physics and isn't an autonomous proxy.
	// IDA Engine.dll: ?Tick@AActor@@UAEHMW4ELevelTick@@@Z [HP1 0x103B3840] (the end: performPhysics after Timer and LifeSpan)
	void PhysicsTick(UActor* actor, float elapsed)
	{
		if (actor->Physics() != PHYS_None && actor->Role() != ROLE_AutonomousProxy && elapsed > 0.0f)
			PerformPhysics(actor, elapsed);
	}
}

namespace KW
{
	// IDA Engine.dll: ?SpawnActor@ULevel@@UAEPAVAActor@@PAVUClass@@VFName@@PAV2@PAVAPawn@@VFVector@@VFRotator@@2HH@Z [HP1 0x103A65A0] (the FindSpot for bCollideWorld/bCollideWhenPlacing templates)
	bool SpawnFindSpot(float radius, float height, vec3& location)
	{
		return FindSpot(vec3(radius, radius, height), location, false, true);
	}
}
