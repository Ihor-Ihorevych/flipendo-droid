#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Audio/AudioDevice.h"
#include "Audio/AudioSource.h"
#include "Utils/File.h"
#include "VM/NativeFunc.h"
#include "Engine.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Utils/StrTools.h"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>
#include <cmath>

// HP2's music: no Galaxy songs in .umx packages but Ogg Vorbis files in Music/, which scripts start and stop by name
// (PlayerPawn.ClientSetMusic, MusicTrigger, NewMusicTrigger, cutscenes, FEBook). ALAudio plays each song as a sound
// source of its own, so several play at once: a level's music goes on under a jingle that a MusicTrigger starts and
// stops again. Here the songs are mixed into the audio device's one music stream (docs/re/hp2/engine.md).

namespace HP2
{
	namespace
	{
		struct Track
		{
			int handle = 0;
			std::unique_ptr<AudioSource> source;
			int fadeState = 0;       // ALAudio's: 0 none, 1 fading in, 2 fading out (the source stops at the end)
			float fadeTime = 0.0f;
			float fadeElapsed = 0.0f;
			float gain = 1.0f;       // set by UpdateMusic
			float mixedGain = 1.0f;  // where the mixer's ramp to gain got to
		};

		// The tracks are changed by the game thread (the natives, UpdateMusic) and read by the audio device's music
		// thread (Mixer::ReadSamples)
		std::mutex tracksMutex;
		std::vector<Track> tracks;
		int lastHandle = 0;
		bool mixerPlaying = false;
		bool started = false;
		std::chrono::steady_clock::time_point lastUpdate;

		const int MixFrequency = 44100;
		const int MixChannels = 2;

		class Mixer : public AudioSource
		{
		public:
			int GetFrequency() override { return MixFrequency; }
			int GetChannels() override { return MixChannels; }
			int GetSamples() override { return -1; }
			void SeekToSample(uint64_t position) override { }

			size_t ReadSamples(float* output, size_t samples) override
			{
				std::fill(output, output + samples, 0.0f);
				buffer.resize(samples);
				size_t frames = samples / MixChannels;
				std::lock_guard lock(tracksMutex);
				for (Track& track : tracks)
				{
					size_t count = track.source->ReadSamples(buffer.data(), samples);
					// A ramp from the last chunk's gain, so a fade has no steps
					float from = track.mixedGain, to = track.gain;
					for (size_t i = 0; i < count; i++)
					{
						float t = frames > 0 ? (float)(i / MixChannels) / (float)frames : 1.0f;
						output[i] += buffer[i] * (from + (to - from) * t);
					}
					track.mixedGain = to;
				}
				for (size_t i = 0; i < samples; i++)
					output[i] = std::clamp(output[i], -1.0f, 1.0f);
				return samples;
			}

		private:
			Array<float> buffer;
		};

		// UALAudioSubsystem::StopMusic: no fade time stops the source now, else it fades out from full volume
		void StopTrack(std::vector<Track>::iterator it, float fadeOutTime)
		{
			if (fadeOutTime <= 0.0f)
			{
				tracks.erase(it);
			}
			else
			{
				it->fadeElapsed = 0.0f;
				it->fadeTime = fadeOutTime;
				it->fadeState = 2;
			}
		}
	}

	// IDA ALAudio.dll: ?PlayMusic@UALAudioSubsystem@@UAEHVFString@@M@Z [HP2 ALAudio 0x10004C70]
	// IDA ALAudio.dll: ?PlaySound@UALAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@VFVector@@MMMHMM@Z [HP2 ALAudio 0x100040F0] (flags 28: streamed, music volume, no position; the fade in)
	// IDA ALAudio.dll: ?RegisterSound@UALAudioSubsystem@@UAEHPAVUSound@@@Z [HP2 ALAudio 0x10003400] (a music Ogg is stream type 2, which FFileStream::Read loops)
	// Actor.PlayMusic(string Song, float FadeInTime): "..\Music\<Song>", with ".ogg" added when the name has another
	// extension or none. An empty song or "None" plays nothing. Returns the handle, 0 when nothing plays. ALAudio's handle
	// is the sound source's index + 1 (reused by later sounds); here every song gets a new one.
	static void NPlayMusic(UObject* Self, const std::string& Song, float FadeInTime, int& ReturnValue)
	{
		ReturnValue = 0;
		if (Song.empty() || StrTools::equals_ignore_case(Song, "None"))
			return;
		std::filesystem::path folder = KW::FindIgnoringCase(engine->LaunchInfo.gameRootFolder, "Music");
		std::filesystem::path path = KW::FindIgnoringCase(folder, Song);
		if (path.empty() || !StrTools::equals_ignore_case(path.extension().string(), ".ogg"))
			path = KW::FindIgnoringCase(folder, Song + ".ogg");
		std::unique_ptr<AudioSource> source;
		if (!path.empty())
			source = AudioSource::CreateOgg(File::read_all_bytes(path.string()), true);
		if (source && source->GetFrequency() != MixFrequency)
			source = AudioSource::CreateResampler(MixFrequency, std::move(source));
		if (!source || source->GetChannels() != MixChannels)
		{
			LogMessage("PlayMusic: can't play " + Song);
			return;
		}

		Track track;
		track.handle = ++lastHandle;
		track.source = std::move(source);
		if (FadeInTime > 0.0f)
		{
			track.fadeState = 1;
			track.fadeTime = FadeInTime;
			track.gain = track.mixedGain = 0.0f;
		}
		std::lock_guard lock(tracksMutex);
		tracks.push_back(std::move(track));
		ReturnValue = lastHandle;
	}

	// IDA ALAudio.dll: ?StopMusic@UALAudioSubsystem@@UAEHHM@Z [HP2 ALAudio 0x100050C0]
	static void NStopMusic(UObject* Self, int SongHandle, float FadeOutTime)
	{
		std::lock_guard lock(tracksMutex);
		auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.handle == SongHandle; });
		if (it != tracks.end())
			StopTrack(it, FadeOutTime);
	}

	// IDA ALAudio.dll: ?StopAllMusic@UALAudioSubsystem@@UAEHM@Z [HP2 ALAudio 0x100051B0]
	static void NStopAllMusic(UObject* Self, float FadeOutTime)
	{
		std::lock_guard lock(tracksMutex);
		if (FadeOutTime <= 0.0f)
		{
			tracks.clear();
			return;
		}
		for (auto it = tracks.begin(); it != tracks.end(); ++it)
			StopTrack(it, FadeOutTime);
	}

	void RegisterMusicNatives()
	{
		RegisterVMNativeFunc_3("Actor", "PlayMusic", &NPlayMusic, 0);
		RegisterVMNativeFunc_2("Actor", "StopMusic", &NStopMusic, 0);
		RegisterVMNativeFunc_1("Actor", "StopAllMusic", &NStopAllMusic, 0);
	}

	// IDA ALAudio.dll: ?Update@UALAudioSubsystem@@UAEXPAUFSceneNode@@@Z [HP2 ALAudio 0x100052D0] (the fades: a step of at most 1 s, linear in time)
	float UpdateMusic(AudioDevice* device, uint8_t musicVolume)
	{
		auto now = std::chrono::steady_clock::now();
		float deltaTime = started ? std::chrono::duration<float>(now - lastUpdate).count() : 0.0f;
		lastUpdate = now;
		started = true;

		if (!mixerPlaying)
		{
			device->PlayMusic(std::make_unique<Mixer>());
			mixerPlaying = true;
		}

		{
			std::lock_guard lock(tracksMutex);
			for (auto it = tracks.begin(); it != tracks.end();)
			{
				if (it->fadeState != 0 && deltaTime < 1.0f)
					it->fadeElapsed += deltaTime;
				if (it->fadeState == 1)
				{
					if (it->fadeElapsed < it->fadeTime)
						it->gain = it->fadeElapsed / it->fadeTime;
					else
					{
						it->gain = 1.0f;
						it->fadeState = 0;
					}
				}
				else if (it->fadeState == 2)
				{
					if (it->fadeElapsed >= it->fadeTime)
					{
						it = tracks.erase(it);
						continue;
					}
					it->gain = 1.0f - it->fadeElapsed / it->fadeTime;
				}
				else
				{
					it->gain = 1.0f;
				}
				++it;
			}
		}

		return !SoundDisabled() ? musicVolume / 255.0f : 0.0f;
	}

	static bool soundsMuted = false;

	// ALAudio keeps MusicVolume and SoundVolume as floats 0..1 (the ini holds MusicVolume=0.53); the audio device keeps
	// HP1's bytes 0..255.
	uint8_t VolumeFromIni(const std::string& value, uint8_t defaultValue)
	{
		try
		{
			return (uint8_t)std::lround(std::clamp(std::stof(value), 0.0f, 1.0f) * 255.0f);
		}
		catch (...)
		{
			return defaultValue;
		}
	}

	std::string VolumeToIni(uint8_t volume)
	{
		return std::to_string(volume / 255.0f);
	}

	// IDA ALAudio.dll: ?Exec@UALAudioSubsystem@@UAEHPBGAAVFOutputDevice@@@Z [HP2 ALAudio 0x100067B0]
	// MUTESOUNDS sets a flag and the gain of every active sound source to 0, UNMUTESOUNDS clears it and gives each source
	// its gain back; the music streams aren't touched. Here the flag holds the sound volume at 0 instead.
	// SETVOLUMES SOUND=<f> MUSIC=<f> (either or both, FESoundVideoPage's sliders / 100) clamps each to 0..1, stores it and
	// sets the gain of every playing source at once: a music stream gets the music volume, a sound its own volume times
	// the sound volume. The audio device here applies its volumes every frame.
	bool AudioCommand(const Array<std::string>& args)
	{
		if (args.empty())
			return false;
		if (StrTools::equals_ignore_case(args[0], "MUTESOUNDS"))
		{
			soundsMuted = true;
		}
		else if (StrTools::equals_ignore_case(args[0], "UNMUTESOUNDS"))
		{
			soundsMuted = false;
		}
		else if (StrTools::equals_ignore_case(args[0], "SETVOLUMES"))
		{
			for (size_t i = 1; i < args.size(); i++)
			{
				size_t eq = args[i].find('=');
				if (eq == std::string::npos || !engine->audiodev)
					continue;
				std::string key = args[i].substr(0, eq);
				if (StrTools::equals_ignore_case(key, "SOUND"))
					engine->audiodev->SoundVolume = VolumeFromIni(args[i].substr(eq + 1), engine->audiodev->SoundVolume);
				else if (StrTools::equals_ignore_case(key, "MUSIC"))
					engine->audiodev->MusicVolume = VolumeFromIni(args[i].substr(eq + 1), engine->audiodev->MusicVolume);
			}
		}
		else
		{
			return false;
		}
		return true;
	}

	bool SoundsMuted()
	{
		return soundsMuted || SoundDisabled();
	}
}
