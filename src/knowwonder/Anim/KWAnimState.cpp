#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "Anim/KWAnimation.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Mesh/UAnimation.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "VM/ScriptCall.h"
#include "VM/NativeFunc.h"
#include "VM/Frame.h"
#include "Utils/Logger.h"
#include "Engine.h"

// HP1 animation state, reimplemented from KnowWonder's Engine.dll (see docs/re/engine/animation.md):
//   AActor::PlayAnim(Sequence, bLoop, Rate, TweenTime, MinRate, Type, RootBone)
//   AActor::CreateAnimChannel, execIsAnimating, execFinishAnim, execTweenAnim, AActor::Tick (anim part)
//
// Differences from stock UE1 that matter:
// - Tweening is a separate blend weight (TweenAlpha 0->1 at TweenRate per second). AnimFrame always
//   starts at 0; there is no negative "tween frame".
// - RootBone='Move' sets bAnimMove (root motion). Any other RootBone plays the sequence on a transient
//   AnimChannel actor (in Owner.AuxAnims) that animates that bone's subtree.
// - Type=AT_Replace removes/stops existing aux channels; AT_Combine leaves them.

namespace KW
{
	static const NameString NAME_Move("Move");

	static bool IsAnimatingSelf(UActor* a)
	{
		return !a->AnimSequence().IsNone() && (a->AnimRate() != 0.0f || a->TweenRate() != 0.0f);
	}

	static UActor* FindChannel(UActor* actor, int boneIndex)
	{
		for (UActor* ch : AuxAnims(actor))
			if (ch && AnimBone(ch) == (uint8_t)boneIndex)
				return ch;
		return nullptr;
	}

	// Removes aux channels: transient ones are destroyed, persistent ones just stop.
	// boneIndex < 0 means all channels, otherwise only those in [boneIndex, boneIndex + NumChildren).
	static void ClearChannels(UActor* actor, USkeletalMesh* skel, int boneIndex, bool destroyPersistent)
	{
		auto aux = AuxAnims(actor);
		for (int i = (int)aux.size() - 1; i >= 0; i--)
		{
			UActor* ch = aux[i];
			if (!ch)
				continue;
			if (boneIndex >= 0)
			{
				int chBone = AnimBone(ch);
				if (chBone < boneIndex || chBone >= boneIndex + (int)skel->RefSkeleton[boneIndex].NumChildren)
					continue;
			}
			if (destroyPersistent || bAnimTransient(ch))
			{
				ch->Destroy();
				aux.Array->Remove(i, 1);
			}
			else
			{
				ch->AnimSequence() = {};
			}
		}
	}

	// IDA Engine.dll: ?CreateAnimChannel@AActor@@QAEPAV1@PAVUClass@@W4EAnimType@@VFName@@_N@Z [HP1 0x10408600]
	UActor* CreateAnimChannel(UActor* actor, UClass* newClass, EAnimType type, const NameString& rootBone, bool bTransient)
	{
		auto skel = UObject::TryCast<USkeletalMesh>(actor->Mesh());
		if (!skel || rootBone.IsNone() || !newClass)
			return nullptr;

		int boneIndex = BoneIndex(skel, rootBone);
		if (boneIndex < 0)
			return nullptr;

		for (UActor* ch : AuxAnims(actor))
			if (ch && AnimBone(ch) == (uint8_t)boneIndex && (bool)bAnimTransient(ch) == bTransient)
				return ch;

		UActor* ch = actor->Spawn(newClass, actor, {}, actor->Location(), actor->Rotation());
		if (!ch)
			return nullptr;

		ch->Mesh() = actor->Mesh();
		ch->SkelAnim() = actor->SkelAnim();
		AnimBone(ch) = (uint8_t)boneIndex;
		bAnimTransient(ch) = bTransient;

		auto aux = AuxAnims(actor);
		if (type == EAnimType::AT_Combine)
		{
			aux.Array->Insert(0, 1);
			aux[0] = ch;
		}
		else
		{
			ClearChannels(actor, skel, boneIndex, true);
			aux.push_back(ch);
		}
		return ch;
	}

	// IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
	bool PlayAnim(UActor* actor, NameString sequence, bool bLoop, float rate, float tweenTime, float minRate, EAnimType type, NameString rootBone)
	{
		if (!actor->Mesh())
		{
			LogMessage("PlayAnim: No mesh");
			return false;
		}

		auto skel = UObject::TryCast<USkeletalMesh>(actor->Mesh());

		if (rootBone == NAME_Move)
		{
			rootBone = {};
			bAnimMove(actor) = true;
		}
		else
		{
			bAnimMove(actor) = false;
		}

		if (skel && !rootBone.IsNone())
		{
			if (sequence.IsNone())
			{
				// PlayAnim('', ..., RootBone): stop the channels on that bone's subtree.
				int boneIndex = BoneIndex(skel, rootBone);
				if (boneIndex < 0)
					return false;
				ClearChannels(actor, skel, boneIndex, false);
				return true;
			}

			UClass* channelClass = engine->packages->FindClass("Engine.AnimChannel");
			if (UActor* ch = CreateAnimChannel(actor, channelClass, type, rootBone, true))
				return PlayAnim(ch, sequence, bLoop, rate, tweenTime, minRate, EAnimType::AT_Replace, {});
		}

		if (type == EAnimType::AT_Replace)
			ClearChannels(actor, skel, -1, false);

		MeshAnimSeq* seq = GetAnimSeq(actor, sequence);
		if (!seq && !sequence.IsNone())
		{
			LogMessage("PlayAnim: Sequence '" + sequence.ToString() + "' not found in Mesh '" + actor->Mesh()->Name.ToString() + "'");
			return false;
		}

		if (seq)
		{
			float framesPerSec = seq->Rate / (float)seq->NumFrames;

			// LoopAnim on the sequence already looping: just change the rate.
			if (actor->AnimSequence() == sequence && bLoop && actor->bAnimLoop() && IsAnimatingSelf(actor))
			{
				actor->AnimRate() = rate * framesPerSec;
				actor->bAnimFinished() = false;
				actor->AnimMinRate() = minRate != 0.0f ? framesPerSec * minRate : 0.0f;
				return true;
			}

			actor->AnimRate() = rate * framesPerSec;
			actor->AnimLast() = 1.0f - 1.0f / (float)seq->NumFrames;
			actor->AnimMinRate() = minRate != 0.0f ? framesPerSec * minRate : 0.0f;
			actor->bAnimNotify() = !seq->Notifys.empty();
			actor->bAnimFinished() = false;
			actor->bAnimLoop() = bLoop;
		}
		else
		{
			actor->AnimLast() = 0.0f;
			actor->bAnimLoop() = false;
		}

		actor->AnimSequence() = sequence;
		actor->bAnimFinished() = false;
		actor->AnimFrame() = 0.0f;
		TweenAlpha(actor) = 0.0f;
		if (actor->AnimLast() == 0.0f)
		{
			actor->AnimMinRate() = 0.0f;
			actor->AnimRate() = 0.0f;
			actor->bAnimNotify() = false;
		}

		if (tweenTime > 0.0f)
		{
			actor->TweenRate() = 1.0f / tweenTime;
		}
		else if (tweenTime == 0.0f)
		{
			actor->TweenRate() = 0.0f;
			TweenAlpha(actor) = 1.0f;
		}
		else
		{
			actor->TweenRate() = 2.0f; // default (TweenTime omitted = -1): 0.5 second blend
		}

		// Not done: the SimAnim replication packing (network only).
		return true;
	}

	// IDA Engine.dll: ?IsAnimating@AActor@@QBEHXZ [HP1 0x1031C3A0] (+ channel lookup in execIsAnimating)
	static bool IsAnimating(UActor* actor, const NameString& rootBone)
	{
		if (rootBone.IsNone())
			return IsAnimatingSelf(actor);

		auto skel = UObject::TryCast<USkeletalMesh>(actor->Mesh());
		if (!skel)
			return false;
		UActor* ch = FindChannel(actor, BoneIndex(skel, rootBone));
		return ch && IsAnimatingSelf(ch);
	}

	// IDA Engine.dll: ?execFinishAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10408250]
	// IDA Engine.dll: ?execPollFinishAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10408460]
	static void FinishAnim(UActor* actor, const NameString& rootBone)
	{
		UActor* target = actor;
		if (!rootBone.IsNone())
		{
			auto skel = UObject::TryCast<USkeletalMesh>(actor->Mesh());
			if (!skel)
				return;
			target = FindChannel(actor, BoneIndex(skel, rootBone));
			if (!target || target == actor)
				return;
		}

		if (target->bAnimLoop())
		{
			target->bAnimLoop() = false;
			target->bAnimFinished() = false;
		}

		// The original sets the latent action on the *target's* state frame, so FinishAnim(RootBone)
		// doesn't block the caller. AnimChannels have no state code, so for them this is a no-op.
		if (target == actor && IsAnimatingSelf(target) && target->AnimFrame() < target->AnimLast() && target->StateFrame)
			target->StateFrame->LatentState = LatentRunState::FinishAnim;
	}

	static void CallAnimEnd(UActor* a)
	{
		CallEvent(a, EventName::AnimEnd);
	}

	// IDA Engine.dll: ?Tick@AActor@@UAEHMW4ELevelTick@@@Z [HP1 0x103B3840] (animation part)
	void TickAnimation(UActor* actor, float elapsed)
	{
		// execPollFinishAnim: the latent FinishAnim ends once bAnimFinished is set.
		if (actor->StateFrame && actor->StateFrame->LatentState == LatentRunState::FinishAnim && actor->bAnimFinished())
			actor->StateFrame->LatentState = LatentRunState::Continue;

		// Transient channels send notifies and AnimEnd to the actor they animate.
		UActor* animator = (AnimBone(actor) != 0 && bAnimTransient(actor)) ? actor->Owner() : actor;
		if (!animator)
			animator = actor;

		float dt = elapsed;
		for (int iteration = 0; iteration < 4; iteration++)
		{
			if (!IsAnimatingSelf(actor) || dt <= 0.0f)
				break;

			// Note: the tween step doesn't consume dt, so a tween-only anim (AnimRate 0) advances
			// TweenAlpha up to 4x per tick. That's what the original does.
			if (actor->TweenRate() > 0.0f)
			{
				float& alpha = TweenAlpha(actor);
				alpha += dt * actor->TweenRate();
				if (alpha >= 1.0f)
				{
					alpha = 1.0f;
					actor->TweenRate() = 0.0f;
					if (actor->AnimRate() == 0.0f)
					{
						actor->bAnimFinished() = true;
						CallAnimEnd(animator);
					}
				}
			}

			if (actor->AnimRate() == 0.0f)
				continue;

			float oldFrame = actor->AnimFrame();
			float rate = actor->AnimRate() >= 0.0f ? actor->AnimRate() : std::max(actor->AnimMinRate(), -actor->AnimRate() * length(animator->Velocity()));
			actor->AnimFrame() += rate * dt;

			if (actor->bAnimNotify() && actor->Mesh())
			{
				// Earliest notify in (oldFrame, AnimFrame]
				MeshAnimSeq* seq = GetAnimSeq(actor, actor->AnimSequence());
				const MeshAnimNotify* hit = nullptr;
				if (seq)
				{
					for (const MeshAnimNotify& n : seq->Notifys)
					{
						if (oldFrame < n.Time && n.Time <= actor->AnimFrame() && (!hit || n.Time - oldFrame < hit->Time - oldFrame))
							hit = &n;
					}
				}
				if (hit)
				{
					float newFrame = actor->AnimFrame();
					dt = (newFrame - hit->Time) * dt / (newFrame - oldFrame);
					actor->AnimFrame() = hit->Time;
					if (FindEventFunction(animator, hit->Function))
						CallEvent(animator, hit->Function);
					continue;
				}
			}

			if (actor->AnimFrame() < actor->AnimLast())
				break;

			if (actor->bAnimLoop())
			{
				if (actor->AnimFrame() >= 1.0f)
				{
					dt = (actor->AnimFrame() - 1.0f) * dt / (actor->AnimFrame() - oldFrame);
					actor->AnimFrame() = 0.0f;
				}
				else
				{
					dt = 0.0f;
				}

				if (oldFrame < actor->AnimLast())
				{
					if (actor->StateFrame && actor->StateFrame->LatentState == LatentRunState::FinishAnim)
						actor->bAnimFinished() = true;
					CallAnimEnd(actor); // looping AnimEnd goes to the channel itself, not the animator
				}
			}
			else
			{
				float newFrame = actor->AnimFrame();
				dt = (newFrame - actor->AnimLast()) * dt / (newFrame - oldFrame);
				actor->AnimRate() = 0.0f;
				actor->AnimFrame() = actor->AnimLast();
				actor->bAnimFinished() = true;
				CallAnimEnd(animator);
			}
		}

		if (actor->StateFrame && actor->StateFrame->LatentState == LatentRunState::FinishAnim && actor->bAnimFinished())
			actor->StateFrame->LatentState = LatentRunState::Continue;
	}

	//
	// Natives
	//

	// IDA Engine.dll: ?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10409E20]
	static void NPlayAnim(UObject* self, const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<uint8_t> Type, std::optional<NameString> RootBone)
	{
		PlayAnim(UObject::Cast<UActor>(self), Sequence, false, Rate.value_or(1.0f), TweenTime.value_or(-1.0f), 0.0f, (EAnimType)Type.value_or(0), RootBone.value_or(NameString()));
	}

	// IDA Engine.dll: ?execLoopAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10409F60]
	static void NLoopAnim(UObject* self, const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<float> MinRate, std::optional<uint8_t> Type, std::optional<NameString> RootBone)
	{
		PlayAnim(UObject::Cast<UActor>(self), Sequence, true, Rate.value_or(1.0f), TweenTime.value_or(-1.0f), MinRate.value_or(0.0f), (EAnimType)Type.value_or(0), RootBone.value_or(NameString()));
	}

	// IDA Engine.dll: ?execTweenAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A0D0]
	static void NTweenAnim(UObject* self, const NameString& Sequence, float Time)
	{
		PlayAnim(UObject::Cast<UActor>(self), Sequence, false, 0.0f, Time, 0.0f, EAnimType::AT_Replace, {});
	}

	// IDA Engine.dll: ?execIsAnimating@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A170]
	static void NIsAnimating(UObject* self, std::optional<NameString> RootBone, BitfieldBool& ReturnValue)
	{
		ReturnValue = IsAnimating(UObject::Cast<UActor>(self), RootBone.value_or(NameString()));
	}

	// IDA Engine.dll: ?execFinishAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10408250]
	static void NFinishAnim(UObject* self, std::optional<NameString> RootBone)
	{
		FinishAnim(UObject::Cast<UActor>(self), RootBone.value_or(NameString()));
	}

	// IDA Engine.dll: ?execCreateAnimChannel@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10408C50]
	static void NCreateAnimChannel(UObject* self, UObject* NewClass, uint8_t Type, const NameString& RootBone, std::optional<bool> bTransient, UObject*& ReturnValue)
	{
		ReturnValue = CreateAnimChannel(UObject::Cast<UActor>(self), UObject::Cast<UClass>(NewClass), (EAnimType)Type, RootBone, bTransient.value_or(false));
	}

	// IDA Engine.dll: ?execHasAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A3D0]
	static void NHasAnim(UObject* self, const NameString& Sequence, BitfieldBool& ReturnValue)
	{
		ReturnValue = GetAnimSeq(UObject::Cast<UActor>(self), Sequence) != nullptr;
	}

	// IDA Engine.dll: ?execGetAnimGroup@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A2E0]
	static void NGetAnimGroup(UObject* self, const NameString& Sequence, NameString& ReturnValue)
	{
		MeshAnimSeq* seq = GetAnimSeq(UObject::Cast<UActor>(self), Sequence);
		ReturnValue = seq ? seq->Group : NameString();
	}

	// IDA Engine.dll: ?execLinkSkelAnim@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A700]
	static void NLinkSkelAnim(UObject* self, UObject* Anim)
	{
		UObject::Cast<UActor>(self)->SkelAnim() = UObject::TryCast<UAnimation>(Anim);
	}

	// IDA Engine.dll: ?execBoneNumber@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A470]
	static void NBoneNumber(UObject* self, const NameString& Bone, int& ReturnValue)
	{
		auto skel = UObject::TryCast<USkeletalMesh>(UObject::Cast<UActor>(self)->Mesh());
		ReturnValue = skel ? BoneIndex(skel, Bone) : -1;
	}

	// IDA Engine.dll: ?execBoneName@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A530]
	// IDA Engine.dll: ?BoneName@USkeletalMesh@@QBE?AVFName@@H@Z [HP1 0x1041DF00]
	static void NBoneName(UObject* self, int Bone, NameString& ReturnValue)
	{
		auto skel = UObject::TryCast<USkeletalMesh>(UObject::Cast<UActor>(self)->Mesh());
		ReturnValue = (skel && Bone >= 0 && Bone < (int)skel->RefSkeleton.size()) ? skel->RefSkeleton[Bone].Name : NameString();
	}

	void OverrideNative(int index, void (*registerFunc)());

	void RegisterAnimNatives()
	{
		OverrideNative(259, [] { RegisterVMNativeFunc_5("Actor", "PlayAnim", &NPlayAnim, 259); });
		OverrideNative(260, [] { RegisterVMNativeFunc_6("Actor", "LoopAnim", &NLoopAnim, 260); });
		OverrideNative(294, [] { RegisterVMNativeFunc_2("Actor", "TweenAnim", &NTweenAnim, 294); });
		OverrideNative(282, [] { RegisterVMNativeFunc_2("Actor", "IsAnimating", &NIsAnimating, 282); });
		OverrideNative(261, [] { RegisterVMNativeFunc_1("Actor", "FinishAnim", &NFinishAnim, 261); });
		OverrideNative(265, [] { RegisterVMNativeFunc_5("Actor", "CreateAnimChannel", &NCreateAnimChannel, 265); });
		OverrideNative(263, [] { RegisterVMNativeFunc_2("Actor", "HasAnim", &NHasAnim, 263); });
		OverrideNative(293, [] { RegisterVMNativeFunc_2("Actor", "GetAnimGroup", &NGetAnimGroup, 293); });
		OverrideNative(268, [] { RegisterVMNativeFunc_2("Actor", "BoneNumber", &NBoneNumber, 268); });
		OverrideNative(269, [] { RegisterVMNativeFunc_2("Actor", "BoneName", &NBoneName, 269); });
		NativeFunctions::NativeByName.erase({ NameString("LinkSkelAnim"), NameString("Actor") });
		RegisterVMNativeFunc_1("Actor", "LinkSkelAnim", &NLinkSkelAnim, 0);
	}
}
