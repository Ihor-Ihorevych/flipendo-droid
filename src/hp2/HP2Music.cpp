#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Audio/AudioDevice.h"
#include "Audio/AudioSource.h"
#include "Utils/File.h"
#include "VM/NativeFunc.h"
#include "Engine.h"
#include <algorithm>
#include <chrono>

// HP2's music: no Galaxy songs in .umx packages but Ogg Vorbis files in Music/, which scripts start and stop by name
// (PlayerPawn.ClientSetMusic, MusicTrigger, FEBook). The natives only record the request; UpdateMusic, called every
// frame from the audio device in place of HP1's Galaxy music update, starts the song and runs the fades.
// One song plays at a time: a new PlayMusic replaces the one playing.

namespace HP2
{
	namespace
	{
		struct MusicState
		{
			int lastHandle = 0;
			int handle = 0;               // the song playing (or about to start), 0 = none
			std::string pendingSong;      // set by PlayMusic, started by UpdateMusic
			bool stopPending = false;
			float fade = 0.0f;            // 0..1
			float fadeRate = 0.0f;        // per second, > 0 fading in, < 0 fading out
			bool started = false;
			std::chrono::steady_clock::time_point lastUpdate;
		};
		MusicState music;

		void StartFade(float seconds, bool in)
		{
			float rate = seconds > 0.0f ? 1.0f / seconds : 1000.0f;
			music.fadeRate = in ? rate : -rate;
		}
	}

	// IDA Engine.dll: ?execPlayMusic@AActor@@QAEXAAUFFrame@@QAX@Z NOT yet verified against HP2's code (ALAudio.dll not reversed)
	// Actor.PlayMusic(string Song, float FadeInTime): the file name in Music/ ("sm_dia_Ambient02_01.ogg"); returns a handle.
	static void NPlayMusic(UObject* Self, const std::string& Song, float FadeInTime, int& ReturnValue)
	{
		music.handle = ++music.lastHandle;
		music.pendingSong = Song;
		music.stopPending = false;
		music.fade = 0.0f;
		StartFade(FadeInTime, true);
		ReturnValue = music.handle;
	}

	// IDA Engine.dll: ?execStopMusic@AActor@@QAEXAAUFFrame@@QAX@Z NOT yet verified against HP2's code (ALAudio.dll not reversed)
	static void NStopMusic(UObject* Self, int SongHandle, float FadeOutTime)
	{
		if (SongHandle != 0 && SongHandle == music.handle)
			StartFade(FadeOutTime, false);
	}

	// IDA Engine.dll: ?execStopAllMusic@AActor@@QAEXAAUFFrame@@QAX@Z NOT yet verified against HP2's code (ALAudio.dll not reversed)
	static void NStopAllMusic(UObject* Self, float FadeOutTime)
	{
		if (music.handle == 0)
			return;
		if (FadeOutTime <= 0.0f)
		{
			music.handle = 0;
			music.pendingSong.clear();
			music.stopPending = true;
		}
		else
		{
			StartFade(FadeOutTime, false);
		}
	}

	void RegisterMusicNatives()
	{
		RegisterVMNativeFunc_3("Actor", "PlayMusic", &NPlayMusic, 0);
		RegisterVMNativeFunc_2("Actor", "StopMusic", &NStopMusic, 0);
		RegisterVMNativeFunc_1("Actor", "StopAllMusic", &NStopAllMusic, 0);
	}

	float UpdateMusic(AudioDevice* device, uint8_t musicVolume)
	{
		auto now = std::chrono::steady_clock::now();
		float deltaTime = music.started ? std::clamp(std::chrono::duration<float>(now - music.lastUpdate).count(), 0.0f, 1.0f) : 0.0f;
		music.lastUpdate = now;
		music.started = true;

		if (music.stopPending)
		{
			device->PlayMusic(nullptr);
			music.stopPending = false;
		}

		if (!music.pendingSong.empty())
		{
			std::string song = music.pendingSong;
			music.pendingSong.clear();
			std::filesystem::path path = KW::FindIgnoringCase(KW::FindIgnoringCase(engine->LaunchInfo.gameRootFolder, "Music"), song);
			std::unique_ptr<AudioSource> source;
			if (!path.empty())
				source = AudioSource::CreateOgg(File::read_all_bytes(path.string()), true);
			if (!source)
				LogMessage("PlayMusic: can't play " + song);
			device->PlayMusic(std::move(source));
		}

		if (music.handle != 0)
		{
			music.fade = std::clamp(music.fade + music.fadeRate * deltaTime, 0.0f, 1.0f);
			if (music.fadeRate < 0.0f && music.fade <= 0.0f)
			{
				device->PlayMusic(nullptr);
				music.handle = 0;
			}
		}

		return music.handle != 0 ? music.fade * musicVolume / 255.0f : 0.0f;
	}
}
