#include "Precomp.h"
#include "HP1.h"
#include "Anim/HP1Animation.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Mesh/UAnimation.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Package/ObjectStream.h"
#include <unordered_map>

namespace HP1
{
	// Side table instead of new members on upstream's UAnimation. Animations are never freed while a
	// game is running, so raw pointers as keys are fine.
	static std::unordered_map<UAnimation*, std::unique_ptr<AnimationData>> AnimationTable;

	AnimationData* GetAnimationData(UAnimation* anim)
	{
		anim->LoadNow(); // objects are delay-loaded; make sure Load (and so LoadAnimation) ran
		auto it = AnimationTable.find(anim);
		return it != AnimationTable.end() ? it->second.get() : nullptr;
	}

	// IDA Engine.dll: ?GetAnimSeq@UAnimation@@UAEPAUFMeshAnimSeq@@VFName@@@Z [HP1 0x10405580]
	MeshAnimSeq* AnimationData::GetSequence(const NameString& name)
	{
		for (MeshAnimSeq& seq : AnimSeqs)
			if (seq.Name == name)
				return &seq;
		return nullptr;
	}

	// IDA Engine.dll: ?GetMovement@UAnimation@@UAEPAUMotionChunk@@VFName@@@Z [HP1 0x10405620]
	AnimMove* AnimationData::GetMove(const NameString& name)
	{
		for (size_t i = 0; i < AnimSeqs.size() && i < Moves.size(); i++)
			if (AnimSeqs[i].Name == name)
				return &Moves[i];
		return nullptr;
	}

	MeshAnimSeq* GetAnimSeq(UActor* actor, const NameString& name)
	{
		UMesh* mesh = actor->Mesh();
		if (!mesh)
			return nullptr;

		mesh->LoadNow();
		if (auto skel = UObject::TryCast<USkeletalMesh>(mesh))
		{
			UAnimation* anim = actor->SkelAnim() ? actor->SkelAnim() : skel->DefaultAnimation;
			AnimationData* data = anim ? GetAnimationData(anim) : nullptr;
			return data ? data->GetSequence(name) : nullptr;
		}

		for (MeshAnimSeq& seq : mesh->AnimSeqs)
			if (seq.Name == name)
				return &seq;
		return nullptr;
	}

	int BoneIndex(USkeletalMesh* mesh, const NameString& name)
	{
		mesh->LoadNow();
		for (size_t i = 0; i < mesh->RefSkeleton.size(); i++)
			if (mesh->RefSkeleton[i].Name == name)
				return (int)i;
		return -1;
	}

	// UAnimation::Serialize in HP1 Engine.dll:
	//   RefBones  array of AnimBone  { name, flags, parent index }
	//   TArray<MotionChunk>  Moves      { FVector RootSpeed3D; FLOAT TrackTime; INT StartBone; DWORD Flags;
	//     AnimTrack                  { Flags, KeyQuat count, KeyPos count, KeyTime count, KeyPosScale, KeyTimeScale }
	//                                  (only the key counts are stored per track; the keys are in the pools below)
	//   TArray<FMeshAnimSeq> AnimSeqs   (stock layout)
	//   TArray<FAnimVec>     KeyQuats, KeyPoses   (bulk, 6 bytes each)
	//   TArray<BYTE>         KeyTimes             (bulk)
	// After loading, each track's keys are consecutive slices of the three pools, in Moves/AnimTracks order.
	// FAnimVec (3x int16): FAnimVec::Quat: xyz = sin(v * (pi/2) / 32767), w = sqrt(1 - x^2 - y^2 - z^2)
	//                      FAnimVec::Vector(Scale): v * Scale / 32767
	// IDA Engine.dll: ?Serialize@UAnimation@@UAEXAAVFArchive@@@Z [HP1 0x1041A2F0]
	void LoadAnimation(UAnimation* anim, ObjectStream* stream)
	{
		auto data = std::make_unique<AnimationData>();

		int NumRefBones = stream->ReadIndex();
		for (int i = 0; i < NumRefBones; i++)
		{
			AnimBone bone;
			bone.Name = stream->ReadName();
			bone.Flags = stream->ReadUInt32();
			bone.ParentIndex = stream->ReadInt32();
			data->RefBones.push_back(bone);
		}

		struct TrackCounts { int Quat, Pos, Time; };
		Array<TrackCounts> counts;

		int NumMoves = stream->ReadIndex();
		for (int i = 0; i < NumMoves; i++)
		{
			AnimMove move;
			move.RootSpeed3D.x = stream->ReadFloat();
			move.RootSpeed3D.y = stream->ReadFloat();
			move.RootSpeed3D.z = stream->ReadFloat();
			move.TrackTime = stream->ReadFloat();
			move.StartBone = stream->ReadInt32();
			move.Flags = stream->ReadUInt32();

			int NumBoneIndices = stream->ReadIndex();
			for (int j = 0; j < NumBoneIndices; j++)
				move.BoneIndices.push_back(stream->ReadInt32());

			int NumAnimTracks = stream->ReadIndex();
			for (int j = 0; j < NumAnimTracks; j++)
			{
				AnimTrack track;
				track.Flags = stream->ReadUInt32();
				TrackCounts c;
				c.Quat = stream->ReadIndex();
				c.Pos = stream->ReadIndex();
				c.Time = stream->ReadIndex();
				track.KeyPosScale = stream->ReadFloat();
				track.KeyTimeScale = stream->ReadFloat();
				counts.push_back(c);
				move.AnimTracks.push_back(std::move(track));
			}
			data->Moves.push_back(std::move(move));
		}

		int NumAnimSeq = stream->ReadIndex();
		for (int i = 0; i < NumAnimSeq; i++)
		{
			MeshAnimSeq seq;
			seq.Name = stream->ReadName();
			seq.Group = stream->ReadName();
			seq.StartFrame = stream->ReadInt32();
			seq.NumFrames = stream->ReadInt32();
			int NumNotifys = stream->ReadIndex();
			for (int j = 0; j < NumNotifys; j++)
			{
				MeshAnimNotify notify;
				notify.Time = stream->ReadFloat();
				notify.Function = stream->ReadName();
				seq.Notifys.push_back(notify);
			}
			seq.Rate = stream->ReadFloat();
			data->AnimSeqs.push_back(seq);
		}

		auto readPool = [&](int elementSize) {
			int count = stream->ReadIndex();
			Array<uint8_t> pool((size_t)count * elementSize);
			if (!pool.empty())
				stream->ReadBytes(pool.data(), (uint32_t)pool.size());
			return pool;
		};
		Array<uint8_t> keyQuats = readPool(6);
		Array<uint8_t> keyPoses = readPool(6);
		Array<uint8_t> keyTimes = readPool(1);

		auto animVec = [](const Array<uint8_t>& pool, size_t index) {
			int16_t v[3];
			memcpy(v, pool.data() + index * 6, 6);
			return vec3((float)v[0], (float)v[1], (float)v[2]);
		};

		const float quatScale = 3.14159265f * 0.5f / 32767.0f;
		size_t quatPos = 0, posPos = 0, timePos = 0, trackIndex = 0;
		for (AnimMove& move : data->Moves)
		{
			for (AnimTrack& track : move.AnimTracks)
			{
				const TrackCounts& c = counts[trackIndex++];
				if (quatPos + c.Quat > keyQuats.size() / 6 || posPos + c.Pos > keyPoses.size() / 6 || timePos + c.Time > keyTimes.size())
					Exception::Throw("HP1 UAnimation key pool overflow in " + anim->Name.ToString());

				for (int k = 0; k < c.Quat; k++)
				{
					vec3 v = animVec(keyQuats, quatPos++);
					quaternion q;
					q.x = std::sin(v.x * quatScale);
					q.y = std::sin(v.y * quatScale);
					q.z = std::sin(v.z * quatScale);
					q.w = std::sqrt(std::max(1.0f - q.x * q.x - q.y * q.y - q.z * q.z, 0.0f));
					track.KeyQuat.push_back(q);
				}
				for (int k = 0; k < c.Pos; k++)
					track.KeyPos.push_back(animVec(keyPoses, posPos++) * (track.KeyPosScale / 32767.0f));
				for (int k = 0; k < c.Time; k++)
					track.KeyTime.push_back((float)keyTimes[timePos++]);
			}
		}

		AnimationTable[anim] = std::move(data);
	}
}
