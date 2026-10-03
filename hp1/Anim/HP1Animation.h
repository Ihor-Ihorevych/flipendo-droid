#pragma once

#include "Math/vec.h"
#include "Math/quaternion.h"
#include "Packages/Engine/Resources/Mesh/UMesh.h"

class UActor;
class UAnimation;
class USkeletalMesh;

// HP1 skeletal animation data (KnowWonder's UAnimation). See docs/re/animation.md.
namespace HP1
{
	struct AnimTrack
	{
		uint32_t Flags = 0;
		Array<quaternion> KeyQuat;
		Array<vec3> KeyPos;
		Array<float> KeyTime; // raw byte values; multiply by KeyTimeScale
		float KeyPosScale = 1.0f;
		float KeyTimeScale = 1.0f;
	};

	// MotionChunk in Engine.dll's exports
	struct AnimMove
	{
		vec3 RootSpeed3D = vec3(0.0f);
		float TrackTime = 0.0f;
		int StartBone = 0;
		uint32_t Flags = 0;
		Array<int> BoneIndices;
		Array<AnimTrack> AnimTracks;
	};

	struct AnimBone
	{
		NameString Name;
		uint32_t Flags = 0;
		int ParentIndex = 0;
	};

	struct AnimationData
	{
		Array<AnimBone> RefBones;
		Array<AnimMove> Moves;        // parallel to AnimSeqs
		Array<MeshAnimSeq> AnimSeqs;

		MeshAnimSeq* GetSequence(const NameString& name);
		AnimMove* GetMove(const NameString& name);
	};

	// Data loaded for a UAnimation, or nullptr if it isn't an HP1 animation.
	AnimationData* GetAnimationData(UAnimation* anim);

	// AActor::GetAnim: the sequence for a name, looked up the way HP1 does it (SkelAnim, then the
	// skeletal mesh's DefaultAnimation, else the classic mesh). No fallback to the first sequence.
	MeshAnimSeq* GetAnimSeq(UActor* actor, const NameString& name);

	// USkeletalMesh::BoneIndex
	int BoneIndex(USkeletalMesh* mesh, const NameString& name);

	// USkeletalMesh::GetFrame: posed world-space vertices, indexed like the mesh's Points/Wedges.
	bool GetSkeletalFrameVerts(UActor* actor, USkeletalMesh* mesh, Array<vec3>& outVerts);
}
