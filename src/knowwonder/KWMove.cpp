#include "Precomp.h"
#include "KWMove.h"
#include "KW.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/UBrush.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Math/coords.h"
#include <cmath>

// HP1's ULevel::MoveActor and friends: how an actor moves through the world (sweep, block, bump, touch, carry what
// stands on it, encroach), teleports (FarMoveActor, FindSpot) and changes zone. Built on the checks of KWCheck.h.
// docs/re/engine/physics.md "Moving actors"

namespace KW
{
	static UActor* LevelInfo()
	{
		return engine->LevelInfo;
	}

	static bool IsMover(UActor* actor)
	{
		return actor->Brush() && UObject::TryCast<UBrush>(actor) && !actor->bStatic();
	}

	static bool IsTiny(const vec3& v)
	{
		return std::abs(v.x) < 0.000099999997f && std::abs(v.y) < 0.000099999997f && std::abs(v.z) < 0.000099999997f;
	}

	static vec3 SafeNormal(const vec3& v)
	{
		float len2 = dot(v, v);
		return len2 == 0.0f ? vec3(0.0f) : v * (1.0f / std::sqrt(len2));
	}

	// Both brushes never overlap anything; otherwise the plain cylinders (no CollisionWidth offset), strictly.
	// IDA Engine.dll: ?IsOverlapping@AActor@@QBEHPBV1@@Z [HP1 0x10379D90]
	bool IsOverlapping(UActor* actor, UActor* other)
	{
		if (actor->Brush() && UObject::TryCast<UBrush>(actor))
			return false;
		if (other->Brush() && UObject::TryCast<UBrush>(other))
			return false;
		if (other == LevelInfo())
			return false;
		vec3 d = actor->Location() - other->Location();
		float r = other->CollisionRadius() + actor->CollisionRadius();
		if (!(r * r > d.x * d.x + d.y * d.y))
			return false;
		float h = other->CollisionHeight() + actor->CollisionHeight();
		return h * h > d.z * d.z;
	}

	// actor stands on other, directly or through what it stands on. (SurrealEngine's UActor::IsBasedOn asks the
	// opposite: a->IsBasedOn(b) is "b stands on a".)
	// IDA Engine.dll: ?IsBasedOn@AActor@@QBEHPBV1@@Z [HP1 0x10352430]
	bool IsBasedOn(UActor* actor, UActor* other)
	{
		for (UActor* a = actor; a; a = a->ActorBase())
		{
			if (a == other)
				return true;
		}
		return false;
	}

	static void Bump(UActor* actor, UActor* other)
	{
		CallEvent(actor, EventName::Bump, { ExpressionValue::ObjectValue(other) });
	}

	// The region an actor is in; a pawn also gets its foot (Location - CollisionHeight) and head (Location + EyeHeight)
	// regions. ZoneChange is raised before Region changes (the script still sees the old zone), then ActorEntered.
	// IDA Engine.dll: ?SetActorZone@ULevel@@UAEXPAVAActor@@HH@Z [HP1 0x103ACDD0]
	void SetActorZone(UActor* actor, bool test, bool forceRefresh)
	{
		if (actor->bDeleteMe())
			return;
		UModel* model = engine->Level->Model;
		ULevelInfo* level = engine->LevelInfo;
		if (actor == level)
		{
			actor->Region() = PointRegion{ level, -1, 0 };
			return;
		}
		UPawn* pawn = UObject::TryCast<UPawn>(actor);
		if (forceRefresh)
		{
			actor->Region() = PointRegion{ level, -1, 0 };
			if (pawn)
			{
				pawn->HeadRegion() = PointRegion{ level, -1, 0 };
				pawn->FootRegion() = PointRegion{ level, -1, 0 };
			}
		}

		PointRegion region = model->FindRegion(actor->Location(), level);
		UZoneInfo* oldZone = actor->Region().Zone;
		if (region.Zone != oldZone && !test)
		{
			if (oldZone)
				CallEvent(oldZone, EventName::ActorLeaving, { ExpressionValue::ObjectValue(actor) });
			CallEvent(actor, EventName::ZoneChange, { ExpressionValue::ObjectValue(region.Zone) });
		}
		bool entered = region.Zone != oldZone;
		actor->Region() = region;
		if (entered && !test && region.Zone)
			CallEvent(region.Zone, EventName::ActorEntered, { ExpressionValue::ObjectValue(actor) });

		if (pawn && !pawn->bDeleteMe())
		{
			vec3 loc = pawn->Location();
			PointRegion foot = model->FindRegion(vec3(loc.x, loc.y, loc.z - pawn->CollisionHeight()), level);
			if (foot.Zone != pawn->FootRegion().Zone && !test)
				CallEvent(pawn, EventName::FootZoneChange, { ExpressionValue::ObjectValue(foot.Zone) });
			pawn->FootRegion() = foot;
			PointRegion head = model->FindRegion(loc + vec3(0.0f, 0.0f, pawn->EyeHeight()), level);
			if (head.Zone != pawn->HeadRegion().Zone && !test)
				CallEvent(pawn, EventName::HeadZoneChange, { ExpressionValue::ObjectValue(head.Zone) });
			pawn->HeadRegion() = head;
			if (level->NetMode() != 3 && pawn->PlayerReplicationInfo())
				pawn->PlayerReplicationInfo()->PlayerZone() = pawn->Region().Zone;
		}
	}

	// IDA Engine.dll: ?MoveActor@ULevel@@UAEHPAVAActor@@VFVector@@VFRotator@@AAUFCheckResult@@HHHH@Z [HP1 0x103AA3A0]
	bool MoveActor(UActor* actor, const vec3& delta, const Rotator& newRotation, CheckResult& hit, bool test, bool ignorePawns, bool ignoreBases, bool noFail)
	{
		if (actor->bStatic() || !actor->bMovable())
			return false;

		if (IsTiny(delta))
		{
			if (newRotation == actor->Rotation())
				return true;
			if (actor->StandingCount() == 0 && !IsMover(actor))
			{
				actor->Rotation() = newRotation;
				return true;
			}
		}

		hit = CheckResult();
		hit.Time = 1.0f;

		float len = 0.0f;
		vec3 dir = delta;
		if (!IsTiny(delta))
		{
			len = std::sqrt(dot(delta, delta));
			dir = delta * (1.0f / len);
		}
		// Everything but a mover probes 2 units further than it moves
		float testAdjust = IsMover(actor) ? 0.0f : 2.0f;
		vec3 testDelta = delta + dir * testAdjust;

		Array<CheckResult> hits;
		bool examined = false;
		if ((actor->bCollideActors() || actor->bCollideWorld()) && (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f))
		{
			BBox box = ActorWorldCollisionBox(actor);
			vec3 center = box.center();
			vec3 extent = box.extents();
			if (IsMover(actor))
				extent -= vec3(0.50999999f);
			hits = MultiLineCheck(center + testDelta, center, extent, actor->bCollideActors(), actor->bCollideWorld() ? LevelInfo() : nullptr, 0);

			if (actor->bBlockActors() || actor->bBlockPlayers() || actor->bCollideWorld())
			{
				for (const CheckResult& h : hits)
				{
					UActor* other = h.Actor;
					if (ignorePawns && !other->bStatic() && (UObject::TryCast<UPawn>(other) || UObject::TryCast<UDecoration>(other)))
						continue;
					if ((ignoreBases && IsBasedOn(actor, other)) || IsBasedOn(other, actor))
						continue;
					examined = true;
					if (IsBlockedBy(actor, other))
					{
						hit = h;
						break;
					}
				}
			}
		}

		vec3 moveDelta = delta;
		if (hit.Time < 1.0f && !noFail)
		{
			hit.Time = ((testAdjust + len) * hit.Time - testAdjust) / len;
			if (hit.Time < 0.0f)
			{
				hit.Time = 0.0f;
				return false;
			}
			moveDelta = delta * hit.Time;
		}

		// Carry what stands on it, turning it with the yaw change
		if (actor->StandingCount() != 0 && !test)
		{
			int yawDelta = newRotation.Yaw - actor->Rotation().Yaw;
			Array<UActor*> based;
			for (UActor* other : engine->Level->Actors)
			{
				if (other && other->ActorBase() == actor)
					based.push_back(other);
			}
			for (UActor* other : based)
			{
				if (other->bDeleteMe())
					continue;
				vec3 rotOffset(0.0f);
				if (newRotation != actor->Rotation())
				{
					int turn = (newRotation.Yaw & 0xFFFC) - (actor->Rotation().Yaw & 0xFFFC);
					vec3 arm = actor->Location() - other->Location();
					Coords c = Coords::Rotation(Rotator(0, turn, 0));
					vec3 turned(dot(arm, c.XAxis), dot(arm, c.YAxis), dot(arm, c.ZAxis));
					rotOffset = arm - turned;
				}
				CheckResult otherHit;
				Rotator otherRotation(other->Rotation().Pitch, other->Rotation().Yaw + yawDelta, other->Rotation().Roll);
				MoveActor(other, moveDelta + rotOffset, otherRotation, otherHit, false, false, true, false);
				if (UPawn* pawn = UObject::TryCast<UPawn>(other))
					pawn->ViewRotation().Yaw += yawDelta;
			}
		}

		if (actor->bCollideActors())
			actor->XLevel()->Collision.RemoveFromCollision(actor);

		if (!test && !noFail && !UObject::TryCast<UPawn>(actor) && CheckEncroachment(actor, actor->Location() + moveDelta, newRotation, false))
		{
			if (actor->bCollideActors())
				actor->XLevel()->Collision.AddToCollision(actor);
			return false;
		}

		actor->Location() += moveDelta;
		actor->Rotation() = newRotation;
		if (actor->bCollideActors())
			actor->XLevel()->Collision.AddToCollision(actor);

		if (!test)
		{
			if (hit.Actor && hit.Actor != LevelInfo() && !IsBasedOn(actor, hit.Actor))
			{
				UActor* other = hit.Actor;
				Bump(other, actor);
				if (!actor->bDeleteMe() && !other->bDeleteMe())
					Bump(actor, other);
			}

			if (examined || !actor->bBlockActors() || !actor->bBlockPlayers())
			{
				for (const CheckResult& h : hits)
				{
					if (!(h.Time < hit.Time) || actor->bDeleteMe())
						break;
					UActor* other = h.Actor;
					if (other->bDeleteMe() || other == LevelInfo())
						continue;
					if (!IsBasedOn(other, actor) && (!ignoreBases || !IsBasedOn(actor, other)) && !IsBlockedBy(actor, other))
						actor->Touch(other);
				}
			}

			for (int i = 0; i < UActor::TouchingArraySize && !actor->bDeleteMe(); i++)
			{
				UActor* other = actor->Touching()[i];
				if (other && !IsOverlapping(actor, other))
					actor->UnTouch(other);
			}
		}

		SetActorZone(actor, test, false);
		return hit.Time > 0.0f;
	}

	// Other actors in the way of an actor (a mover, or anything that collides) moving to location/rotation. A mover
	// first pushes each one along (moveSmooth); only what it couldn't push away gets EncroachingOn, and if that returns
	// true the move is refused. Then EncroachedBy for actors it is blocked by, touch for the rest (touchNotify).
	// IDA Engine.dll: ?CheckEncroachment@ULevel@@UAEHPAVAActor@@VFVector@@VFRotator@@H@Z [HP1 0x103AB5F0]
	bool CheckEncroachment(UActor* actor, const vec3& location, const Rotator& rotation, bool touchNotify)
	{
		if (!actor->bCollideActors() && !actor->bBlockActors() && !actor->bBlockPlayers() && !IsMover(actor))
			return false;

		Array<CheckResult> results = ActorEncroachmentCheck(actor, location, rotation);
		for (const CheckResult& r : results)
		{
			UActor* other = r.Actor;
			if (other == actor || other == LevelInfo() || other->bDeleteMe())
				continue;
			if (!IsBlockedBy(actor, other))
				continue;

			bool otherIsMover = IsMover(other);
			if (IsMover(actor) && !otherIsMover)
			{
				// Push it along; still in the way afterwards: put it back and ask
				vec3 push = location - actor->Location();
				MoveSmooth(other, push);
				bool stillThere = false;
				for (const CheckResult& again : ActorEncroachmentCheck(actor, location, rotation))
				{
					if (again.Actor == other)
					{
						stillThere = true;
						break;
					}
				}
				if (!stillThere)
					continue;
				vec3 oldLocation = actor->Location();
				actor->Location() = location;
				MoveSmooth(other, -push);
				actor->Location() = oldLocation;
			}
			if (CallEvent(actor, EventName::EncroachingOn, { ExpressionValue::ObjectValue(other) }).ToBool())
				return true;
		}

		if (touchNotify)
		{
			for (int i = 0; i < UActor::TouchingArraySize; i++)
			{
				UActor* other = actor->Touching()[i];
				if (other && !IsOverlapping(actor, other))
					actor->UnTouch(other);
			}
		}

		for (const CheckResult& r : results)
		{
			UActor* other = r.Actor;
			if (other == actor || other == LevelInfo() || other->bDeleteMe() || actor->bDeleteMe())
				continue;
			if (IsBlockedBy(actor, other))
				CallEvent(other, EventName::EncroachedBy, { ExpressionValue::ObjectValue(actor) });
			else if (touchNotify)
				actor->Touch(other);
		}
		return false;
	}

	// Push location out of the world along test - location: a ray to test, and if it hits, back along the normal.
	// IDA Engine.dll: ?AdjustSpot@ULevel@@UAEXAAVFVector@@V2@MAAUFCheckResult@@@Z [HP1 0x103A9570]
	static void AdjustSpot(vec3& location, const vec3& test, float extent)
	{
		CheckResult hit;
		SingleLineCheck(hit, nullptr, test, location, TRACE_Movers | TRACE_Level, vec3(0.0f));
		if (hit.Time < 1.0f)
			location += hit.Normal * ((1.05f - hit.Time) * extent);
	}

	// A free spot for a box of extent at or near location (moved by at most sqrt(1.5) times the extent). With checkFirst
	// a location that is already free is kept (SpawnActor); without it the push-out steps always run (FarMoveActor).
	// IDA Engine.dll: ?FindSpot@ULevel@@UAEHVFVector@@AAV2@HH@Z [HP1 0x103A9690]
	bool FindSpot(const vec3& extent, vec3& location, bool checkActors, bool checkFirst)
	{
		CheckResult hit;
		if (extent.x == 0.0f && extent.y == 0.0f && extent.z == 0.0f)
			return SinglePointCheck(hit, location, extent, 0, LevelInfo(), checkActors);

		if (checkFirst && SinglePointCheck(hit, location, extent, 0, LevelInfo(), checkActors))
			return true;

		vec3 start = location;
		float diag = std::sqrt(dot(extent, extent)) + 2.0f;
		for (int i = -1; i < 2; i += 2)
		{
			AdjustSpot(start, vec3(start.x + i * extent.x, start.y, start.z), extent.x);
			AdjustSpot(start, vec3(start.x, start.y + i * extent.y, start.z), extent.y);
			AdjustSpot(start, vec3(start.x, start.y, start.z + i * extent.z), extent.z);
		}
		if (SinglePointCheck(hit, start, extent, 0, LevelInfo(), checkActors))
		{
			location = start;
			return true;
		}

		for (int i = -1; i < 2; i += 2)
		{
			for (int j = -1; j < 2; j += 2)
			{
				for (int k = -1; k < 2; k += 2)
					AdjustSpot(start, vec3(start.x + i * extent.x, start.y + j * extent.y, start.z + k * extent.z), diag);
			}
		}
		vec3 moved = start - location;
		if (dot(extent, extent) * 1.5f < dot(moved, moved))
			return false;
		if (!SinglePointCheck(hit, start, extent, 0, LevelInfo(), checkActors))
			return false;
		location = start;
		return true;
	}

	// Teleport: FindSpot for actors that collide with the world (or bCollideWhenPlacing), then encroachment (with touch).
	// Unbases what stood on it and sets bJustTeleported.
	// IDA Engine.dll: ?FarMoveActor@ULevel@@UAEHPAVAActor@@VFVector@@HH@Z [HP1 0x103A9DE0]
	bool FarMoveActor(UActor* actor, const vec3& destIn, bool test, bool noCheck)
	{
		if (actor->bStatic() || !actor->bMovable())
			return false;
		if (actor->bCollideActors())
			actor->XLevel()->Collision.RemoveFromCollision(actor);

		vec3 dest = destIn;
		bool ok = true;
		bool isClient = engine->LevelInfo->NetMode() == 3;
		if (!noCheck && (actor->bCollideWorld() || (actor->bCollideWhenPlacing() && !isClient)))
			ok = FindSpot(vec3(actor->CollisionRadius(), actor->CollisionRadius(), actor->CollisionHeight()), dest, false, false);

		if (ok)
		{
			if (!test && !noCheck)
				ok = !CheckEncroachment(actor, dest, actor->Rotation(), true);
			if (ok)
			{
				if (!test && !isClient)
				{
					if (actor->StandingCount() != 0)
					{
						for (UActor* other : engine->Level->Actors)
						{
							if (other && other->ActorBase() == actor)
								other->SetBase(nullptr, true);
						}
					}
					actor->bJustTeleported() = true;
				}
				actor->Location() = dest;
				actor->OldLocation() = dest;
			}
		}

		if (actor->bCollideActors())
			actor->XLevel()->Collision.AddToCollision(actor);
		if (ok)
			SetActorZone(actor, test, false);
		return ok;
	}

	// What the actor stands on: a trace 8 units down with its cylinder.
	// IDA Engine.dll: ?FindBase@AActor@@QAEXXZ [HP1 0x103E4FD0]
	void FindBase(UActor* actor)
	{
		CheckResult hit;
		vec3 loc = actor->Location();
		SingleLineCheck(hit, actor, vec3(loc.x, loc.y, loc.z - 8.0f), loc, TRACE_Blocking | TRACE_Movers | TRACE_Level, vec3(actor->CollisionRadius(), actor->CollisionRadius(), actor->CollisionHeight()));
		if (actor->ActorBase() != hit.Actor)
			actor->SetBase(hit.Actor, true);
	}

	// The remaining move along two walls.
	// IDA Engine.dll: ?TwoWallAdjust@AActor@@QAEXAAVFVector@@0000M@Z [HP1 0x1031C3E0]
	void TwoWallAdjust(const vec3& desiredDir, vec3& delta, const vec3& hitNormal, const vec3& oldHitNormal, float hitTime)
	{
		if (dot(oldHitNormal, hitNormal) > 0.0f)
		{
			delta = (delta - hitNormal * dot(delta, hitNormal)) * (1.0f - hitTime);
			if (dot(desiredDir, delta) <= 0.0f)
				delta = vec3(0.0f);
		}
		else
		{
			vec3 along = SafeNormal(cross(hitNormal, oldHitNormal));
			delta = along * (dot(along, delta) * (1.0f - hitTime));
			if (dot(desiredDir, delta) < 0.0f)
				delta = -delta;
		}
	}

	// Move, and on a hit slide along the wall (HitWall first), then along a second wall.
	// IDA Engine.dll: ?moveSmooth@AActor@@QAEHVFVector@@@Z [HP1 0x103E4C30]
	bool MoveSmooth(UActor* actor, const vec3& delta)
	{
		CheckResult hit;
		bool result = MoveActor(actor, delta, actor->Rotation(), hit);
		if (hit.Time < 1.0f && !actor->bDeleteMe())
		{
			vec3 adjusted = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Time);
			if (dot(adjusted, delta) >= 0.0f)
			{
				vec3 oldNormal = hit.Normal;
				vec3 desiredDir = SafeNormal(delta);
				CallEvent(actor, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor) });
				if (actor->bDeleteMe())
					return result;
				MoveActor(actor, adjusted, actor->Rotation(), hit);
				if (hit.Time < 1.0f && !actor->bDeleteMe())
				{
					TwoWallAdjust(desiredDir, adjusted, hit.Normal, oldNormal, hit.Time);
					MoveActor(actor, adjusted, actor->Rotation(), hit);
				}
			}
		}
		return result;
	}
}
