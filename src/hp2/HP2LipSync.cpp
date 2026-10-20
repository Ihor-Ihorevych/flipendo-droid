#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "Anim/KWAnimation.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include <chrono>
#include <cmath>
#include <cstring>

// HP2's lip sync (docs/re/hp2/engine.md): a sound with flag 0x80 carries a second lazy array, one amplitude byte every
// 0.02 s of the sound. When ALAudio plays such a sound on an actor it gives the actor an AnimChannel on its mouth_root
// bone playing mouth_anim at rate 0, and every 0.05 s sets the channel's AnimFrame to the mouth position for the time
// since the sound started. When the sound stops the channel tweens to mouth_rest.

namespace HP2
{
	namespace
	{
		constexpr float AmpSampleInterval = 0.02f; // FLipSyncData::fAmpSampleInterval
		constexpr uint32_t SoundHasLipSync = 0x80;

		double Now()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// UE1's appFloor: fistp of x - 0.5 under round to nearest
		int FloorToInt(float x)
		{
			return (int)std::nearbyint(x - 0.5f);
		}

		// IDA Engine.dll: ?GetInterpAmplitude@FLipSyncData@@IAEEM@Z [HP2 0x10388F30]
		// The amplitude at a time, clamped to the sound's duration (USound +104, the first of the HP2Values), blended
		// linearly between the two samples around it.
		uint8_t GetInterpAmplitude(USound* sound, float time)
		{
			const Array<uint8_t>& amps = sound->HP2ExtraData;
			float duration;
			memcpy(&duration, &sound->HP2Values[0], sizeof(float));

			float t = time >= 0.0f ? std::min(time, duration) : 0.0f;
			if (t + AmpSampleInterval <= duration)
			{
				int i0 = FloorToInt(t / AmpSampleInterval);
				int i1 = FloorToInt((t + AmpSampleInterval) / AmpSampleInterval);
				float frac = std::fmod(t, AmpSampleInterval);
				if (i0 < 0 || i1 >= (int)amps.size())
					return 0;
				return (uint8_t)(int64_t)((amps[i0] * (double)(AmpSampleInterval - frac) + amps[i1] * (double)frac) / AmpSampleInterval);
			}
			if (amps.empty())
				return 0;
			int i = std::clamp(FloorToInt(t / AmpSampleInterval), 0, (int)amps.size() - 1);
			return amps[i];
		}

		// IDA Engine.dll: ?CalcMaxAmplitude@FLipSyncData@@IAEXXZ [HP2 0x10389170]
		// IDA Engine.dll: ?GetMaxAmplitude@FLipSyncData@@IAEEXZ [HP2 0x10389140]
		uint8_t GetMaxAmplitude(USound* sound)
		{
			uint8_t maxAmp = 0;
			for (uint8_t a : sound->HP2ExtraData)
				maxAmp = std::max(maxAmp, a);
			return maxAmp;
		}

		// IDA Engine.dll: ?GetMouthPos@FLipSyncData@@QAEMM@Z [HP2 0x10388EB0]
		// 0 (closed) to 0.63 of mouth_anim. A sound whose amplitudes are all 0 divides 0 by 0 in the original.
		float GetMouthPos(USound* sound, float time)
		{
			uint8_t maxAmp = GetMaxAmplitude(sound);
			if (maxAmp == 0)
				return 0.0f;
			return (float)((double)GetInterpAmplitude(sound, time) / (double)maxAmp * 0.9f * 0.7f);
		}
	}

	// IDA ALAudio.dll: ?PlaySound@UALAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@VFVector@@MMMHMM@Z [HP2 ALAudio 0x100040F0] (a sound with flag 0x80 played on an actor gets a lip sync tracker)
	// IDA ALAudio.dll: not exported: sub_100075D0 [HP2 ALAudio 0x100075D0] (the tracker's start, called at the end of PlaySound: string "mouth_root")
	// IDA ALAudio.dll: not exported: sub_10007670 [HP2 ALAudio 0x10007670] (the tracker's update, called from UALAudioSubsystem::Update for every playing source)
	void UpdateLipSync(PlayingSound& playing)
	{
		if (!playing.Actor || !playing.Sound || !(playing.Sound->HP2Flags & SoundHasLipSync))
			return;

		if (playing.LipSyncStart < 0.0)
		{
			playing.LipSyncStart = Now();
			playing.LipSyncLast = 0.0f;
			UClass* channelClass = engine->packages->FindClass("Engine.AnimChannel");
			playing.MouthChannel = KW::CreateAnimChannel(playing.Actor, channelClass, EAnimType::AT_Replace, NameString("mouth_root"), false, true);
			if (playing.MouthChannel)
				KW::PlayAnim(playing.MouthChannel, NameString("mouth_anim"), false, 0.0f, -1.0f, 0.0f, EAnimType::AT_Replace, {});
			return;
		}

		if (!playing.MouthChannel || playing.MouthChannel->bDeleteMe())
			return;
		float time = (float)(Now() - playing.LipSyncStart);
		if (time - playing.LipSyncLast >= 0.05f)
		{
			playing.LipSyncLast = time;
			playing.MouthChannel->AnimFrame() = GetMouthPos(playing.Sound, time);
		}
	}

	// IDA ALAudio.dll: ?OldStopSound@UALAudioSubsystem@@QAEXH@Z [HP2 ALAudio 0x10006E30] (every stopped source, through StopSoundSource)
	// IDA ALAudio.dll: not exported: sub_10007580 [HP2 ALAudio 0x10007580] (the tracker's stop: string "mouth_rest")
	void StopLipSync(PlayingSound& playing)
	{
		if (playing.Actor && playing.MouthChannel && !playing.MouthChannel->bDeleteMe())
			KW::PlayAnim(playing.MouthChannel, NameString("mouth_rest"), false, 1.0f, 1.0f, 1.0f, EAnimType::AT_Replace, {});
		playing.MouthChannel = nullptr;
		playing.LipSyncStart = -1.0;
	}
}
