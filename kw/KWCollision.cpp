#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "Math/coords.h"
#include "VM/NativeFunc.h"
#include "Packages/Engine/Resources/Mesh/UMesh.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <cmath>

// HP1 actors pick their collision primitive with Actor.CollideType. CT_Box (UBox) is an oriented box centered on
// the actor, with half extents CollisionRadius (X), CollisionWidth (Y, CollisionRadius when 0) and CollisionHeight (Z)
// in the actor's rotated frame. Lev_Tut1 uses it for BlockAll walls along the Grand Hall stairs (250 x 10 x 200),
// BlockPlayers, Triggers and CutScene trigger volumes. As cylinders they block or trigger far too much.
//
// UBox::LineCheck / PointCheck move the check into the box's frame (AActor::ToLocal): start and end are transformed,
// and the checking actor's extent box (CollisionRadius, CollisionRadius, CollisionHeight, axis aligned in the world)
// becomes the bounding box of its rotated corners. Then it is box against box: the target box grown by that extent,
// with square corners. Hit locations and normals go back to world space.

namespace KW
{
	namespace
	{
		struct Box
		{
			dvec3 Center;
			dvec3 Axis[3]; // box frame axes in world space
			dvec3 Extents;
		};

		// IDA Engine.dll: ?GetCollisionBoundingBox@UBox@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103FE130] (local box, bWorld=false)
		// IDA Engine.dll: ?ToLocal@AActor@@UBE?AVFCoords@@XZ [HP1 0x1031BAC0]
		Box GetBox(UActor* actor)
		{
			Box box;
			box.Center = to_dvec3(actor->Location());
			mat4 rot = Coords::Rotation(actor->Rotation()).ToMatrix();
			box.Axis[0] = to_dvec3(normalize((rot * vec4(1.0f, 0.0f, 0.0f, 0.0f)).xyz()));
			box.Axis[1] = to_dvec3(normalize((rot * vec4(0.0f, 1.0f, 0.0f, 0.0f)).xyz()));
			box.Axis[2] = to_dvec3(normalize((rot * vec4(0.0f, 0.0f, 1.0f, 0.0f)).xyz()));
			float width = CollisionWidth(actor) == 0.0f ? actor->CollisionRadius() : CollisionWidth(actor);
			box.Extents = dvec3(actor->CollisionRadius(), width, actor->CollisionHeight());
			return box;
		}

		dvec3 ToLocal(const Box& box, const dvec3& v)
		{
			return dvec3(dot(v, box.Axis[0]), dot(v, box.Axis[1]), dot(v, box.Axis[2]));
		}

		vec3 ToWorldNormal(const Box& box, const dvec3& n)
		{
			dvec3 w = box.Axis[0] * n.x + box.Axis[1] * n.y + box.Axis[2] * n.z;
			return vec3((float)w.x, (float)w.y, (float)w.z);
		}

		// The checking actor's world extent box (radius, radius, height) in the box's frame: half size of the bounding
		// box of its rotated corners (FBox::TransformBy).
		dvec3 CheckExtents(const Box& box, double height, double radius)
		{
			dvec3 e;
			for (int i = 0; i < 3; i++)
				e[i] = radius * (std::abs(box.Axis[i].x) + std::abs(box.Axis[i].y)) + height * std::abs(box.Axis[i].z);
			return e;
		}

		// The axis aligned point check (box of half size e at p against the box of half size b at the origin), in the
		// box's frame. Overlap needs more than 0.003 of penetration on every axis. On overlap: the push-out normal and
		// depth of the shallowest axis, tested in the order +Z, -Z, +Y, -Y, +X, -X (later axes only win when smaller).
		// IDA Engine.dll: not exported: sub_103FC1D0 [HP1 0x103FC1D0] (thunk sub_10302D6F; UBox::PointCheck's test)
		bool LocalPointCheck(const dvec3& b, const dvec3& p, const dvec3& e, dvec3& normal, double& depth)
		{
			dvec3 lo = p - e, hi = p + e;
			for (int i = 0; i < 3; i++)
			{
				if (!(-b[i] + 0.003 < hi[i] && b[i] - 0.003 > lo[i]))
					return false;
			}
			const double candidates[6] = { b.z - lo.z, hi.z + b.z, b.y - lo.y, hi.y + b.y, b.x - lo.x, hi.x + b.x };
			const dvec3 normals[6] = { dvec3(0, 0, 1), dvec3(0, 0, -1), dvec3(0, 1, 0), dvec3(0, -1, 0), dvec3(1, 0, 0), dvec3(-1, 0, 0) };
			depth = candidates[0];
			normal = normals[0];
			for (int i = 1; i < 6; i++)
			{
				if (candidates[i] < depth)
				{
					depth = candidates[i];
					normal = normals[i];
				}
			}
			return true;
		}
	}

	// IDA Engine.dll: ?LineCheck@UBox@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@22K@Z [HP1 0x103FE620]
	// IDA Engine.dll: ?PointCheck@UBox@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@2K@Z [HP1 0x103FE590]
	bool IsBoxCollider(UActor* actor)
	{
		return CollideType(actor) == CT_Box && !actor->Brush();
	}

	// The segment origin + dir * [tmin, tmax] against the box. Starting outside: the earliest of the six grown faces
	// the segment crosses (tested -X, +X, -Y, +Y, -Z, +Z) whose crossing point, 0.001 inside, lies inside the grown box;
	// the time is pulled back by 0.001 of the segment. Starting inside: blocked at once only when the end is inside too
	// and deeper (larger shallowest-axis depth) than the start, with the start's push-out normal; otherwise free, so an
	// actor that ends up overlapping can move out or along.
	// IDA Engine.dll: not exported: sub_103FC980 [HP1 0x103FC980] (thunk sub_10303166; UBox::LineCheck's test)
	double BoxActorTrace(UActor* actor, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, double height, double radius, vec3& outNormal)
	{
		Box box = GetBox(actor);
		dvec3 e = CheckExtents(box, height, radius);
		dvec3 start = ToLocal(box, origin + dirNormalized * tmin - box.Center);
		dvec3 end = ToLocal(box, origin + dirNormalized * tmax - box.Center);
		dvec3 b = box.Extents;

		dvec3 startNormal;
		double startDepth;
		if (LocalPointCheck(b, start, e, startNormal, startDepth))
		{
			dvec3 endNormal;
			double endDepth;
			if (LocalPointCheck(b, end, e, endNormal, endDepth) && endDepth > startDepth)
			{
				outNormal = ToWorldNormal(box, startNormal);
				return tmin;
			}
			return tmax;
		}

		dvec3 grown = b + e;
		dvec3 delta = end - start;
		double time = 1.0;
		dvec3 hitNormal(0.0);
		for (int i = 0; i < 6; i++)
		{
			int axis = i / 2;
			double sign = (i & 1) ? 1.0 : -1.0; // -X face first, then +X
			double dist = sign < 0.0 ? (-grown[axis] - start[axis]) : (start[axis] - grown[axis]);
			double travel = sign < 0.0 ? delta[axis] : -delta[axis];
			if (dist <= -0.003 || std::max(dist, 0.0) >= travel * time)
				continue;
			double t = std::max(dist / travel, 0.0);
			dvec3 n(0.0);
			n[axis] = sign;
			dvec3 p = start + delta * t - n * 0.001;
			if (p.x > -grown.x && p.x < grown.x && p.y > -grown.y && p.y < grown.y && p.z > -grown.z && p.z < grown.z)
			{
				time = std::max(t - 0.001, 0.0);
				hitNormal = n;
			}
		}
		if (time == 1.0)
			return tmax;
		outNormal = ToWorldNormal(box, hitNormal);
		return tmin + (tmax - tmin) * time;
	}

	// UBox::PointCheck with the cylinder's extent box (CollisionRadius, CollisionRadius, CollisionHeight).
	// IDA Engine.dll: not exported: sub_103FC1D0 [HP1 0x103FC1D0] (thunk sub_10302D6F)
	bool BoxActorOverlapCylinder(UActor* actor, const dvec3& center, double height, double radius)
	{
		Box box = GetBox(actor);
		dvec3 normal;
		double depth;
		return LocalPointCheck(box.Extents, ToLocal(box, center - box.Center), CheckExtents(box, height, radius), normal, depth);
	}

	bool BoxActorOverlapSphere(UActor* actor, const dvec3& center, double radius)
	{
		Box box = GetBox(actor);
		dvec3 p = ToLocal(box, center - box.Center);
		double dx = std::max(std::abs(p.x) - box.Extents.x, 0.0);
		double dy = std::max(std::abs(p.y) - box.Extents.y, 0.0);
		double dz = std::max(std::abs(p.z) - box.Extents.z, 0.0);
		return dx * dx + dy * dy + dz * dz < radius * radius;
	}

	vec3 BoxCollisionExtents(UActor* actor)
	{
		Box box = GetBox(actor);
		dvec3 e(0.0);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				e[i] += std::abs(box.Axis[j][i]) * box.Extents[j];
		return vec3((float)e.x, (float)e.y, (float)e.z);
	}

	void OverrideNative(int index, void (*registerFunc)());

	// HP1 adds an optional third parameter: SetCollisionSize(NewRadius, NewHeight, optional NewWidth). NewWidth
	// defaults to the current CollisionWidth and is stored there; MountFinish (harry.uc) passes three values.
	// Always returns true (no room check).
	// IDA Engine.dll: ?execSetCollisionSize@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A870]
	// IDA Engine.dll: ?SetCollisionSize@AActor@@QAEXMM@Z [HP1 0x10379C80]
	static void NSetCollisionSize(UObject* Self, float NewRadius, float NewHeight, std::optional<float> NewWidth, BitfieldBool& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		CollisionWidth(actor) = NewWidth.value_or(CollisionWidth(actor));
		actor->SetCollisionSize(NewRadius, NewHeight);
		ReturnValue = true;
	}

	namespace
	{
		// Object.BoundingBox as script memory (Min, Max, IsValid).
		struct ScriptBoundingBox
		{
			vec3 Min;
			vec3 Max;
			uint8_t IsValid;
		};

		// The axis aligned box around a local box's corners moved by a transform.
		BBox TransformBox(const BBox& box, const mat4& m)
		{
			BBox result;
			for (int i = 0; i < 8; i++)
			{
				vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z);
				vec3 p = (m * vec4(corner, 1.0f)).xyz();
				result.min = i == 0 ? p : vec3(std::min(result.min.x, p.x), std::min(result.min.y, p.y), std::min(result.min.z, p.z));
				result.max = i == 0 ? p : vec3(std::max(result.max.x, p.x), std::max(result.max.y, p.y), std::max(result.max.z, p.z));
			}
			return result;
		}

		// AActor::ToWorld: rotation, then Location (no PrePivot, no scale).
		mat4 ToWorld(UActor* a)
		{
			return mat4::translate(a->Location()) * Coords::Rotation(a->Rotation()).ToMatrix();
		}

		// The primitives of HP1's UPrimitive::GetCollisionBoundingBox overrides, chosen like AActor::GetPrimitive.
		enum class PrimKind { Cylinder, OrientedCylinder, Box, Mesh, Brush };

		// IDA Engine.dll: ?GetPrimitive@AActor@@UBEPAVUPrimitive@@XZ [HP1 0x1037A880]
		PrimKind GetPrimitive(UActor* a)
		{
			uint8_t type = CollideType(a);
			if (type == CT_Shape && a->Mesh())
				return PrimKind::Mesh;
			if (type == CT_Shape && a->Brush())
				return PrimKind::Brush;
			if (type == CT_OrientedCylinder)
				return PrimKind::OrientedCylinder;
			if (type == CT_Box)
				return PrimKind::Box;
			return PrimKind::Cylinder;
		}

		// The collision box of a primitive for an actor, in the actor's local space or (bWorld) the world.
		// IDA Engine.dll: ?GetCollisionBoundingBox@UPrimitive@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103FA2F0]
		// IDA Engine.dll: ?GetCollisionBoundingBox@UOrientedCylinder@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103FB190]
		// IDA Engine.dll: ?GetCollisionBoundingBox@UBox@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103FE130]
		// IDA Engine.dll: ?GetCollisionBoundingBox@UBoxPrim@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103FE7A0] (also UModel's slot)
		// IDA Engine.dll: ?GetCollisionBoundingBox@UMesh@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x103B84D0]
		// IDA Engine.dll: ?GetCollisionBoundingBox@USkeletalMesh@@UBE?AVFBox@@PBVAActor@@_N@Z [HP1 0x1041B8B0]
		BBox GetCollisionBoundingBox(UActor* a, PrimKind kind, bool world)
		{
			float r = a->CollisionRadius();
			float h = a->CollisionHeight();
			switch (kind)
			{
			default:
			case PrimKind::Cylinder:
			{
				// The vertical centre is offset by CollisionWidth (as in the original).
				vec3 c = world ? a->Location() : vec3(0.0f);
				c.z += CollisionWidth(a);
				return BBox(c - vec3(r, r, h), c + vec3(r, r, h));
			}
			case PrimKind::OrientedCylinder:
			case PrimKind::Box:
			{
				float w = kind == PrimKind::Box && CollisionWidth(a) != 0.0f ? CollisionWidth(a) : r;
				BBox box(vec3(-r, -w, -h), vec3(r, w, h));
				return world ? TransformBox(box, ToWorld(a)) : box;
			}
			case PrimKind::Brush:
			{
				UModel* brush = a->Brush();
				return world ? TransformBox(brush->BoundingBox, ToWorld(a)) : brush->BoundingBox;
			}
			case PrimKind::Mesh:
			{
				UMesh* mesh = a->Mesh();
				USkeletalMesh* skel = UObject::TryCast<USkeletalMesh>(mesh);
				if (world && skel)
					return GetSkeletalCollisionBox(a, skel);
				if (world)
				{
					mat4 objectToWorld = mat4::translate(a->Location() + a->PrePivot()) * Coords::Rotation(a->Rotation()).ToMatrix() * mat4::scale(a->DrawScale());
					return TransformBox(mesh->BoundingBox, objectToWorld * mesh->meshToObject);
				}
				vec3 scale = mesh->Scale * a->DrawScale();
				return BBox((mesh->BoundingBox.min - mesh->Origin) * scale, (mesh->BoundingBox.max - mesh->Origin) * scale);
			}
			}
		}
	}

	// With bVisual the actor's Mesh (or Brush) gives the box, otherwise its collision primitive. Always in the world.
	// Target and baseSpell aim at the centre of this box (spell lock-on and homing).
	// IDA Engine.dll: ?execGetWorldCollisionBox@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A950]
	static void NGetWorldCollisionBox(UObject* Self, std::optional<bool> bVisual, ScriptBoundingBox& ReturnValue)
	{
		UActor* a = UObject::Cast<UActor>(Self);
		PrimKind kind;
		if (bVisual.value_or(false) && a->Mesh())
			kind = PrimKind::Mesh;
		else if (bVisual.value_or(false) && a->Brush())
			kind = PrimKind::Brush;
		else
			kind = GetPrimitive(a);
		BBox box = GetCollisionBoundingBox(a, kind, true);
		ReturnValue.Min = box.min;
		ReturnValue.Max = box.max;
		ReturnValue.IsValid = 1;
	}

	// The size (Max - Min) of the actor's local box: for a skeletal mesh the average of its per-frame bounding boxes
	// (USkeletalMesh+504, averaged in Serialize; raw mesh units), otherwise the local collision box of the Mesh, the
	// Brush or the collision primitive. ActorShadow sizes its decal with it.
	// IDA Engine.dll: ?execGetRenderExtent@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040AA10]
	// IDA Engine.dll: ?Serialize@USkeletalMesh@@UAEXAAVFArchive@@@Z [HP1 0x1041A7D0] (the averaged box)
	static void NGetRenderExtent(UObject* Self, vec3& ReturnValue)
	{
		UActor* a = UObject::Cast<UActor>(Self);
		BBox box;
		if (USkeletalMesh* skel = UObject::TryCast<USkeletalMesh>(a->Mesh()))
		{
			box = BBox(vec3(0.0f), vec3(0.0f));
			if (!skel->BoundingBoxes.empty())
			{
				for (const BBox& b : skel->BoundingBoxes)
				{
					box.min += b.min;
					box.max += b.max;
				}
				float inv = 1.0f / (float)skel->BoundingBoxes.size();
				box.min *= inv;
				box.max *= inv;
			}
		}
		else
		{
			box = GetCollisionBoundingBox(a, a->Mesh() ? PrimKind::Mesh : a->Brush() ? PrimKind::Brush : GetPrimitive(a), false);
		}
		ReturnValue = box.max - box.min;
	}

	// Switching to PHYS_None or PHYS_Rotating stops the actor dead: HP1 clears Velocity and Acceleration. Cutscenes rely on it:
	// Capture puts Harry in CutIdleing (PHYS_Rotating), then CutMovingTo walks him with MoveSmooth under PHYS_Walking. Without
	// the reset the player's last run velocity/acceleration carried him past the cutscene mark and he ran in place, turned away.
	// The Base handling of the original (FindBase/SetBase by physics mode) is left to SurrealEngine as before; NOT ported.
	// IDA Engine.dll: ?execSetPhysics@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x103E4AD0]
	// IDA Engine.dll: ?setPhysics@AActor@@QAEXEPAV1@@Z [HP1 0x103E5140]
	static void NSetPhysics(UObject* Self, uint8_t newPhysics)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		if (actor->Physics() == newPhysics)
			return;
		actor->SetPhysics(newPhysics);
		if (newPhysics == PHYS_None || newPhysics == PHYS_Rotating)
		{
			actor->Velocity() = vec3(0.0f);
			actor->Acceleration() = vec3(0.0f);
		}
	}

	void RegisterCollisionNatives()
	{
		OverrideNative(283, [] { RegisterVMNativeFunc_4("Actor", "SetCollisionSize", &NSetCollisionSize, 283); });
		OverrideNative(286, [] { RegisterVMNativeFunc_2("Actor", "GetWorldCollisionBox", &NGetWorldCollisionBox, 286); });
		OverrideNative(274, [] { RegisterVMNativeFunc_1("Actor", "GetRenderExtent", &NGetRenderExtent, 274); });
		OverrideNative(3970, [] { RegisterVMNativeFunc_1("Actor", "SetPhysics", &NSetPhysics, 3970); });
	}
}
