#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Anim/KWAnimation.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/USound.h"
#include "VM/NativeFunc.h"
#include "GameWindow.h"
#include "Engine.h"
#include "Native/NCanvas.h"
#include "Packages/Core/UClass.h"

// HP2's natives: the ones HP1 doesn't have (BoneRot, IsSoftwareRendering, GetCurrentKeyState; music in HP2Music.cpp), and
// adapters for natives whose script signature grew in HP2. Adapters only unpack the HP2 arguments and call the shared
// body in src/knowwonder/, so the behaviour exists once (docs/one-engine.md).

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());
	void StopSound(UActor* actor, USound* sound, uint8_t slot, float fadeOutTime);
	void PlaySound(UActor* actor, USound* sound, uint8_t slot, float volume, bool noOverride, float radius, float pitch, bool disable3D, bool loop);
	float TransientSoundPitchScale(UActor* actor);
	UActor* CreateAnimChannel(UActor* actor, UClass* newClass, EAnimType type, const NameString& rootBone, bool bTransient, bool bNotReplaceable);
}

namespace HP2
{
	// IDA Engine.dll: ?execBoneRot@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x10418350]
	// Actor.BoneRot(name Bone): HP1's BonePos (src/knowwonder/KWAttach.cpp) giving the bone frame's rotation instead of its
	// origin (GetBoneCoords, vtable +156, then FCoords::OrthoRotation). Only for skeletal meshes, otherwise Rotation.
	static void NBoneRot(UObject* Self, const NameString& Bone, Rotator& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(actor->Mesh());
		vec3 origin, x, y, z;
		if (mesh && KW::GetBoneCoords(actor, mesh, KW::BoneIndex(mesh, Bone), origin, x, y, z))
			ReturnValue = KW::OrthoRotation(x, y, z);
		else
			ReturnValue = actor->Rotation();
	}

	// IDA Engine.dll: ?execIsSoftwareRendering@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x10422A50]
	// The original compares [Engine.Engine] GameRenderDevice in the ini with "SoftDrv.SoftwareRenderDevice". Flipendo
	// always renders with its own hardware render devices, so this is never true.
	static void NIsSoftwareRendering(UObject* Self, BitfieldBool& ReturnValue)
	{
		ReturnValue = false;
	}

	// IDA Engine.dll: ?execGetCurrentKeyState@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x10422C60]
	// Actor.GetCurrentKeyState(EInputKey Key): whether the key is held right now. The original reads the first
	// viewport's input key-down table (XLevel->Engine->Client->Viewports(0)->Input), Key clamped to 0..254, false
	// without a viewport.
	static void NGetCurrentKeyState(UObject* Self, uint8_t Key, BitfieldBool& ReturnValue)
	{
		ReturnValue = engine->window && engine->window->GetKeyState((EInputKey)std::min<int>(Key, 254));
	}

	// IDA Engine.dll: ?execCreateAnimChannel@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x104165B0]
	// HP2 added `optional bool bNotReplaceable` (the shared body is in src/knowwonder/Anim/KWAnimState.cpp).
	static void NCreateAnimChannel(UObject* Self, UObject* NewClass, uint8_t Type, const NameString& RootBone, std::optional<bool> bTransient, std::optional<bool> bNotReplaceable, UObject*& ReturnValue)
	{
		ReturnValue = KW::CreateAnimChannel(UObject::Cast<UActor>(Self), UObject::Cast<UClass>(NewClass), (EAnimType)Type, RootBone, bTransient.value_or(false), bNotReplaceable.value_or(false));
	}

	// IDA Engine.dll: ?execStopSound@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x1041A550]
	// HP2 added `optional float FadeOutTime` (HP1: StopSound(Sound, Slot)); Slot still defaults to SLOT_Misc. The
	// original passes it to the audio subsystem (vtable +116, ALAudio.dll) with a slot id built from the actor's
	// object index instead of HP1's pointer; the shared body in src/knowwonder/KWSound.cpp keeps one id scheme for both games.
	static void NStopSound(UObject* Self, std::optional<UObject*> Sound, std::optional<uint8_t> Slot, std::optional<float> FadeOutTime)
	{
		KW::StopSound(UObject::Cast<UActor>(Self), Sound ? UObject::Cast<USound>(*Sound) : nullptr, Slot.value_or(SLOT_Misc), FadeOutTime.value_or(0.0f));
	}

	// IDA Engine.dll: ?execPlaySound@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x104192B0]
	// IDA Engine.dll: ?execPlayOwnedSound@AActor@@QAEXAAUFFrame@@QAX@Z [HP2 0x10419CA0] NOT yet verified against HP2's code (taken to match PlaySound)
	// HP2 added `optional bool Disable3D, optional bool Loop`; the shared body is in src/knowwonder/KWSound.cpp.
	static void NPlaySound(UObject* Self, UObject* Sound, std::optional<uint8_t> Slot, std::optional<float> Volume, std::optional<bool> bNoOverride,
		std::optional<float> Radius, std::optional<float> Pitch, std::optional<bool> Disable3D, std::optional<bool> Loop)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		KW::PlaySound(actor, UObject::TryCast<USound>(Sound), Slot.value_or(SLOT_Misc), Volume.value_or(actor->TransientSoundVolume()), bNoOverride.value_or(false),
			Radius.value_or(actor->TransientSoundRadius()), Pitch.value_or(KW::TransientSoundPitchScale(actor)), Disable3D.value_or(false), Loop.value_or(false));
	}

	// IDA Engine.dll: ?execDrawText@UCanvas@@QAEXAAUFFrame@@QAX@Z NOT yet verified against HP2's code
	// HP2 added `optional int numChars, optional bool bCursor`. No HP2 script passes either, so the text is drawn as in HP1.
	static void NDrawText(UObject* Self, const std::string& Text, std::optional<bool> CR, std::optional<int> numChars, std::optional<bool> bCursor)
	{
		NCanvas::DrawText(Self, Text, CR);
	}

	void RegisterNatives()
	{
		KW::OverrideNative(328, [] { RegisterVMNativeFunc_2("Actor", "BoneRot", &NBoneRot, 328); });
		KW::OverrideNative(329, [] { RegisterVMNativeFunc_1("Actor", "IsSoftwareRendering", &NIsSoftwareRendering, 329); });
		KW::OverrideNative(330, [] { RegisterVMNativeFunc_2("Actor", "GetCurrentKeyState", &NGetCurrentKeyState, 330); });
		KW::OverrideNative(265, [] { RegisterVMNativeFunc_6("Actor", "CreateAnimChannel", &NCreateAnimChannel, 265); });
		KW::OverrideNative(568, [] { RegisterVMNativeFunc_3("Actor", "StopSound", &NStopSound, 568); });
		KW::OverrideNative(264, [] { RegisterVMNativeFunc_8("Actor", "PlaySound", &NPlaySound, 264); });
		RegisterVMNativeFunc_8("Actor", "PlayOwnedSound", &NPlaySound, 0);
		KW::OverrideNative(465, [] { RegisterVMNativeFunc_4("Canvas", "DrawText", &NDrawText, 465); });
		RegisterMusicNatives();
	}
}
