#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "Math/coords.h"
#include "VM/NativeFunc.h"
#include "Packages/Engine/Resources/Mesh/UMesh.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <cmath>

// HP1 actors pick their collision primitive with Actor.CollideType. CT_Box is an oriented box centered on
// the actor, with half extents CollisionRadius (X), CollisionWidth (Y) and CollisionHeight (Z) in the
// actor's rotated frame. Lev_Tut1 uses it for BlockAll walls along the Grand Hall stairs (250 x 10 x 200),
// BlockPlayers, Triggers and CutScene trigger volumes. As cylinders they block or trigger far too much.
//
// Upstream's moving shapes are vertical cylinders (radius, half height). In the box's frame a yaw-rotated
// box keeps the cylinder vertical, so a swept cylinder against the box is a ray against the box grown by
// the radius in X/Y and the half height in Z. The grown box has square corners where the exact shape is
// rounded, which only matters right at the box's vertical edges.

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

		Box GetBox(UActor* actor)
		{
			Box box;
			box.Center = to_dvec3(actor->Location());
			mat4 rot = Coords::Rotation(actor->Rotation()).ToMatrix();
			box.Axis[0] = to_dvec3(normalize((rot * vec4(1.0f, 0.0f, 0.0f, 0.0f)).xyz()));
			box.Axis[1] = to_dvec3(normalize((rot * vec4(0.0f, 1.0f, 0.0f, 0.0f)).xyz()));
			box.Axis[2] = to_dvec3(normalize((rot * vec4(0.0f, 0.0f, 1.0f, 0.0f)).xyz()));
			box.Extents = dvec3(std::abs(actor->CollisionRadius()), std::abs(CollisionWidth(actor)), std::abs(actor->CollisionHeight()));
			return box;
		}

		dvec3 ToLocal(const Box& box, const dvec3& v)
		{
			return dvec3(dot(v, box.Axis[0]), dot(v, box.Axis[1]), dot(v, box.Axis[2]));
		}

		// Cylinder half extents measured along the box axes. Exact for yaw-only boxes; for tilted boxes it
		// is the cylinder's bounding box projected on the axes.
		dvec3 CylinderExtents(const Box& box, double height, double radius)
		{
			dvec3 e;
			for (int i = 0; i < 3; i++)
			{
				double horiz = std::sqrt(box.Axis[i].x * box.Axis[i].x + box.Axis[i].y * box.Axis[i].y);
				e[i] = radius * horiz + height * std::abs(box.Axis[i].z);
			}
			return e;
		}
	}

	// IDA Engine.dll: ?LineCheck@UBox@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@22K@Z [HP1 0x103FE620]
	// IDA Engine.dll: ?PointCheck@UBox@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@2K@Z [HP1 0x103FE590]
	//   NOT yet verified against these: written from the Actor.uc CollideType comments. UBox builds its box via
	//   vtable+96 and tests in sub_10303166 (line) / sub_10302D6F (point).
	bool IsBoxCollider(UActor* actor)
	{
		return CollideType(actor) == CT_Box && !actor->Brush();
	}

	double BoxActorTrace(UActor* actor, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, double height, double radius, vec3& outNormal)
	{
		Box box = GetBox(actor);
		dvec3 ext = box.Extents + CylinderExtents(box, height, radius);
		dvec3 o = ToLocal(box, origin - box.Center);
		dvec3 d = ToLocal(box, dirNormalized);

		// Slab test
		double tEnter = -1e30, tExit = 1e30;
		int enterAxis = -1;
		double enterSign = 0.0;
		for (int i = 0; i < 3; i++)
		{
			if (std::abs(d[i]) < 1e-12)
			{
				if (o[i] < -ext[i] || o[i] > ext[i])
					return tmax;
				continue;
			}
			double t0 = (-ext[i] - o[i]) / d[i];
			double t1 = (ext[i] - o[i]) / d[i];
			double sign = -1.0; // entering through the -ext face
			if (t0 > t1)
			{
				std::swap(t0, t1);
				sign = 1.0;
			}
			if (t0 > tEnter)
			{
				tEnter = t0;
				enterAxis = i;
				enterSign = sign;
			}
			tExit = std::min(tExit, t1);
		}
		if (enterAxis < 0 || tEnter > tExit || tExit < tmin || tEnter >= tmax)
			return tmax;

		if (tEnter < tmin)
		{
			// Starting inside (or touching) the box: block only movement further in, through the nearest
			// face, so an actor that ends up overlapping can still walk out.
			int axis = 0;
			double best = 1e30, sign = 1.0;
			for (int i = 0; i < 3; i++)
			{
				double depth = ext[i] - std::abs(o[i]);
				if (depth < best)
				{
					best = depth;
					axis = i;
					sign = o[i] >= 0.0 ? 1.0 : -1.0;
				}
			}
			if (d[axis] * sign >= 0.0)
				return tmax;
			enterAxis = axis;
			enterSign = sign;
			tEnter = tmin;
		}

		dvec3 n = box.Axis[enterAxis] * enterSign;
		outNormal = vec3((float)n.x, (float)n.y, (float)n.z);
		return tEnter;
	}

	bool BoxActorOverlapCylinder(UActor* actor, const dvec3& center, double height, double radius)
	{
		Box box = GetBox(actor);
		dvec3 p = ToLocal(box, center - box.Center);
		bool yawOnly = std::abs(box.Axis[2].z) > 0.9999;
		if (!yawOnly)
		{
			dvec3 ext = box.Extents + CylinderExtents(box, height, radius);
			return std::abs(p.x) < ext.x && std::abs(p.y) < ext.y && std::abs(p.z) < ext.z;
		}
		if (std::abs(p.z) >= box.Extents.z + height)
			return false;
		double dx = std::max(std::abs(p.x) - box.Extents.x, 0.0);
		double dy = std::max(std::abs(p.y) - box.Extents.y, 0.0);
		return dx * dx + dy * dy < radius * radius;
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
