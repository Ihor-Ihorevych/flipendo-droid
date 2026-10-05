#include "Precomp.h"
#include "KWCheck.h"
#include "KW.h"
#include "KWActor.h"
#include "Engine.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Collision/TopLevel/CollisionSystem.h"
#include "Math/coords.h"
#include <algorithm>
#include <cmath>

// HP1's actor collision primitives and the level-wide checks built on them (ULevel::SingleLineCheck etc.): what the
// movement code asks "what would I hit". The actor hash is SurrealEngine's bucket grid (CollisionSystem, filled with
// each actor's world collision box like FCollisionHash::AddActor); every candidate is then tested with its own primitive.
// docs/re/engine/collision.md

namespace KW
{
	static vec3 SafeNormal(const vec3& v)
	{
		float len2 = dot(v, v);
		if (len2 == 0.0f)
			return vec3(0.0f);
		return v * (1.0f / std::sqrt(len2));
	}

	// AActor::GetCylinderExtent
	static vec3 CylinderExtent(UActor* actor)
	{
		return vec3(actor->CollisionRadius(), actor->CollisionRadius(), actor->CollisionHeight());
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Cylinder (UPrimitive's own checks): vertical axis, centre raised by CollisionWidth

	// IDA Engine.dll: ?LineCheck@UPrimitive@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@22K@Z [HP1 0x103FA760]
	static bool CylinderLineCheck(CheckResult& hit, UActor* actor, const vec3& end, const vec3& start, const vec3& extent)
	{
		vec3 cyl = CylinderExtent(actor);
		vec3 loc = actor->Location();
		float rx = extent.x + cyl.x;
		float ry = extent.y + cyl.y;
		float hz = extent.z + cyl.z;
		if (start.x >= loc.x + rx && end.x >= loc.x + rx)
			return true;
		if (start.x <= loc.x - rx && end.x <= loc.x - rx)
			return true;
		if (start.y >= loc.y + ry && end.y >= loc.y + ry)
			return true;
		if (start.y <= loc.y - ry && end.y <= loc.y - ry)
			return true;
		float center = CollisionWidth(actor) + loc.z;
		float top = center + hz;
		float bottom = center - hz;
		if (start.z >= top && end.z >= top)
			return true;
		if (start.z <= bottom && end.z <= bottom)
			return true;

		float t0 = 0.0f, t1 = 1.0f;
		if (start.z < top)
		{
			if (end.z > top)
				t1 = (top - start.z) / (end.z - start.z);
		}
		else if (end.z < top)
		{
			t0 = (top - start.z) / (end.z - start.z);
			hit.Normal = vec3(0.0f, 0.0f, 1.0f);
		}
		if (start.z > bottom)
		{
			if (end.z < bottom)
				t1 = (bottom - start.z) / (end.z - start.z);
		}
		else if (end.z > bottom)
		{
			t0 = (bottom - start.z) / (end.z - start.z);
			hit.Normal = vec3(0.0f, 0.0f, -1.0f);
		}

		double relX = start.x - loc.x, relY = start.y - loc.y;
		double dirX = end.x - start.x, dirY = end.y - start.y;
		double a = dirY * dirY + dirX * dirX;
		double b = (dirY * relY + dirX * relX) * 2.0;
		double c = relY * relY + relX * relX - (double)rx * rx;
		double disc = b * b - c * a * 4.0;

		if (c >= 1.0 || start.z <= bottom || start.z >= top)
		{
			if (disc < 0.0)
				return true;
			bool hitSide;
			if (a >= 0.0000000099999991)
			{
				double s = std::sqrt(disc);
				double half = 0.5 / a;
				double exit = (s - b) * half;
				if (t1 > exit)
					t1 = (float)exit;
				double enter = -((s + b) * half);
				if (enter >= t0)
				{
					t0 = (float)enter;
					vec3 n((float)(dirX * enter + start.x - loc.x), (float)(dirY * enter + start.y - loc.y), 0.0f);
					float len2 = dot(n, n);
					hit.Normal = len2 >= 0.0000000099999999f ? n * (1.0f / std::sqrt(len2)) : n;
				}
				hitSide = t0 < t1;
			}
			else
			{
				hitSide = c < 0.0;
			}
			if (!hitSide)
				return true;
			hit.Time = std::clamp(t0 - 0.001f, 0.0f, 1.0f);
			hit.Location = start + (end - start) * hit.Time;
		}
		else
		{
			// Inside the cylinder already: blocked only when moving towards its axis
			if (dirX * relX + dirY * relY >= -0.1)
				return true;
			hit.Time = 0.0f;
			hit.Location = start;
			hit.Normal = SafeNormal(vec3((float)relX, (float)relY, 0.0f));
		}
		hit.Actor = actor;
		hit.Model = nullptr;
		return false;
	}

	// IDA Engine.dll: ?PointCheck@UPrimitive@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@2K@Z [HP1 0x103FA420]
	static bool CylinderPointCheck(CheckResult& hit, UActor* actor, const vec3& location, const vec3& extent)
	{
		vec3 loc = actor->Location();
		float dz = CollisionWidth(actor) + loc.z - location.z;
		float hz = extent.z + actor->CollisionHeight();
		if (!(hz * hz > dz * dz))
			return true;
		float dx = loc.x - location.x, dy = loc.y - location.y;
		float r = extent.x + actor->CollisionRadius();
		if (!(r * r > dx * dx + dy * dy))
			return true;

		hit.Actor = actor;
		hit.Normal = SafeNormal(location - loc);
		if (hit.Normal.z < -0.5f || hit.Normal.z > 0.5f)
		{
			hit.Location = vec3(location.x, location.y, loc.z - extent.z);
		}
		else
		{
			// As in the original: the side location is the cylinder's edge, with the point's Z added to the centre's
			vec3 side = SafeNormal(vec3(hit.Normal.x, hit.Normal.y, 0.0f));
			hit.Location = vec3(loc.x - side.x * extent.x, loc.y - side.y * extent.x, loc.z - side.z * extent.x + location.z);
		}
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Oriented cylinder: the cylinder checks in the actor's rotated frame (around Location)

	struct ActorFrame
	{
		vec3 Location;
		vec3 Axis[3]; // the actor's rotated X, Y, Z in world space

		explicit ActorFrame(UActor* actor)
		{
			Location = actor->Location();
			mat4 rot = Coords::Rotation(actor->Rotation()).ToMatrix();
			for (int i = 0; i < 3; i++)
			{
				vec4 a(i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f, 0.0f);
				Axis[i] = (rot * a).xyz();
			}
		}
		vec3 ToLocal(const vec3& p) const { vec3 v = p - Location; return vec3(dot(v, Axis[0]), dot(v, Axis[1]), dot(v, Axis[2])) + Location; }
		vec3 ToWorld(const vec3& p) const { vec3 v = p - Location; return Axis[0] * v.x + Axis[1] * v.y + Axis[2] * v.z + Location; }
		vec3 ToWorldNormal(const vec3& n) const { return Axis[0] * n.x + Axis[1] * n.y + Axis[2] * n.z; }
		vec3 LocalExtent(const vec3& e) const
		{
			vec3 r;
			for (int i = 0; i < 3; i++)
				r[i] = std::abs(Axis[i].x) * e.x + std::abs(Axis[i].y) * e.y + std::abs(Axis[i].z) * e.z;
			return r;
		}
	};

	// IDA Engine.dll: ?LineCheck@UOrientedCylinder@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@22K@Z [HP1 0x103FBC50]
	static bool OrientedCylinderLineCheck(CheckResult& hit, UActor* actor, const vec3& end, const vec3& start, const vec3& extent)
	{
		ActorFrame frame(actor);
		if (CylinderLineCheck(hit, actor, frame.ToLocal(end), frame.ToLocal(start), frame.LocalExtent(extent)))
			return true;
		hit.Location = frame.ToWorld(hit.Location);
		hit.Normal = frame.ToWorldNormal(hit.Normal);
		return false;
	}

	// IDA Engine.dll: ?PointCheck@UOrientedCylinder@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@2K@Z [HP1 0x103FB7A0]
	static bool OrientedCylinderPointCheck(CheckResult& hit, UActor* actor, const vec3& location, const vec3& extent)
	{
		ActorFrame frame(actor);
		if (CylinderPointCheck(hit, actor, frame.ToLocal(location), frame.LocalExtent(extent)))
			return true;
		hit.Location = frame.ToWorld(hit.Location);
		hit.Normal = frame.ToWorldNormal(hit.Normal);
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// An actor's primitive

	bool PrimitiveLineCheck(CheckResult& hit, UActor* actor, const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags)
	{
		switch (ActorPrimitive(actor))
		{
		case PrimitiveKind::Brush: return ModelLineCheck(hit, ModelFrame::Brush(actor), end, start, extent, extraNodeFlags);
		case PrimitiveKind::Box: return BoxLineCheck(hit, actor, end, start, extent);
		case PrimitiveKind::OrientedCylinder: return OrientedCylinderLineCheck(hit, actor, end, start, extent);
		default: return CylinderLineCheck(hit, actor, end, start, extent); // meshes use UPrimitive's cylinder
		}
	}

	bool PrimitivePointCheck(CheckResult& hit, UActor* actor, const vec3& location, const vec3& extent, uint8_t extraNodeFlags)
	{
		switch (ActorPrimitive(actor))
		{
		case PrimitiveKind::Brush: return ModelPointCheck(hit, ModelFrame::Brush(actor), location, extent, extraNodeFlags);
		case PrimitiveKind::Box: return BoxPointCheck(hit, actor, location, extent);
		case PrimitiveKind::OrientedCylinder: return OrientedCylinderPointCheck(hit, actor, location, extent);
		default: return CylinderPointCheck(hit, actor, location, extent);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// The actor hash

	static CollisionSystem& Hash()
	{
		return engine->Level->Collision;
	}

	static bool InHash(UActor* actor)
	{
		return actor && !actor->bDeleteMe() && actor->bCollideActors() && actor->Collision.Inserted;
	}

	// IDA Engine.dll: ?ActorLineCheck@FCollisionHash@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@11E@Z [HP1 0x10365CC0]
	Array<CheckResult> ActorLineCheck(const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags)
	{
		Array<CheckResult> results;
		vec3 boxMin(std::min(start.x, end.x), std::min(start.y, end.y), std::min(start.z, end.z));
		vec3 boxMax(std::max(start.x, end.x), std::max(start.y, end.y), std::max(start.z, end.z));
		for (UActor* actor : Hash().ActorsInBox(boxMin - extent, boxMax + extent))
		{
			if (!InHash(actor))
				continue;
			CheckResult hit;
			hit.Time = 0.0f;
			if (!PrimitiveLineCheck(hit, actor, end, start, extent, extraNodeFlags))
				results.push_back(hit);
		}
		return results;
	}

	// IDA Engine.dll: ?ActorPointCheck@FCollisionHash@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@1K@Z [HP1 0x103650B0]
	Array<CheckResult> ActorPointCheck(const vec3& location, const vec3& extent, uint8_t extraNodeFlags)
	{
		Array<CheckResult> results;
		for (UActor* actor : Hash().ActorsInBox(location - extent, location + extent))
		{
			if (!InHash(actor))
				continue;
			CheckResult hit;
			hit.Time = 1.0f;
			if (!PrimitivePointCheck(hit, actor, location, extent, 0))
				results.push_back(hit);
		}
		return results;
	}

	// IDA Engine.dll: ?ActorRadiusCheck@FCollisionHash@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@MK@Z [HP1 0x10365500]
	Array<CheckResult> ActorRadiusCheck(const vec3& location, float radius)
	{
		Array<CheckResult> results;
		vec3 r(radius);
		for (UActor* actor : Hash().ActorsInBox(location - r, location + r))
		{
			if (!InHash(actor))
				continue;
			vec3 d = actor->Location() - location;
			if (dot(d, d) < radius * radius)
			{
				CheckResult hit;
				hit.Actor = actor;
				results.push_back(hit);
			}
		}
		return results;
	}

	// The actor is moved to location/rotation for the test and put back. Other movers (non-static brushes) are not
	// tested; every other colliding actor's cylinder (at its Location) is point checked against this actor's primitive.
	// IDA Engine.dll: ?ActorEncroachmentCheck@FCollisionHash@@UAEPAUFCheckResult@@AAVFMemStack@@PAVAActor@@VFVector@@VFRotator@@K@Z [HP1 0x103658B0]
	Array<CheckResult> ActorEncroachmentCheck(UActor* actor, const vec3& location, const Rotator& rotation)
	{
		Array<CheckResult> results;
		vec3 oldLocation = actor->Location();
		Rotator oldRotation = actor->Rotation();
		actor->Location() = location;
		actor->Rotation() = rotation;

		BBox box = ActorWorldCollisionBox(actor);
		for (UActor* other : Hash().ActorsInBox(box.min, box.max))
		{
			if (!InHash(other) || other == actor)
				continue;
			if (other->Brush() && UObject::TryCast<UBrush>(other) && !other->bStatic())
				continue;
			CheckResult hit;
			hit.Time = 1.0f;
			if (!PrimitivePointCheck(hit, actor, other->Location(), CylinderExtent(other), 0))
			{
				hit.Actor = other;
				hit.Model = nullptr;
				results.push_back(hit);
			}
		}

		actor->Location() = oldLocation;
		actor->Rotation() = oldRotation;
		return results;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// ULevel

	// The world first (to the given end); actors only up to 5 units past the world hit (their times rescaled to the
	// whole line). With a world hit closer than 0.01 of the line and 30 units, only movers still count; without
	// traceActors too. A mover hit on the world hit's plane within 2 units of it is put 2 units before it.
	// IDA Engine.dll: ?MultiLineCheck@ULevel@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@11HPAVALevelInfo@@E@Z [HP1 0x103AC620]
	Array<CheckResult> MultiLineCheck(const vec3& endIn, const vec3& start, const vec3& extent, bool traceActors, UActor* levelInfo, uint8_t extraNodeFlags)
	{
		Array<CheckResult> results;
		vec3 end = endIn;
		float scale = 1.0f;
		bool worldHit = false;
		bool worldTooClose = false;
		CheckResult world;
		if (levelInfo)
		{
			world.Time = 1.0f;
			if (!ModelLineCheck(world, ModelFrame::Level(engine->Level->Model), end, start, extent, extraNodeFlags))
			{
				world.Actor = levelInfo;
				worldHit = true;
				results.push_back(world);
				float dist = length(world.Location - start);
				scale = std::min((dist + 5.0f) * world.Time / (dist + 0.000099999997f), 1.0f);
				end = start + (endIn - start) * scale;
				if (world.Time < 0.0099999998f && dist < 30.0f)
					worldTooClose = true;
			}
		}

		if (traceActors || levelInfo)
		{
			for (CheckResult& hit : ActorLineCheck(end, start, extent, extraNodeFlags))
			{
				if (results.size() >= 64)
					break;
				bool isMover = UObject::TryCast<UMover>(hit.Actor) != nullptr;
				if (!traceActors && !isMover)
					continue;
				if (worldTooClose && !isMover)
					continue;
				if (worldHit && isMover && hit.Normal == world.Normal && dot(hit.Location - world.Location, hit.Location - world.Location) < 4.0f)
				{
					vec3 dir = end - start;
					float len = length(dir);
					hit.Location = world.Location - dir * (2.0f / len);
					hit.Time = length(hit.Location - start) / len;
				}
				hit.Time *= scale;
				results.push_back(hit);
			}
		}

		std::stable_sort(results.begin(), results.end(), [](const CheckResult& a, const CheckResult& b) { return a.Time < b.Time; });
		return results;
	}

	static bool IsOwnedBy(UActor* source, UActor* actor)
	{
		for (UActor* a = source; a; a = a->Owner())
		{
			if (a == actor)
				return true;
		}
		return false;
	}

	// IDA Engine.dll: ?SingleLineCheck@ULevel@@UAEHAAUFCheckResult@@PAVAActor@@ABVFVector@@2KV4@E@Z [HP1 0x103AC180]
	bool SingleLineCheck(CheckResult& hit, UActor* source, const vec3& end, const vec3& start, uint32_t traceFlags, const vec3& extent, uint8_t extraNodeFlags)
	{
		UActor* levelInfo = (traceFlags & TRACE_Level) ? engine->LevelInfo : nullptr;
		bool traceActors = (traceFlags & (TRACE_Blocking | TRACE_Movers | TRACE_Level | TRACE_Others)) != 0;
		for (const CheckResult& r : MultiLineCheck(end, start, extent, traceActors, levelInfo, extraNodeFlags))
		{
			UActor* other = r.Actor;
			if (source && IsOwnedBy(source, other))
				continue;
			bool accept;
			if (UObject::TryCast<ULevelInfo>(other))
				accept = (traceFlags & TRACE_Level) != 0;
			else if (UObject::TryCast<UMover>(other))
				accept = (traceFlags & TRACE_Movers) != 0;
			else if (UObject::TryCast<UZoneInfo>(other))
				accept = (traceFlags & TRACE_ZoneChanges) != 0;
			else if (source && IsBlockedBy(source, other))
				accept = (traceFlags & TRACE_Blocking) != 0;
			else
				accept = (traceFlags & TRACE_Others) != 0;
			if (accept)
			{
				hit = r;
				return false;
			}
		}
		hit.Time = 1.0f;
		hit.Actor = nullptr;
		return true;
	}

	// The world's hit (if any) first, then the actors'.
	// IDA Engine.dll: ?MultiPointCheck@ULevel@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@1KPAVALevelInfo@@H@Z [HP1 0x103ABF70]
	Array<CheckResult> MultiPointCheck(const vec3& location, const vec3& extent, uint8_t extraNodeFlags, UActor* levelInfo, bool actors)
	{
		Array<CheckResult> results;
		if (levelInfo)
		{
			CheckResult world;
			world.Time = 1.0f;
			if (!ModelPointCheck(world, ModelFrame::Level(engine->Level->Model), location, extent, 0))
			{
				world.Actor = levelInfo;
				results.push_back(world);
			}
		}
		if (actors)
		{
			for (const CheckResult& r : ActorPointCheck(location, extent, extraNodeFlags))
				results.push_back(r);
		}
		return results;
	}

	// The hit whose (pushed out) location is nearest the point.
	// IDA Engine.dll: ?SinglePointCheck@ULevel@@UAEHAAUFCheckResult@@VFVector@@1KPAVALevelInfo@@H@Z [HP1 0x103ABD70]
	bool SinglePointCheck(CheckResult& hit, const vec3& location, const vec3& extent, uint8_t extraNodeFlags, UActor* levelInfo, bool actors)
	{
		Array<CheckResult> results = MultiPointCheck(location, extent, extraNodeFlags, levelInfo, actors);
		if (results.empty())
			return true;
		hit = results[0];
		for (size_t i = 1; i < results.size(); i++)
		{
			vec3 a = hit.Location - location, b = results[i].Location - location;
			if (dot(a, a) > dot(b, b))
				hit = results[i];
		}
		return false;
	}
}
