#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Resources/USound.h"
#include "VM/NativeFunc.h"
#include "Engine.h"

// Actor.ModifySound / Actor.StopSound: change or stop a playing sound by slot (and optionally by sound).
// BroomHarry.UpdateBroomSound fades the broom loop with ModifySound(SOUND_Volume, ...) every tick.

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// Same id as upstream's NActor::PlaySound for this actor and slot (bit 0, bNoOverride, is ignored when matching).
	static int SoundId(UActor* actor, uint8_t slot)
	{
		return ((((int)(ptrdiff_t)actor) & 0xffffff) << 4) + (slot << 1);
	}

	// IDA Engine.dll: ?execModifySound@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040BF20]
	// IDA Galaxy.dll: ?ModifySound@UGalaxyAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@EM@Z [HP1 Galaxy 0x10607AE0]
	static void NModifySound(UObject* Self, uint8_t Parameter, float Value, std::optional<UObject*> Sound, std::optional<uint8_t> Slot, BitfieldBool& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		USound* sound = Sound ? UObject::Cast<USound>(*Sound) : nullptr;
		ReturnValue = engine->audiodev && engine->audiodev->ModifySoundHP1(SoundId(actor, Slot.value_or(SLOT_Misc)), sound, Parameter, Value);
	}

	// IDA Engine.dll: ?execStopSound@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C060]
	// IDA Galaxy.dll: ?StopSound@UGalaxyAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@@Z [HP1 Galaxy 0x10607BA0]
	static void NStopSound(UObject* Self, std::optional<UObject*> Sound, std::optional<uint8_t> Slot)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		USound* sound = Sound ? UObject::Cast<USound>(*Sound) : nullptr;
		if (engine->audiodev)
			engine->audiodev->StopSoundHP1(SoundId(actor, Slot.value_or(SLOT_Misc)), sound);
	}

	void RegisterSoundNatives()
	{
		OverrideNative(567, [] { RegisterVMNativeFunc_5("Actor", "ModifySound", &NModifySound, 567); });
		OverrideNative(568, [] { RegisterVMNativeFunc_2("Actor", "StopSound", &NStopSound, 568); });
	}
}
