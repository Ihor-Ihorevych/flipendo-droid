#include "Precomp.h"
#include "KWCheck.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Brush/UBrush.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Math/coords.h"
#include <cmath>

// HP1's BSP collision (Engine.dll): line, box sweep and point checks against a model's BSP, the level's or a mover
// brush's. A box is tested against the convex hulls of the solid leaves it reaches (UModel.LeafHulls: the hull's node
// planes, then its bounding box), the planes pushed out by the box, plus bevel planes where two hull planes face
// opposite ways along an axis. docs/re/engine/collision.md

namespace KW
{
	ModelFrame ModelFrame::Level(UModel* model)
	{
		ModelFrame frame;
		frame.Model = model;
		return frame;
	}

	ModelFrame ModelFrame::Brush(UActor* owner)
	{
		ModelFrame frame;
		frame.Model = owner->Brush();
		frame.Owner = owner;
		frame.Transformed = true;
		UBrush* brush = UObject::TryCast<UBrush>(owner);
		vec3 scale = brush ? brush->MainScale().Scale : vec3(1.0f);
		frame.LocalToWorld = mat4::translate(owner->Location()) * Coords::Rotation(owner->Rotation()).ToMatrix() * mat4::scale(scale) * mat4::translate(-owner->PrePivot());
		frame.WorldToLocal = mat4::translate(owner->PrePivot()) * mat4::scale(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z) * Coords::InverseRotation(owner->Rotation()).ToMatrix() * mat4::translate(-owner->Location());
		return frame;
	}

	vec4 ModelFrame::WorldPlane(const vec4& p) const
	{
		if (!Transformed)
			return p;
		// A plane goes through the inverse transpose; for HP1's movers (rotation, translation) that is FPlane::TransformPlaneByOrtho
		mat4 invT = mat4::transpose(WorldToLocal);
		vec4 w = invT * vec4(p.x, p.y, p.z, -p.w);
		float len = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
		if (len <= 0.0f)
			return p;
		return vec4(w.x / len, w.y / len, w.z / len, -w.w / len);
	}

	vec4 ModelFrame::Plane(int node) const
	{
		const BspNode& n = Model->Nodes[node];
		return WorldPlane(vec4(n.PlaneX, n.PlaneY, n.PlaneZ, n.PlaneW));
	}

	vec3 ModelFrame::WorldNormal(const vec3& n) const
	{
		if (!Transformed)
			return n;
		vec4 p = WorldPlane(vec4(n, 0.0f));
		return vec3(p.x, p.y, p.z);
	}

	static double PlaneDot(const vec4& p, const vec3& v)
	{
		return (double)p.x * v.x + (double)p.y * v.y + (double)p.z * v.z - p.w;
	}

	static double BoxPushOut(const vec3& extent, const vec4& p)
	{
		return std::abs((double)extent.x * p.x) + std::abs((double)extent.y * p.y) + std::abs((double)extent.z * p.z);
	}

	// FBspNode::IsCsg: a node with a polygon whose flags don't say it isn't solid
	// IDA Engine.dll: not exported: sub_1042CDA0 [HP1 0x1042CDA0] (thunk 0x10301398, called by the BSP checks below)
	static bool IsCsg(const BspNode& node, uint8_t extraFlags)
	{
		return node.NumVertices != 0 && (node.NodeFlags & (extraFlags | 0x21)) == 0;
	}

	// The line of two planes: a point on it, its direction (normalized). False for parallel planes.
	// IDA Engine.dll: not exported: sub_1042CAF0 [HP1 0x1042CAF0] (thunk 0x10304BD8, the bevel planes of the hull checks)
	static bool PlanesIntersection(vec3& point, vec3& dir, const vec4& a, const vec4& b)
	{
		dir = cross(vec3(a.x, a.y, a.z), vec3(b.x, b.y, b.z));
		double len2 = (double)dir.x * dir.x + (double)dir.y * dir.y + (double)dir.z * dir.z;
		if (len2 < 0.0000010000001)
		{
			point = vec3(0.0f);
			dir = vec3(0.0f);
			return false;
		}
		vec3 an(a.x, a.y, a.z), bn(b.x, b.y, b.z);
		point = (cross(dir, an) * b.w + cross(bn, dir) * a.w) * (float)(1.0 / len2);
		dir = normalize(dir);
		return true;
	}

	static vec3 SafeNormal(const vec3& v)
	{
		float len = length(v);
		return len > 0.0f ? v / len : vec3(0.0f);
	}

	// A convex leaf hull: its planes (world space), the sign of each plane along X/Y/Z (for the bevels), its box.
	struct LeafHull
	{
		int Count = 0;
		vec4 Planes[64];
		int Nodes[64];
		int Signs[64];
		vec3 BoxMin, BoxMax;
	};

	static bool LoadHull(LeafHull& hull, const ModelFrame& frame, int node)
	{
		UModel* model = frame.Model;
		int bound = model->Nodes[node].CollisionBound;
		if (bound == -1)
			return false;
		const int32_t* list = &model->LeafHulls[bound];
		hull.Count = 0;
		while (list[hull.Count] != -1 && hull.Count < 64)
		{
			int index = list[hull.Count];
			const BspNode& n = model->Nodes[index & ~0x40000000];
			vec4 p = frame.WorldPlane(vec4(n.PlaneX, n.PlaneY, n.PlaneZ, n.PlaneW));
			if (index & 0x40000000)
				p = vec4(-p.x, -p.y, -p.z, -p.w);
			hull.Planes[hull.Count] = p;
			hull.Nodes[hull.Count] = index & ~0x40000000;
			int sx = p.x < 0.0f ? 1 : (p.x > 0.0f ? 2 : 0);
			int sy = p.y < 0.0f ? 4 : (p.y > 0.0f ? 8 : 0);
			int sz = p.z < 0.0f ? 16 : (p.z > 0.0f ? 32 : 0);
			hull.Signs[hull.Count] = sx | sy | sz;
			hull.Count++;
		}
		const float* box = (const float*)&list[hull.Count + 1];
		hull.BoxMin = vec3(box[0], box[1], box[2]);
		hull.BoxMax = vec3(box[3], box[4], box[5]);
		return true;
	}

	// The hull's bounding box as planes. Only for the level (an untransformed model). The -X, -Y and both Z sides are
	// pushed out by 0.1, the +X and +Y sides in by 0.1 (as HP1 does).
	static void HullBoxPlanes(const LeafHull& hull, vec4 planes[6])
	{
		planes[0] = vec4(0.0f, 0.0f, -1.0f, 0.1f - hull.BoxMin.z);
		planes[1] = vec4(0.0f, 0.0f, 1.0f, hull.BoxMax.z + 0.1f);
		planes[2] = vec4(-1.0f, 0.0f, 0.0f, 0.1f - hull.BoxMin.x);
		planes[3] = vec4(1.0f, 0.0f, 0.0f, hull.BoxMax.x - 0.1f);
		planes[4] = vec4(0.0f, -1.0f, 0.0f, 0.1f - hull.BoxMin.y);
		planes[5] = vec4(0.0f, 1.0f, 0.0f, hull.BoxMax.y - 0.1f);
	}

	// The bevel plane between hull planes i and j along an axis, when they face opposite ways along it. False when there is
	// none to test.
	static bool BevelPlane(const LeafHull& hull, int i, int j, int axis, vec4& bevel)
	{
		int mask = 3 << (axis * 2);
		if (((hull.Signs[i] | hull.Signs[j]) & mask) != mask)
			return false;
		vec3 a(0.0f);
		a[axis] = 1.0f;
		vec3 ni(hull.Planes[i].x, hull.Planes[i].y, hull.Planes[i].z);
		vec3 nj(hull.Planes[j].x, hull.Planes[j].y, hull.Planes[j].z);
		if (dot(cross(a, ni), cross(a, nj)) <= 0.001f)
			return false;
		vec3 point, dir;
		PlanesIntersection(point, dir, hull.Planes[i], hull.Planes[j]);
		vec3 normal = SafeNormal(cross(a, dir));
		if (dot(ni, normal) < 0.0f)
			normal = -normal;
		bevel = vec4(normal, dot(point, normal));
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Line (zero extent)

	struct RayWalk
	{
		CheckResult* Hit;
		const ModelFrame* Frame;
		uint8_t ExtraFlags;
		bool WasOutside = false;

		// True = keep going (nothing hit in this part), false = hit.
		// IDA Engine.dll: not exported: sub_104294C0 [HP1 0x104294C0] (thunk 0x1030222F, called by UModel::LineCheck for a
		// zero extent)
		bool Walk(int prevNode, int node, vec3 end, vec3 start, bool outside)
		{
			UModel* model = Frame->Model;
			while (node != -1)
			{
				const BspNode& n = model->Nodes[node];
				vec4 plane = Frame->Plane(node);
				double distStart = PlaneDot(plane, start);
				double distEnd = PlaneDot(plane, end);
				if (distStart > -0.001 && distEnd > -0.001)
				{
					outside = outside || IsCsg(n, ExtraFlags & 0xCE);
					node = n.Front;
					continue;
				}
				if (distStart < 0.001 && distEnd < 0.001)
				{
					outside = outside && !IsCsg(n, ExtraFlags & 0xCE);
					node = n.Back;
					continue;
				}

				vec3 mid = start + (start - end) * (float)(distStart / (distEnd - distStart));
				int side = distStart <= 0.0 ? 0 : 1;
				bool nearOutside = side == 0 ? (outside && !IsCsg(n, ExtraFlags)) : (outside || IsCsg(n, ExtraFlags));
				if (!Walk(node, side ? n.Front : n.Back, mid, start, nearOutside))
					return false;

				if (side == 1)
					outside = outside && !IsCsg(n, ExtraFlags);
				else
					outside = outside || IsCsg(n, ExtraFlags);
				prevNode = node;
				node = side ? n.Back : n.Front;
				start = mid;
			}

			if (outside)
			{
				WasOutside = true;
				return true;
			}
			if (WasOutside || (ExtraFlags & 0x10) == 0)
			{
				const BspNode& n = model->Nodes[prevNode];
				Hit->Location = start;
				Hit->Normal = vec3(n.PlaneX, n.PlaneY, n.PlaneZ); // the model's own plane: LineCheck takes it to world space
				Hit->Model = model;
				Hit->Item = prevNode;
				return false;
			}
			return true;
		}
	};

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Box sweep

	struct BoxSweep
	{
		CheckResult* Hit;
		const ModelFrame* Frame;
		vec3 Extent;
		vec3 End, Start;
		bool HitSomething = false;

		LeafHull Hull;
		float T0 = -1.0f, T1 = 1.0f;
		vec3 BestNormal;
		int BestItem = -1;

		// Clip the swept segment against a plane pushed out by the box. False when the box misses the hull.
		// IDA Engine.dll: not exported: sub_1042C050 [HP1 0x1042C050] (thunk 0x103027D9, the box sweep's clip step)
		bool Clip(const vec4& plane, int item)
		{
			double push = BoxPushOut(Extent, plane);
			double distStart = PlaneDot(plane, Start);
			double distEnd = PlaneDot(plane, End);
			double d = distStart - push;
			if (distStart > distEnd && d >= -push && d < 0.0)
				d = 0.0;
			double denom = distStart - distEnd;
			double t = d / denom;
			if (denom < -0.0000099999997)
			{
				if (t < T1)
					T1 = (float)t;
			}
			else if (denom > 0.0000099999997)
			{
				if (t > T0)
				{
					T0 = (float)t;
					BestNormal = vec3(plane.x, plane.y, plane.z);
					if (item >= 0)
						BestItem = item;
				}
			}
			else if (distStart > push && distEnd > push)
			{
				return false;
			}
			return T0 < T1;
		}

		void TestLeaf(int node)
		{
			if (!LoadHull(Hull, *Frame, node))
				return;
			T0 = -1.0f;
			T1 = Hit->Time;
			BestNormal = vec3(0.0f);
			BestItem = node;

			for (int i = 0; i < Hull.Count; i++)
			{
				if (!Clip(Hull.Planes[i], Hull.Nodes[i]))
					return;
			}
			if (!Frame->Transformed)
			{
				vec4 box[6];
				HullBoxPlanes(Hull, box);
				for (int i = 0; i < 6; i++)
				{
					if (!Clip(box[i], -1))
						return;
				}
			}
			for (int i = 1; i < Hull.Count; i++)
			{
				for (int j = 0; j < i; j++)
				{
					for (int axis = 0; axis < 3; axis++)
					{
						vec4 bevel;
						if (BevelPlane(Hull, i, j, axis, bevel) && !Clip(bevel, -1))
							return;
					}
				}
			}

			if (T0 > -1.0f && T0 < T1 && T1 > 0.0f)
			{
				Hit->Time = T0;
				Hit->Normal = BestNormal;
				Hit->Actor = Frame->Owner;
				Hit->Model = Frame->Model;
				Hit->Item = BestItem;
				HitSomething = true;
			}
		}

		// IDA Engine.dll: not exported: sub_1042A480 [HP1 0x1042A480] (thunk 0x103028DD, called by UModel::LineCheck with an
		// extent: walks the BSP with the planes pushed out by 1.1 times the box, tests the solid leaves' hulls)
		void Walk(int parent, int node, bool outside)
		{
			UModel* model = Frame->Model;
			vec3 extent11 = Extent * 1.1f;
			while (node != -1)
			{
				const BspNode& n = model->Nodes[node];
				vec4 plane = Frame->Plane(node);
				double distStart = PlaneDot(plane, Start);
				double distEnd = PlaneDot(plane, End);
				double push = BoxPushOut(extent11, plane);
				bool backTouched = distStart <= push || distEnd <= push;
				bool frontTouched = distStart >= -push || distEnd >= -push;
				bool nearIsFront = distStart >= distEnd;

				if (nearIsFront ? frontTouched : backTouched)
				{
					bool nearOutside = nearIsFront ? (outside || IsCsg(n, 0)) : (outside && !IsCsg(n, 0));
					Walk(node, nearIsFront ? n.Front : n.Back, nearOutside);
				}
				if (!(nearIsFront ? backTouched : frontTouched))
					return;

				parent = node;
				node = nearIsFront ? n.Back : n.Front;
				outside = nearIsFront ? (outside && !IsCsg(n, 0)) : (outside || IsCsg(n, 0));
			}
			if (!outside)
				TestLeaf(parent);
		}
	};

	// IDA Engine.dll: ?LineCheck@UModel@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@22K@Z [HP1 0x10429C80]
	bool ModelLineCheck(CheckResult& hit, const ModelFrame& frame, const vec3& end, const vec3& start, const vec3& extent, uint8_t extraNodeFlags)
	{
		UModel* model = frame.Model;
		if (model->Nodes.empty())
		{
			if (model->RootOutside)
				return true;
			hit.Time = 0.0f;
			hit.Normal = vec3(0.0f, 0.0f, 1.0f);
			hit.Location = start;
			return false;
		}

		if (extent.x != 0.0f || extent.y != 0.0f || extent.z != 0.0f)
		{
			hit.Time = 2.0f;
			BoxSweep sweep;
			sweep.Hit = &hit;
			sweep.Frame = &frame;
			sweep.Extent = extent;
			sweep.End = end;
			sweep.Start = start;
			float dist = length(end - start);
			sweep.Walk(0, 0, model->RootOutside != 0);
			if (!sweep.HitSomething)
				return true;
			hit.Time = std::clamp(hit.Time - 0.5f / dist, 0.0f, 1.0f);
			hit.Location = start + (end - start) * hit.Time;
			return hit.Time == 1.0f;
		}

		RayWalk walk;
		walk.Hit = &hit;
		walk.Frame = &frame;
		walk.ExtraFlags = extraNodeFlags;
		if (walk.Walk(0, 0, end, start, model->RootOutside != 0))
			return true;

		vec3 dir = end - start;
		double len2 = (double)dir.x * dir.x + (double)dir.y * dir.y + (double)dir.z * dir.z;
		double len = std::sqrt(len2);
		vec3 toHit = hit.Location - start;
		hit.Time = (float)((toHit.x * (double)dir.x + toHit.y * (double)dir.y + toHit.z * (double)dir.z) / len2);
		hit.Time = std::clamp(hit.Time - (float)(0.5 / len), 0.0f, 1.0f);
		hit.Actor = frame.Owner;
		hit.Location = start + dir * hit.Time;
		hit.Normal = frame.WorldNormal(hit.Normal);
		vec3 back = -(dir * (float)(1.0 / len));
		if (dot(back, hit.Normal) < 0.0f)
			hit.Normal = -hit.Normal;
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Point / box at a location

	struct BoxPoint
	{
		CheckResult* Hit;
		const ModelFrame* Frame;
		vec3 Extent;
		vec3 Point;
		uint8_t ExtraFlags;
		float Best = 100000.0f;
		LeafHull Hull;

		// The box against one plane. False when the box is wholly in front (misses this hull). Records the shallowest
		// penetration: the box pushed out of that plane, 1.02 times as far.
		// IDA Engine.dll: not exported: sub_10428FB0 [HP1 0x10428FB0] (thunk 0x103039BD, the point check's bevel test)
		bool Test(const vec4& plane, int item)
		{
			double push = BoxPushOut(Extent, plane);
			double d = PlaneDot(plane, Point);
			if (d > 0.0 && d < Best)
			{
				if (d >= push)
					return false;
				Best = (float)d;
				vec3 n(plane.x, plane.y, plane.z);
				Hit->Location = Point + n * (float)(1.02 * (push - d));
				Hit->Normal = n;
				Hit->Actor = Frame->Owner;
				Hit->Model = Frame->Model;
				Hit->Item = item;
				Hit->Time = 0.0f;
			}
			return d < push;
		}

		// True = no collision
		bool TestLeaf(int node)
		{
			if (!LoadHull(Hull, *Frame, node))
				return true;
			for (int i = 0; i < Hull.Count; i++)
			{
				if (!Test(Hull.Planes[i], Hull.Nodes[i]))
					return true;
			}
			if (!Frame->Transformed)
			{
				vec4 box[6];
				HullBoxPlanes(Hull, box);
				for (int i = 0; i < 6; i++)
				{
					if (!Test(box[i], -1))
						return true;
				}
			}
			for (int i = 1; i < Hull.Count; i++)
			{
				for (int j = 0; j < i; j++)
				{
					for (int axis = 0; axis < 3; axis++)
					{
						vec4 bevel;
						if (BevelPlane(Hull, i, j, axis, bevel) && !Test(bevel, -1))
							return true;
					}
				}
			}
			return false;
		}

		// IDA Engine.dll: not exported: sub_10427630 [HP1 0x10427630] (thunk 0x10302568, called by UModel::PointCheck with an
		// extent)
		bool Walk(int parent, int node, bool outside)
		{
			UModel* model = Frame->Model;
			bool result = true;
			vec3 extent11 = Extent * 1.1f;
			while (node != -1)
			{
				const BspNode& n = model->Nodes[node];
				vec4 plane = Frame->Plane(node);
				double push = BoxPushOut(extent11, plane);
				double d = PlaneDot(plane, Point);
				if (d > -push)
				{
					if (!Walk(node, n.Front, outside || IsCsg(n, ExtraFlags)))
						result = false;
				}
				parent = node;
				node = n.Back;
				outside = outside && !IsCsg(n, ExtraFlags);
				if (d > push)
					return result;
			}
			if (!outside && !TestLeaf(parent))
				return false;
			return result;
		}
	};

	// IDA Engine.dll: ?PointCheck@UModel@@UAEHAAUFCheckResult@@PAVAActor@@VFVector@@2K@Z [HP1 0x104271D0]
	bool ModelPointCheck(CheckResult& hit, const ModelFrame& frame, const vec3& location, const vec3& extent, uint8_t extraNodeFlags)
	{
		UModel* model = frame.Model;
		hit.Model = model;
		hit.Time = 0.0f;
		hit.Normal = vec3(0.0f);
		hit.Location = location;
		hit.Actor = frame.Owner;
		bool outside = model->RootOutside != 0;
		if (model->Nodes.empty())
			return outside;

		if (extent.x == 0.0f && extent.y == 0.0f && extent.z == 0.0f)
		{
			int node = 0;
			while (true)
			{
				const BspNode& n = model->Nodes[node];
				int side;
				if (PlaneDot(frame.Plane(node), location) <= 0.0)
				{
					side = 0;
					outside = outside && !IsCsg(n, 0);
				}
				else
				{
					side = 1;
					outside = outside || IsCsg(n, 0);
				}
				int next = side ? n.Front : n.Back;
				if (next == -1)
				{
					hit.Item = side + 2 * node;
					return outside;
				}
				node = next;
			}
		}

		BoxPoint check;
		check.Hit = &hit;
		check.Frame = &frame;
		check.Extent = extent;
		check.Point = location;
		check.ExtraFlags = extraNodeFlags;
		return check.Walk(0, 0, outside);
	}

	// IDA Engine.dll: not exported: sub_10429300 [HP1 0x10429300] (called by UModel::FastLineCheck)
	static bool FastWalk(UModel* model, int node, vec3 end, vec3 start, bool outside)
	{
		if (node == -1)
			return outside;
		while (true)
		{
			const BspNode& n = model->Nodes[node];
			bool notCsg = (n.NodeFlags & 1) != 0;
			vec4 plane(n.PlaneX, n.PlaneY, n.PlaneZ, n.PlaneW);
			double distStart = PlaneDot(plane, start);
			double distEnd = PlaneDot(plane, end);
			bool startFront = distStart >= 0.0;
			bool endFront = distEnd >= 0.0;
			if (startFront != endFront)
			{
				double t = distStart / (distStart - distEnd);
				vec3 mid = start + (end - start) * (float)t;
				bool endOutside = notCsg ? outside : endFront;
				if (!FastWalk(model, endFront ? n.Front : n.Back, mid, end, endOutside))
					return false;
				end = mid;
			}
			node = startFront ? n.Front : n.Back;
			outside = notCsg ? outside : startFront;
			if (node == -1)
				return outside;
		}
	}

	// IDA Engine.dll: ?FastLineCheck@UModel@@QAEEVFVector@@0@Z [HP1 0x104291E0]
	bool ModelFastLineCheck(UModel* model, const vec3& end, const vec3& start)
	{
		if (model->Nodes.empty())
			return model->RootOutside != 0;
		return FastWalk(model, 0, end, start, model->RootOutside != 0);
	}
}
