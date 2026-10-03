#include "Precomp.h"
#include "HP1.h"
#include "HP1Actor.h"
#include "Anim/HP1Animation.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Mesh/UAnimation.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Render/VisibleFrame.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Light/LightSystem.h"
#include "Math/coords.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Engine.h"
#include <unordered_map>

// HP1 skeletal pose evaluation and skinning, reimplemented from HP1 Engine.dll:
//   USkeletalMesh::ApplyAnim   - bone places (quat + pos) for the current AnimSequence/AnimFrame, tween blend,
//                                aux channel overrides, root handling
//   USkeletalMesh::GetFrame    - bone hierarchy -> coords, linear blend skinning of LocalPoints
//   USkeletalMesh::GetMeshCoords - mesh to world placement
//   Core.dll: FCoords(FPlace), FCoords::operator/=(FCoords), SlerpQuat
// See docs/re/animation.md.

namespace HP1
{
	// FPlace
	struct Place
	{
		quaternion Orientation;
		vec3 Position = vec3(0.0f);
	};

	// FCoords with KnowWonder's composition. Apply(p) = Origin + XAxis*p.x + YAxis*p.y + ZAxis*p.z
	struct BoneCoords
	{
		vec3 Origin = vec3(0.0f);
		vec3 XAxis = vec3(1.0f, 0.0f, 0.0f);
		vec3 YAxis = vec3(0.0f, 1.0f, 0.0f);
		vec3 ZAxis = vec3(0.0f, 0.0f, 1.0f);

		vec3 ApplyVector(const vec3& v) const { return XAxis * v.x + YAxis * v.y + ZAxis * v.z; }
		vec3 Apply(const vec3& p) const { return Origin + ApplyVector(p); }
	};

	// Core.dll FCoords::FCoords(const FPlace&)
	// IDA Core.dll: ??0FCoords@@QAE@ABVFPlace@@@Z [HP1 Core 0x1014F7A0]
	static BoneCoords ToCoords(const Place& p)
	{
		const quaternion& q = p.Orientation;
		float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
		float xx = x2 * q.x, xy = y2 * q.x, xz = z2 * q.x;
		float yy = y2 * q.y, yz = z2 * q.y, zz = z2 * q.z;
		float wx = x2 * q.w, wy = y2 * q.w, wz = z2 * q.w;

		BoneCoords c;
		c.Origin = p.Position;
		c.XAxis = vec3(1.0f - (zz + yy), xy - wz, wy + xz);
		c.YAxis = vec3(wz + xy, 1.0f - (zz + xx), yz - wx);
		c.ZAxis = vec3(xz - wy, wx + yz, 1.0f - (yy + xx));
		return c;
	}

	// Core.dll FCoords::operator/=(const FCoords& B) on A: result applies A first, then B.
	static BoneCoords Compose(const BoneCoords& b, const BoneCoords& a)
	{
		BoneCoords r;
		r.XAxis = b.ApplyVector(a.XAxis);
		r.YAxis = b.ApplyVector(a.YAxis);
		r.ZAxis = b.ApplyVector(a.ZAxis);
		r.Origin = b.Apply(a.Origin);
		return r;
	}

	// Core.dll SlerpQuat: shortest path, then a one-step renormalization.
	// IDA Core.dll: ?SlerpQuat@@YA?AVFQuat@@ABV1@0M@Z [HP1 Core 0x1014F5A0]
	static quaternion SlerpQuat(const quaternion& a, const quaternion& b, float alpha)
	{
		float rawCos = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		float cosom = std::abs(rawCos);
		if (cosom >= 1.0f)
			return a;

		float omega = std::acos(cosom);
		float invSin = 1.0f / std::sin(omega);
		float scale0 = std::sin((1.0f - alpha) * omega) * invSin;
		float scale1 = std::sin(alpha * omega) * invSin;
		if (rawCos < 0.0f)
			scale1 = -scale1;

		quaternion r(scale0 * a.x + scale1 * b.x, scale0 * a.y + scale1 * b.y, scale0 * a.z + scale1 * b.z, scale0 * a.w + scale1 * b.w);
		float sq = r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w;
		if (std::abs(sq - 1.0f) > 0.00001f)
		{
			if (sq < 0.00001f)
			{
				r = quaternion(0.0f, 0.0f, 0.1f, 0.0f);
			}
			else
			{
				float s = 1.5f - sq * 0.5f;
				r = quaternion(r.x * s, r.y * s, r.z * s, r.w * s);
			}
		}
		return r;
	}

	static Place RefPlace(const RefSkeletonBone& bone)
	{
		Place p;
		p.Orientation = bone.Orientation;
		p.Position = bone.Position;
		return p;
	}

	// Key sampling from ApplyAnim. KeyTime[k] (k >= 1) is the delta from key k-1, key 0 is at time 0;
	// past the last key it interpolates towards key 0 at TrackTime.
	static Place SampleTrack(const AnimTrack& track, float time, float trackTime)
	{
		Place p;
		int numKeys = (int)track.KeyTime.size();
		if (numKeys <= 1 || track.KeyQuat.empty())
		{
			if (!track.KeyQuat.empty()) p.Orientation = track.KeyQuat[0];
			if (!track.KeyPos.empty()) p.Position = track.KeyPos[0];
			return p;
		}

		float prevTime = 0.0f;
		float nextTime = 0.0f;
		int next = 1;
		while (next < numKeys)
		{
			nextTime += track.KeyTime[next] * track.KeyTimeScale;
			if (nextTime > time)
				break;
			prevTime = nextTime;
			next++;
		}
		int prev = next - 1;

		auto keyQuat = [&](int k) { return track.KeyQuat[std::min(k, (int)track.KeyQuat.size() - 1)]; };
		auto keyPos = [&](int k) { return track.KeyPos.size() == 1 ? track.KeyPos[0] : track.KeyPos[std::min(k, (int)track.KeyPos.size() - 1)]; };

		if (prevTime != time && next >= numKeys)
		{
			next = 0;
			nextTime = trackTime;
			prev = numKeys - 1;
		}

		if (prevTime == time || prev == next)
		{
			p.Orientation = keyQuat(prev);
			if (!track.KeyPos.empty()) p.Position = keyPos(prev);
			return p;
		}

		float alpha = (time - prevTime) / (nextTime - prevTime);
		p.Orientation = SlerpQuat(keyQuat(prev), keyQuat(next), alpha);
		if (!track.KeyPos.empty())
			p.Position = mix(keyPos(prev), keyPos(next), alpha);
		return p;
	}

	// The per-actor CFSkelHeader from the original's GCache: last pose (for the TweenAlpha blend), the
	// mesh bone -> animation track map and the bAnimMove root motion bank.
	struct SkelCache
	{
		USkeletalMesh* Mesh = nullptr;
		UAnimation* LinkedAnim = nullptr; // +4
		float LastFrame = -1.0f;          // +8  AnimFrame of the last full evaluation
		float LastTween = -1.0f;          // +12 TweenAlpha of the last full evaluation
		NameString LastSequence;          // +16 AnimSequence of the last full evaluation
		bool HasPose = false;             // +20
		bool HasBox = false;
		BBox Box; // last render box, root bone space (CFSkelHeader+80)
		vec3 LastRootPos = vec3(0.0f);    // +108 root position at the last evaluation, mesh space
		float RootLastFrame = 0.0f;       // +120 AnimFrame the adjust bank was last paid out at
		vec3 RootMove = vec3(0.0f);       // +124 banked root movement, mesh space (GetRootMovement)
		vec3 RootAdjust = vec3(0.0f);     // +136 movement still owed (AdjustRootMovement), mesh space
		bool RootValid = false;           // +148 LastRootPos is set for this sequence
		Array<int> BoneMap;
		Array<Place> Places;
	};
	static std::unordered_map<UActor*, SkelCache> SkelCaches;

	static SkelCache& GetSkelCache(UActor* actor, USkeletalMesh* mesh)
	{
		SkelCache& cache = SkelCaches[actor];
		if (cache.Mesh != mesh)
		{
			cache = {};
			cache.Mesh = mesh;
			cache.BoneMap.resize(mesh->RefSkeleton.size(), -1);
			cache.Places.resize(mesh->RefSkeleton.size());
		}
		return cache;
	}

	static mat4 GetMeshToWorld(UActor* actor, USkeletalMesh* mesh);

	// The mesh root's place: its own track with the ancestors' keys folded in (the mesh root may be below
	// the animation's root). Inlined in ApplyAnim, twice (pose and bAnimMove's time-0 start position).
	static Place SampleRoot(AnimationData* data, AnimMove* move, int track, float time)
	{
		Place place = SampleTrack(move->AnimTracks[track], time, move->TrackTime);
		int child = track;
		int parent = data->RefBones[child].ParentIndex;
		while (parent != child && parent >= 0 && parent < (int)move->AnimTracks.size())
		{
			Place pp = SampleTrack(move->AnimTracks[parent], time, move->TrackTime);
			BoneCoords pc = ToCoords(pp);
			const quaternion& c = place.Orientation;
			const quaternion& q = pp.Orientation;
			// -(c * q): same rotation, the sign is what the original produces
			place.Orientation = quaternion(
				q.y * c.z - c.x * q.w - q.x * c.w - q.z * c.y,
				c.x * q.z - q.w * c.y - q.y * c.w - q.x * c.z,
				q.x * c.y - q.w * c.z - q.z * c.w - c.x * q.y,
				q.z * c.z + q.y * c.y + c.x * q.x - q.w * c.w);
			place.Position = pc.Apply(place.Position);
			child = parent;
			parent = data->RefBones[child].ParentIndex;
		}
		return place;
	}

	// bAnimMove (part of ApplyAnim): the root bone stays at its reference position and its movement since the
	// last evaluation is banked for GetRootMovement (AActor::Tick moves the actor by it). Movement the actor
	// couldn't make comes back through AdjustRootMovement and is paid out again, at most at the animation's own
	// speed: |KeyPos[last] - KeyPos[0]| of the root track per unit of AnimFrame.
	static void BankRootMotion(SkelCache& cache, UActor* actor, USkeletalMesh* mesh, AnimationData* data, AnimMove* move, int track, Place& place)
	{
		const AnimTrack& rootTrack = move->AnimTracks[track];
		if (!cache.RootValid)
		{
			// First evaluation of this sequence: start from key 0.
			cache.LastRootPos = SampleRoot(data, move, track, 0.0f).Position;
			cache.RootAdjust = vec3(0.0f);
			cache.RootValid = true;
		}

		vec3 delta = place.Position - cache.LastRootPos;
		cache.LastRootPos = place.Position;
		place.Position = mesh->RefSkeleton[0].Position;
		cache.RootMove += delta;

		if (cache.LastSequence == actor->AnimSequence() && actor->AnimFrame() != cache.RootLastFrame)
		{
			if (cache.RootAdjust != vec3(0.0f) && !rootTrack.KeyPos.empty())
			{
				vec3 cycle = rootTrack.KeyPos.back() - rootTrack.KeyPos.front();
				float frames = (actor->AnimFrame() == 0.0f ? 1.0f : actor->AnimFrame()) - cache.RootLastFrame;
				vec3 limit = vec3(std::abs(cycle.x), std::abs(cycle.y), std::abs(cycle.z)) * frames;
				vec3 step(
					std::clamp(cache.RootAdjust.x, -limit.x, limit.x),
					std::clamp(cache.RootAdjust.y, -limit.y, limit.y),
					std::clamp(cache.RootAdjust.z, -limit.z, limit.z));
				cache.RootMove += step;
				cache.RootAdjust -= step;
			}
			cache.RootLastFrame = actor->AnimFrame();
		}
	}

	// USkeletalMesh::ApplyAnim(Owner, Header, bRootOnly). header != nullptr means 'actor' is an aux channel
	// writing into its owner's pose. rootOnly (GetRootMovement) evaluates bone 0 only and doesn't record the
	// evaluation, so the next full one still runs. A full evaluation with nothing changed (sequence, frame,
	// TweenAlpha) keeps the pose, so the tween blend doesn't run twice for the same frame.
	// IDA Engine.dll: ?ApplyAnim@USkeletalMesh@@ABEXPAVAActor@@PAUCFSkelHeader@1@_N@Z [HP1 0x1041BA60] (RefPlace/SampleTrack are inlined in it)
	static void ApplyAnim(USkeletalMesh* mesh, UActor* actor, SkelCache* header, bool rootOnly = false)
	{
		SkelCache& cache = GetSkelCache(actor, mesh);
		bool isChannel = header != nullptr;
		if (!header)
			header = &cache;

		if (cache.LastSequence != actor->AnimSequence() && !rootOnly)
		{
			cache.RootValid = false;
			cache.RootLastFrame = 0.0f;
		}

		bool unchanged = !isChannel && cache.LastSequence == actor->AnimSequence() && cache.LastFrame == actor->AnimFrame() && cache.LastTween == TweenAlpha(actor);
		if (!unchanged)
		{
			size_t numBones = rootOnly ? std::min<size_t>(1, mesh->RefSkeleton.size()) : mesh->RefSkeleton.size();

			if (!actor->SkelAnim() && mesh->DefaultAnimation)
				actor->SkelAnim() = mesh->DefaultAnimation;

			UAnimation* anim = actor->SkelAnim();
			AnimationData* data = anim ? GetAnimationData(anim) : nullptr;
			MeshAnimSeq* seq = data ? data->GetSequence(actor->AnimSequence()) : nullptr;
			AnimMove* move = data ? data->GetMove(actor->AnimSequence()) : nullptr;

			if (seq && move)
			{
				if (anim != cache.LinkedAnim)
				{
					int animBone = AnimBone(actor);
					for (size_t i = 0; i < mesh->RefSkeleton.size(); i++)
					{
						cache.BoneMap[i] = -1;
						if (isChannel && ((int)i < animBone || (int)i > animBone + (int)mesh->RefSkeleton[animBone].NumChildren))
							continue;
						for (size_t j = 0; j < move->AnimTracks.size() && j < data->RefBones.size(); j++)
						{
							if (mesh->RefSkeleton[i].Name == data->RefBones[j].Name)
							{
								cache.BoneMap[i] = (int)j;
								break;
							}
						}
					}
					cache.LinkedAnim = anim;
				}

				if (!move->AnimTracks.empty())
				{
					float time = std::min(actor->AnimFrame(), 1.0f) * move->TrackTime;
					float tween = (cache.HasPose && actor->TweenRate() != 0.0f) ? 1.0f - TweenAlpha(actor) : 0.0f;

					for (size_t i = 0; i < numBones; i++)
					{
						int track = cache.BoneMap[i];
						Place place;
						if (track < 0)
						{
							if (isChannel)
								continue;
							place = RefPlace(mesh->RefSkeleton[i]);
						}
						else if (i == 0)
						{
							place = SampleRoot(data, move, track, time);
							if (bAnimMove(actor))
								BankRootMotion(cache, actor, mesh, data, move, track, place);
						}
						else
						{
							place = SampleTrack(move->AnimTracks[track], time, move->TrackTime);
						}

						if (tween != 0.0f)
						{
							const Place& old = cache.Places[i];
							place.Orientation = SlerpQuat(place.Orientation, old.Orientation, tween);
							place.Position = place.Position * (1.0f - tween) + old.Position * tween;
						}

						cache.Places[i] = place;
						if (header != &cache)
							header->Places[i] = place;
					}
				}
			}
			else if (!isChannel)
			{
				for (size_t i = 0; i < numBones; i++)
					cache.Places[i] = RefPlace(mesh->RefSkeleton[i]);
				if (!rootOnly)
					cache.LinkedAnim = nullptr;
			}
		}

		if (rootOnly)
			return;

		cache.HasPose = true;
		cache.LastSequence = actor->AnimSequence();
		cache.LastFrame = actor->AnimFrame();
		cache.LastTween = TweenAlpha(actor);

		// Aux channels override their bone subtrees, in AuxAnims order. Finished transient channels are
		// destroyed here (this is where the original cleans them up).
		auto aux = AuxAnims(actor);
		for (size_t i = 0; i < aux.size(); i++)
		{
			UActor* ch = aux[i];
			if (!ch)
				continue;
			ApplyAnim(mesh, ch, header);
			if (bAnimTransient(ch) && !ch->bAnimLoop() && ch->AnimFrame() >= ch->AnimLast())
			{
				SkelCaches.erase(ch);
				ch->Destroy();
				aux.Array->Remove(i, 1);
				i--;
			}
		}
	}

	// The banked root movement, turned into world space by the mesh coords (rotation, scale, Y mirror), then cleared.
	// IDA Engine.dll: ?GetRootMovement@USkeletalMesh@@UAE?AVFVector@@PAVAActor@@@Z [HP1 0x1041EE20]
	static vec3 GetRootMovement(UActor* actor, USkeletalMesh* mesh)
	{
		ApplyAnim(mesh, actor, nullptr, true);
		SkelCache& cache = GetSkelCache(actor, mesh);
		vec3 move = (GetMeshToWorld(actor, mesh) * vec4(cache.RootMove, 0.0f)).xyz();
		cache.RootMove = vec3(0.0f);
		return move;
	}

	// World space movement the actor didn't make goes back into the mesh space adjust bank (dot products with
	// the mesh axes: the transposed mesh coords).
	// IDA Engine.dll: ?AdjustRootMovement@USkeletalMesh@@UAEXPAVAActor@@ABVFVector@@@Z [HP1 0x1041EFF0]
	static void AdjustRootMovement(UActor* actor, USkeletalMesh* mesh, const vec3& delta)
	{
		if (delta == vec3(0.0f))
			return;
		SkelCache& cache = GetSkelCache(actor, mesh);
		mat4 m = GetMeshToWorld(actor, mesh);
		vec3 x = (m * vec4(1.0f, 0.0f, 0.0f, 0.0f)).xyz();
		vec3 y = (m * vec4(0.0f, 1.0f, 0.0f, 0.0f)).xyz();
		vec3 z = (m * vec4(0.0f, 0.0f, 1.0f, 0.0f)).xyz();
		cache.RootAdjust += vec3(dot(delta, x), dot(delta, y), dot(delta, z));
	}

	// The end of AActor::Tick: a bAnimMove actor with authority moves by its animation's root movement
	// (ULevel::MoveActor), slides once along whatever it hits, and hands back what it couldn't make.
	// IDA Engine.dll: ?Tick@AActor@@UAEHMW4ELevelTick@@@Z [HP1 0x103B3840] (the bAnimMove block at its end)
	void TickRootMotion(UActor* actor)
	{
		if (!bAnimMove(actor) || actor->Role() != ROLE_Authority || actor->bDeleteMe())
			return;
		USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(actor->Mesh());
		if (!mesh || mesh->RefSkeleton.empty())
			return;

		vec3 delta = GetRootMovement(actor, mesh);
		if (delta == vec3(0.0f))
			return;

		CollisionHit hit = actor->TryMove(delta);
		vec3 rest = delta * (1.0f - hit.Fraction);
		if (hit.Fraction < 1.0f)
		{
			vec3 slide = rest - hit.Normal * dot(rest, hit.Normal);
			CollisionHit hit2 = actor->TryMove(slide);
			rest -= slide * hit2.Fraction;
		}
		AdjustRootMovement(actor, mesh, rest);
	}

	// USkeletalMesh::GetMeshCoords as a matrix: mesh space -> world.
	// IDA Engine.dll: ?GetMeshCoords@USkeletalMesh@@ABE?AVFCoords@@PBVAActor@@@Z [HP1 0x1041AEF0]
	// A pawn's weapon is drawn in the pawn's weapon frame: Render.dll passes that frame as the coords (with the
	// weapon's Location added to its origin, which cancels the Location in the mesh coords), zeroes the weapon's
	// Rotation and swaps ThirdPersonScale into DrawScale for the draw.
	static struct { UActor* Actor = nullptr; mat4 Frame; float DrawScale = 1.0f; } WeaponDraw;

	void BeginWeaponDraw(UActor* weapon, const mat4& frameToWorld, float drawScale)
	{
		WeaponDraw.Actor = weapon;
		WeaponDraw.Frame = frameToWorld;
		WeaponDraw.DrawScale = drawScale;
	}

	void EndWeaponDraw()
	{
		WeaponDraw.Actor = nullptr;
	}

	static mat4 GetMeshToWorld(UActor* actor, USkeletalMesh* mesh)
	{
		bool weaponDraw = actor == WeaponDraw.Actor;
		float drawScale = weaponDraw ? WeaponDraw.DrawScale : actor->DrawScale();
		float wide = Wideness(actor) / 128.0f;
		vec3 scale(mesh->Scale.x * drawScale * wide, -mesh->Scale.y * drawScale * wide, mesh->Scale.z * drawScale);

		vec3 adjust(0.0f);
		if (bAlignBottom(actor) && actor->bCollideWorld() && actor->Physics() != 0 && CollideType(actor) != 3)
			adjust.z = (mesh->Origin.z - mesh->BoundingBox.min.z) * mesh->Scale.z * drawScale - (actor->CollisionHeight() + 2.5f);

		mat4 placement = weaponDraw ? WeaponDraw.Frame : mat4::translate(actor->Location()) * Coords::Rotation(actor->Rotation()).ToMatrix();
		return placement * mat4::translate(actor->PrePivot() + adjust) * Coords::Rotation(mesh->RotOrigin).ToMatrix() *
			mat4::scale(scale) * mat4::translate(-mesh->Origin);
	}

	// GetFrame's bone coords (mesh space, before GetMeshCoords) from the pose ApplyAnim left in the cache:
	// B[0] = FCoords(Place[0]), B[i] = B[parent] composed with FCoords(Place[i]).
	static void ComputeBones(UActor* animator, USkeletalMesh* mesh, Array<BoneCoords>& bones)
	{
		SkelCache& cache = GetSkelCache(animator, mesh);
		size_t numBones = mesh->RefSkeleton.size();
		bones.resize(numBones);
		bones[0] = ToCoords(cache.Places[0]);
		for (size_t i = 1; i < numBones; i++)
		{
			uint32_t parent = mesh->RefSkeleton[i].ParentIndex;
			if (parent >= i)
				parent = 0;
			bones[i] = Compose(bones[parent], ToCoords(cache.Places[i]));
		}
	}

	static BoneCoords ToWorld(const mat4& meshToWorld, const BoneCoords& b)
	{
		BoneCoords r;
		r.Origin = (meshToWorld * vec4(b.Origin, 1.0f)).xyz();
		r.XAxis = (meshToWorld * vec4(b.XAxis, 0.0f)).xyz();
		r.YAxis = (meshToWorld * vec4(b.YAxis, 0.0f)).xyz();
		r.ZAxis = (meshToWorld * vec4(b.ZAxis, 0.0f)).xyz();
		return r;
	}

	// USkeletalMesh::GetFrame: world-space vertex positions, indexed like Points.
	// IDA Engine.dll: ?GetFrame@USkeletalMesh@@UAEXPAVFVector@@HVFCoords@@PAVAActor@@AAH@Z [HP1 0x1041DF50]
	// IDA Engine.dll: ?GetFrame@USkeletalMesh@@UAEXPAVFVector@@HVFCoords@@PAVAActor@@@Z [HP1 0x10404470] (wrapper)
	static bool GetFrame(UActor* actor, USkeletalMesh* mesh, Array<vec3>& outVerts)
	{
		if (mesh->RefSkeleton.empty() || mesh->BoneWeightIndices.empty())
			return false;

		UActor* animator = actor;
		if (actor->bAnimByOwner() && actor->Owner())
			animator = actor->Owner();

		ApplyAnim(mesh, animator, nullptr);
		size_t numBones = mesh->RefSkeleton.size();
		Array<BoneCoords> bones;
		ComputeBones(animator, mesh, bones);

		size_t numVerts = mesh->Points.size();
		outVerts.clear();
		outVerts.resize(numVerts, vec3(0.0f));

		if (mesh->BoneWeightIndices.size() == 1)
		{
			size_t count = std::min<size_t>(mesh->BoneWeightIndices[0].Number, std::min(numVerts, mesh->LocalPoints.size()));
			for (size_t k = 0; k < count; k++)
				outVerts[k] = bones[0].Apply(mesh->LocalPoints[k]);
		}
		else
		{
			for (size_t n = 0; n < mesh->BoneWeightIndices.size() && n < numBones; n++)
			{
				const BoneWeightIndex& index = mesh->BoneWeightIndices[n];
				for (size_t k = index.WeightIndex; k < (size_t)index.WeightIndex + index.Number; k++)
				{
					if (k >= mesh->BoneWeights.size() || k >= mesh->LocalPoints.size())
						break;
					uint32_t point = mesh->BoneWeights[k].PointIndex;
					if (point >= numVerts)
						break;
					float weight = mesh->BoneWeights[k].BoneWeight * (1.0f / 65535.0f);
					outVerts[point] += bones[n].Apply(mesh->LocalPoints[k]) * weight;
				}
			}
		}

		mat4 meshToWorld = GetMeshToWorld(actor, mesh);
		for (vec3& v : outVerts)
			v = (meshToWorld * vec4(v, 1.0f)).xyz();
		return true;
	}

	// sub_1041B500: animation bounding box in root bone space. Mesh.BoundingBoxes is indexed like the
	// animation's AnimSeqs; while tweening it is unioned with the previous box, and with the aux channels' boxes.
	// IDA Engine.dll: not exported: sub_1041B500 [HP1 0x1041B500]; find it as the only callee of USkeletalMesh::GetRenderBoundingBox that also calls itself (aux channels)
	static bool GetAnimBox(UActor* actor, USkeletalMesh* mesh, BBox& outBox)
	{
		bool valid = false;
		BBox box;
		UAnimation* anim = actor->SkelAnim();
		AnimationData* data = anim ? GetAnimationData(anim) : nullptr;
		if (data && !actor->AnimSequence().IsNone())
		{
			for (size_t i = 0; i < data->AnimSeqs.size(); i++)
			{
				if (data->AnimSeqs[i].Name == actor->AnimSequence())
				{
					if (i < mesh->BoundingBoxes.size())
					{
						box = mesh->BoundingBoxes[i];
						valid = true;
					}
					break;
				}
			}
		}

		auto unite = [&](const BBox& b) {
			if (!valid) { box = b; valid = true; return; }
			box.min = vec3(std::min(box.min.x, b.min.x), std::min(box.min.y, b.min.y), std::min(box.min.z, b.min.z));
			box.max = vec3(std::max(box.max.x, b.max.x), std::max(box.max.y, b.max.y), std::max(box.max.z, b.max.z));
		};

		SkelCache& cache = GetSkelCache(actor, mesh);
		if (TweenAlpha(actor) != 1.0f && cache.HasBox)
			unite(cache.Box);
		cache.Box = box;
		cache.HasBox = valid;

		for (UActor* ch : AuxAnims(actor))
		{
			BBox chBox;
			if (ch && GetAnimBox(ch, mesh, chBox))
				unite(chBox);
		}

		outBox = box;
		return valid;
	}

	// USkeletalMesh::GetRenderBoundingBox, as a world space AABB for culling/BSP placement.
	// IDA Engine.dll: ?GetRenderBoundingBox@USkeletalMesh@@UAE?AVFCoords@@PBVAActor@@H@Z [HP1 0x1041B2F0]
	bool GetSkeletalFrameVerts(UActor* actor, USkeletalMesh* mesh, Array<vec3>& outVerts)
	{
		return GetFrame(actor, mesh, outVerts);
	}

	BBox GetRenderBoundingBox(UActor* actor, USkeletalMesh* mesh)
	{
		mat4 meshToWorld = GetMeshToWorld(actor, mesh);

		BBox box;
		BoneCoords root;
		if (GetAnimBox(actor, mesh, box) && !mesh->RefSkeleton.empty())
		{
			auto it = SkelCaches.find(actor);
			if (it != SkelCaches.end() && it->second.HasPose && it->second.Mesh == mesh)
				root = ToCoords(it->second.Places[0]);
			else
				root = ToCoords(RefPlace(mesh->RefSkeleton[0]));
		}
		else
		{
			box = mesh->BoundingBox;
		}

		BBox result;
		for (int i = 0; i < 8; i++)
		{
			vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z);
			vec3 p = (meshToWorld * vec4(root.Apply(corner), 1.0f)).xyz();
			if (i == 0)
			{
				result.min = p;
				result.max = p;
			}
			else
			{
				result.min = vec3(std::min(result.min.x, p.x), std::min(result.min.y, p.y), std::min(result.min.z, p.z));
				result.max = vec3(std::max(result.max.x, p.x), std::max(result.max.y, p.y), std::max(result.max.z, p.z));
			}
		}
		return result;
	}

	// World coords of a bone: origin = the bone's world position, axes = its orientation including the mesh scale
	// and GetMeshCoords' Y mirror. An invalid bone gives the mesh coords. Only the root is posed for bone 0.
	// IDA Engine.dll: ?GetBoneCoords@USkeletalMesh@@UBE?AVFCoords@@PAVAActor@@H@Z [HP1 0x1041F3C0]
	bool GetBoneCoords(UActor* actor, USkeletalMesh* mesh, int bone, vec3& origin, vec3& x, vec3& y, vec3& z)
	{
		if (mesh->RefSkeleton.empty())
			return false;
		ApplyAnim(mesh, actor, nullptr, bone == 0);
		SkelCache& cache = GetSkelCache(actor, mesh);
		mat4 meshToWorld = GetMeshToWorld(actor, mesh);

		BoneCoords c;
		if (bone >= 0 && bone < (int)mesh->RefSkeleton.size() && bone < (int)cache.Places.size())
		{
			c = ToCoords(cache.Places[bone]);
			while (bone != 0)
			{
				uint32_t parent = mesh->RefSkeleton[bone].ParentIndex;
				bone = parent < (uint32_t)bone ? (int)parent : 0;
				c = Compose(ToCoords(cache.Places[bone]), c);
			}
		}
		c = ToWorld(meshToWorld, c);
		origin = c.Origin;
		x = c.XAxis;
		y = c.YAxis;
		z = c.ZAxis;
		return true;
	}

	// The weapon frame GetFrame leaves on the mesh for Render.dll (mesh->WeaponBoneIndex >= 0): WeaponAdjust in the
	// weapon bone's space, orthonormalized, Y negated (undoes GetMeshCoords' mirror). World space here; the original
	// works in camera space, which is the same rigid transform away.
	// IDA Engine.dll: ?GetFrame@USkeletalMesh@@UAEXPAVFVector@@HVFCoords@@PAVAActor@@AAH@Z [HP1 0x1041DF50] (the WeaponBoneIndex block at its end)
	bool SkeletalWeaponFrame(UActor* actor, USkeletalMesh* mesh, vec3& origin, vec3& x, vec3& y, vec3& z)
	{
		int weaponBone = (int)mesh->WeaponBoneIndex;
		if (weaponBone < 0 || weaponBone >= (int)mesh->RefSkeleton.size() || mesh->BoneWeightIndices.empty())
			return false;

		UActor* animator = actor;
		if (actor->bAnimByOwner() && actor->Owner())
			animator = actor->Owner();
		ApplyAnim(mesh, animator, nullptr);
		Array<BoneCoords> bones;
		ComputeBones(animator, mesh, bones);
		BoneCoords bone = ToWorld(GetMeshToWorld(actor, mesh), bones[weaponBone]);

		BoneCoords adjust;
		adjust.Origin = mesh->WeaponAdjust.Origin;
		adjust.XAxis = mesh->WeaponAdjust.XAxis;
		adjust.YAxis = mesh->WeaponAdjust.YAxis;
		adjust.ZAxis = mesh->WeaponAdjust.ZAxis;
		BoneCoords w = Compose(bone, adjust);

		auto safeNormal = [](const vec3& v) { float sq = dot(v, v); return sq >= 1e-8f ? v * (1.0f / std::sqrt(sq)) : vec3(0.0f); };
		x = safeNormal(w.XAxis);
		y = safeNormal(cross(x, w.ZAxis));
		z = cross(x, y);
		y = -y;
		origin = w.Origin;
		return true;
	}

	// IDA Engine.dll: none: drawing is our own (HP1 renders through Render.dll/D3DDrv); pose and skinning come from GetFrame above
	bool DrawSkeletalMesh(VisibleFrame* frame, UActor* actor, UActor* lightLocationActor, USkeletalMesh* mesh, bool translucentPass)
	{
		static Array<vec3> verts;
		static Array<vec3> normals;
		if (!GetFrame(actor, mesh, verts))
			return false;

		bool useExtWedges = !mesh->ExtWedges.empty();
		auto wedgeVertex = [&](int w) -> int { return useExtWedges ? mesh->ExtWedges[w].Vertex : mesh->Wedges[w].Vertex; };
		size_t numWedges = useExtWedges ? mesh->ExtWedges.size() : mesh->Wedges.size();

		// Smooth normals from the skinned triangles. The Y mirror in GetMeshCoords flips the winding,
		// hence the cross product order.
		normals.clear();
		normals.resize(verts.size(), vec3(0.0f));
		for (const MeshFace& face : mesh->Faces)
		{
			if (face.Indices[0] >= numWedges || face.Indices[1] >= numWedges || face.Indices[2] >= numWedges)
				continue;
			int i0 = wedgeVertex(face.Indices[0]), i1 = wedgeVertex(face.Indices[1]), i2 = wedgeVertex(face.Indices[2]);
			if (i0 >= (int)verts.size() || i1 >= (int)verts.size() || i2 >= (int)verts.size())
				continue;
			vec3 n = cross(verts[i2] - verts[i0], verts[i1] - verts[i0]);
			normals[i0] += n;
			normals[i1] += n;
			normals[i2] += n;
		}
		for (vec3& n : normals)
		{
			float len = length(n);
			n = len > 0.0f ? n / len : vec3(0.0f, 0.0f, 1.0f);
		}

		auto lightsys = &engine->Level->Light;

		uint32_t polyFlags = 0;
		switch (actor->Style())
		{
		default: break;
		case STY_Masked: polyFlags |= PF_Masked; break;
		case STY_Translucent: polyFlags |= PF_Translucent; break;
		case STY_Modulated: polyFlags |= PF_Modulated; break;
		}
		if (actor->bNoSmooth()) polyFlags |= PF_NoSmooth;
		if (actor->bSelected()) polyFlags |= PF_Selected;
		if (actor->bMeshEnviroMap()) polyFlags |= PF_Environment;
		if (actor->bMeshCurvy()) polyFlags |= PF_Flat;
		if (actor->bUnlit() || actor->Region().ZoneNumber == 0) polyFlags |= PF_Unlit;

		UZoneInfo* zoneActor = engine->GetZoneActor(actor->Region().ZoneNumber);
		VertexLight vertexLight;
		lightsys->InitVertexLight(vertexLight, lightLocationActor, zoneActor);

		bool needTranslucentPass = false;
		GouraudVertex vertices[3];
		for (const MeshFace& face : mesh->Faces)
		{
			if (face.MaterialIndex >= mesh->Materials.size())
				continue;
			const MeshMaterial& material = mesh->Materials[face.MaterialIndex];
			if (material.PolyFlags & PF_Invisible)
				continue;

			uint32_t renderflags = material.PolyFlags | polyFlags;
			UTexture* tex = (renderflags & PF_Environment) ? engine->render->Mesh.envmap : (material.TextureIndex < engine->render->Mesh.textures.size() ? engine->render->Mesh.textures[material.TextureIndex] : nullptr);
			if (!tex)
				continue;

			bool isTranslucent = (renderflags & (PF_Translucent | PF_Modulated | PF_Highlighted)) != 0;
			if (isTranslucent != translucentPass)
			{
				needTranslucentPass |= isTranslucent;
				continue;
			}

			engine->render->UpdateTexture(tex);
			TextureInfo texinfo;
			engine->render->UpdateTextureInfo(texinfo, tex);

			float width = texinfo.Texture ? (float)texinfo.Texture->UsedMipmaps.front().Width : 256.0f;
			float height = texinfo.Texture ? (float)texinfo.Texture->UsedMipmaps.front().Height : 256.0f;

			bool valid = true;
			vec3 faceNormals[3];
			for (int i = 0; i < 3; i++)
			{
				int w = face.Indices[i];
				if (w >= (int)numWedges) { valid = false; break; }
				int v = wedgeVertex(w);
				if (v >= (int)verts.size()) { valid = false; break; }

				vertices[i].Point = verts[v];
				if (useExtWedges)
					vertices[i].UV = { mesh->ExtWedges[w].U * width, mesh->ExtWedges[w].V * height };
				else
					vertices[i].UV = { mesh->Wedges[w].U * width / 255.0f, mesh->Wedges[w].V * height / 255.0f };
				faceNormals[i] = normals[v];
			}
			if (!valid)
				continue;

			if (renderflags & PF_Environment)
			{
				mat3 rotmat = mat3(frame->Frame.WorldToView * frame->Frame.ObjectToWorld);
				for (int i = 0; i < 3; i++)
				{
					vec3 v = normalize(vertices[i].Point);
					vec3 p = rotmat * reflect(v, faceNormals[i]);
					vertices[i].UV = { (p.x + 1.0f) * 128.0f * width / 255.0f, (p.y + 1.0f) * 128.0f * height / 255.0f };
				}
			}

			for (int i = 0; i < 3; i++)
			{
				vertices[i].Light = vertexLight.GetVertexLight(vertices[i].Point, faceNormals[i], !!(renderflags & PF_Unlit), !!(renderflags & PF_TwoSided));
				vertices[i].Fog = vertexLight.GetVertexFog(vertices[i].Point);
			}

			frame->Device->DrawGouraudPolygon(&frame->Frame, texinfo, vertices, 3, renderflags | PF_RenderFog);
		}
		return needTranslucentPass;
	}
}
