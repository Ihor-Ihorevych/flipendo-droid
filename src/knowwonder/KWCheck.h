#pragma once

// KnowWonder's collision checks (HP1's Engine.dll), the layer the movement code (MoveActor, physics) stands on.
// docs/re/engine/collision.md

#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/rotator.h"

#include "Math/bbox.h"
#include "Utils/Array.h"

class UActor;
class UModel;

namespace KW
{
	// FCheckResult: what a check hit. Time is the fraction of a line check (0 for a point check), Item the BSP node
	// (for a hull hit: the hull plane's node, so the surface can be looked up), -1 for a box plane or bevel.
	struct CheckResult
	{
		UActor* Actor = nullptr;
		vec3 Location = vec3(0.0f);
		vec3 Normal = vec3(0.0f);
		UModel* Model = nullptr; // the primitive when it is a BSP model (level or mover brush)
		float Time = 1.0f;
		int Item = -1;
	};

	// AActor::GetPrimitive: which UPrimitive checks an actor (Actor.CollideType; a brush actor is always its Brush)
	enum class PrimitiveKind { Cylinder, OrientedCylinder, Box, Mesh, Brush };
	PrimitiveKind ActorPrimitive(UActor* actor);
	BBox ActorWorldCollisionBox(UActor* actor);

	// A BSP model and where it is: the level (owner null) or a mover's brush (planes taken to world space).
	struct ModelFrame
	{
		UModel* Model = nullptr;
		UActor* Owner = nullptr;
		bool Transformed = false;
		mat4 LocalToWorld;
		mat4 WorldToLocal;

		static ModelFrame Level(UModel* model);
		static ModelFrame Brush(UActor* owner);

		vec4 Plane(int node) const;      // a node's plane in world space
		vec4 WorldPlane(const vec4& localPlane) const;
		vec3 WorldNormal(const vec3& localNormal) const;
	};

	// UModel::LineCheck: returns true when nothing was hit (UE1's convention), else fills hit.
	bool ModelLineCheck(CheckResult& hit, const ModelFrame& frame, const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags);
	// UModel::PointCheck: returns true when the box (extent zero: the point) touches nothing.
	bool ModelPointCheck(CheckResult& hit, const ModelFrame& frame, const vec3& location, const vec3& extent, uint8_t extraNodeFlags);
	// UModel::FastLineCheck: true when the line is clear.
	bool ModelFastLineCheck(UModel* model, const vec3& end, const vec3& start);

	// UPrimitive::LineCheck / PointCheck for an actor's own primitive (cylinder, oriented cylinder, box, mesh: its
	// cylinder, brush: its BSP). True when nothing is hit.
	bool PrimitiveLineCheck(CheckResult& hit, UActor* actor, const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags);
	bool PrimitivePointCheck(CheckResult& hit, UActor* actor, const vec3& location, const vec3& extent, uint8_t extraNodeFlags);
	bool BoxLineCheck(CheckResult& hit, UActor* actor, const vec3& end, const vec3& start, const vec3& extent);
	bool BoxPointCheck(CheckResult& hit, UActor* actor, const vec3& location, const vec3& extent);

	// ULevel::SingleLineCheck's trace flags
	enum TraceFlag : uint32_t
	{
		TRACE_Blocking = 1,     // actors that block the tracing actor (AActor::IsBlockedBy)
		TRACE_Movers = 2,
		TRACE_Level = 4,
		TRACE_ZoneChanges = 8,
		TRACE_Others = 16,      // actors that don't block it
		TRACE_AllColliding = TRACE_Blocking | TRACE_Movers | TRACE_Level | TRACE_ZoneChanges | TRACE_Others
	};

	// FCollisionHash checks: every colliding actor whose primitive the line / box hits. Unsorted.
	Array<CheckResult> ActorLineCheck(const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags);
	Array<CheckResult> ActorPointCheck(const vec3& location, const vec3& extent, uint8_t extraNodeFlags);
	Array<CheckResult> ActorRadiusCheck(const vec3& location, float radius);
	// Actors that the actor would overlap at location/rotation (a brush tests their cylinders against its BSP).
	Array<CheckResult> ActorEncroachmentCheck(UActor* actor, const vec3& location, const Rotator& rotation);

	// ULevel checks. The level is tested when levelInfo is given (TRACE_Level), actors when traceActors.
	Array<CheckResult> MultiLineCheck(const vec3& end, const vec3& start, const vec3& extent, bool traceActors, UActor* levelInfo, uint8_t extraNodeFlags);
	// The first hit the flags accept, ignoring the source actor and its owners. True when nothing was hit.
	bool SingleLineCheck(CheckResult& hit, UActor* source, const vec3& end, const vec3& start, uint32_t traceFlags, const vec3& extent, uint8_t extraNodeFlags = 0);
	Array<CheckResult> MultiPointCheck(const vec3& location, const vec3& extent, uint8_t extraNodeFlags, UActor* levelInfo, bool actors);
	bool SinglePointCheck(CheckResult& hit, const vec3& location, const vec3& extent, uint8_t extraNodeFlags, UActor* levelInfo, bool actors);
}
