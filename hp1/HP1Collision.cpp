#include "Precomp.h"
#include "HP1.h"
#include "HP1Actor.h"
#include "Math/coords.h"
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

namespace HP1
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
}
