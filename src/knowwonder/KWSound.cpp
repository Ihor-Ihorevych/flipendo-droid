#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/UMusic.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "Audio/AudioSource.h"
#include "Audio/AudioDevice.h"
#include <algorithm>
#include <atomic>
#include <chrono>
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

	// IDA Galaxy.dll: ?StopSound@UGalaxyAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@@Z [HP1 Galaxy 0x10607BA0]
	// Shared by HP1 (NStopSound below) and HP2 (src/hp2/HP2Natives.cpp, which adds FadeOutTime).
	// TODO HP2: fadeOutTime (ALAudio.dll's StopSound, not reversed yet); HP1 always passes 0.
	void StopSound(UActor* actor, USound* sound, uint8_t slot, float fadeOutTime)
	{
		if (engine->audiodev)
			engine->audiodev->StopSoundHP1(SoundId(actor, slot), sound);
	}

	// IDA Engine.dll: ?execStopSound@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C060]
	static void NStopSound(UObject* Self, std::optional<UObject*> Sound, std::optional<uint8_t> Slot)
	{
		StopSound(UObject::Cast<UActor>(Self), Sound ? UObject::Cast<USound>(*Sound) : nullptr, Slot.value_or(SLOT_Misc), 0.0f);
	}

	// Music, as Galaxy (HP1's audio driver) runs it every update:
	// - an mp2 Song plays as a streamed sound, looped unless PlayerPawn.bDontLoopSong (ClientSetMusic's bDontLoop;
	//   MusicEvent passes bPlayOnceOnly). bSongFinished is set when no music plays or the stream has ended; the end
	//   also clears Song with Transition = MTRAN_Instant. MusicEvent.WaitForSongToFinish waits on bSongFinished.
	// - a Transition is a request: the old song fades out (MTRAN_Fade 1 s, FastFade 1/3 s, SlowFade 5 s, the others at
	//   once), then the new one starts. The stream is only stopped and started again when the Song is a different one:
	//   a MusicEvent for the song already playing (a doorway trigger crossed again) leaves it playing.
	// - the music volume is PercentMusicVolume % (MusicEvent's boost) of MusicVolume, times the fade.
	// Upstream restarts the song on every Transition, switches at once and plays every stream once.
	namespace
	{
		struct PlayerMusicProps
		{
			PropertyDataOffset PercentMusicVolume;
			PropertyDataOffset bDontLoopSong;
			PropertyDataOffset bSongFinished;
		};

		const PlayerMusicProps& GetPlayerMusicProps()
		{
			static PlayerMusicProps props;
			static bool initialized = false;
			if (!initialized)
			{
				UClass* cls = engine->packages->FindClass("Engine.PlayerPawn");
				if (!cls)
					Exception::Throw("HP1: Engine.PlayerPawn class not found");
				props.PercentMusicVolume = cls->GetPropertyDataOffset("PercentMusicVolume");
				props.bDontLoopSong = cls->GetPropertyDataOffset("bDontLoopSong");
				props.bSongFinished = cls->GetPropertyDataOffset("bSongFinished");
				initialized = true;
			}
			return props;
		}

		// Runs on the music thread. Restarts the song at its end (a fresh decoder: upstream's mp3 source doesn't
		// rewind once it reached the end), or pads with silence and raises the finished flag.
		class SongSource : public AudioSource
		{
		public:
			SongSource(std::unique_ptr<AudioSource> source, Array<uint8_t> data, bool isMp3, bool loop, std::shared_ptr<std::atomic<bool>> finished)
				: source(std::move(source)), data(std::move(data)), isMp3(isMp3), loop(loop), finished(std::move(finished))
			{
			}

			int GetFrequency() override { return source->GetFrequency(); }
			int GetChannels() override { return source->GetChannels(); }
			int GetSamples() override { return source->GetSamples(); }
			void SeekToSample(uint64_t position) override { source->SeekToSample(position); }

			size_t ReadSamples(float* output, size_t samples) override
			{
				size_t pos = 0;
				bool restarted = false;
				while (pos < samples && !ended)
				{
					size_t count = source->ReadSamples(output + pos, samples - pos);
					pos += count;
					if (pos < samples)
					{
						if (!loop || (restarted && count == 0))
						{
							ended = true;
							finished->store(true);
						}
						else
						{
							if (isMp3)
								source = AudioSource::CreateMp3(data);
							else
								source->SeekToSample(0);
							restarted = true;
						}
					}
				}
				for (size_t i = pos; i < samples; i++)
					output[i] = 0.0f;
				return samples;
			}

		private:
			std::unique_ptr<AudioSource> source;
			Array<uint8_t> data;
			bool isMp3;
			bool loop;
			bool ended = false;
			std::shared_ptr<std::atomic<bool>> finished;
		};

		// Galaxy's music state: the current song, its CD track and section, MusicFade
		struct MusicState
		{
			UMusic* song = nullptr; // only compared, never read (it can be gone after a level change)
			uint8_t cdTrack = 255;
			uint8_t section = 255;
			float fade = 1.0f;
			float volume = 0.0f; // the music volume last set
			std::shared_ptr<std::atomic<bool>> finished; // the playing stream's end flag; null when no song plays
			std::chrono::steady_clock::time_point lastUpdate;
			bool started = false;
		} music;

		std::unique_ptr<AudioSource> CreateSongSource(UPlayerPawn* player, UMusic* song)
		{
			// HP1's songs are all mp2; Galaxy streams those, the other formats are upstream's
			bool isMp3 = song->Format == "mp3" || song->Format == "mp2";
			std::unique_ptr<AudioSource> source;
			if (isMp3)
				source = AudioSource::CreateMp3(song->Data);
			else if (song->Format == "ogg")
				source = AudioSource::CreateOgg(song->Data, true);
			else if (song->Format == "wav")
				source = AudioSource::CreateWav(song->Data);
			else
				source = AudioSource::CreateMod(song->Data, true, music.section != 255 ? music.section : 0);
			if (!source)
				return source;
			bool loop = !player->BoolValue(GetPlayerMusicProps().bDontLoopSong);
			music.finished = std::make_shared<std::atomic<bool>>(false);
			return std::make_unique<SongSource>(std::move(source), isMp3 ? song->Data : Array<uint8_t>(), isMp3, loop, music.finished);
		}
	}

	// IDA Galaxy.dll: ?Update@UGalaxyAudioSubsystem@@UAEXUFPointRegion@@AAVFCoords@@@Z [HP1 Galaxy 0x106081F0] (the music part, from bSongFinished at 0x10608F21; the music channel's volume in the channel loop)
	// IDA Galaxy.dll: ?SetVolumes@UGalaxyAudioSubsystem@@QAEXXZ [HP1 Galaxy 0x10606640]
	float UpdateMusic(AudioDevice* device, UPlayerPawn* player, uint8_t musicVolume, int latency)
	{
		// Galaxy's time step: real time since the last update, at most 1 s
		auto now = std::chrono::steady_clock::now();
		float deltaTime = music.started ? std::chrono::duration<float>(now - music.lastUpdate).count() : 0.0f;
		deltaTime = std::clamp(deltaTime, 0.0f, 1.0f);
		music.lastUpdate = now;
		music.started = true;

		// No player while a level or save loads: the music goes on as it is
		if (!player)
			return music.volume;

		const PlayerMusicProps& props = GetPlayerMusicProps();

		// No music playing: only the flag. A song that played to its end is also cleared.
		bool ended = music.finished && music.finished->load();
		player->BoolValue(props.bSongFinished) = !music.finished || ended;
		if (ended)
		{
			music.finished.reset();
			player->Transition() = MTRAN_Instant;
			player->Song() = nullptr;
		}

		uint8_t transition = player->Transition();
		if (transition != MTRAN_None)
		{
			// With the music volume at 0 Galaxy doesn't change the stream, even for a different song
			bool changed = music.song != player->Song() && musicVolume != 0;
			if (music.song || music.cdTrack != 255)
			{
				// Fade the old song out first; a song set with section 255 stops at once. The fade ends a little
				// below 0, by the output latency.
				bool faded = true;
				if (music.section != 255)
				{
					if (transition == MTRAN_Fade)
					{
						music.fade -= deltaTime;
						faded = music.fade < latency * -0.002f;
					}
					else if (transition == MTRAN_SlowFade)
					{
						music.fade -= deltaTime * 0.2f;
						faded = music.fade < latency * -0.0004f;
					}
					else if (transition == MTRAN_FastFade)
					{
						music.fade -= deltaTime * 3.0f;
						faded = music.fade < latency * -0.006f;
					}
				}
				if (faded)
				{
					if (music.song && changed)
					{
						device->PlayMusic(nullptr);
						music.finished.reset();
					}
					music.song = nullptr;
					music.cdTrack = 255;
				}
			}

			if (!music.song && music.cdTrack == 255)
			{
				music.fade = 1.0f;
				music.song = player->Song();
				music.cdTrack = player->CdTrack();
				music.section = player->SongSection();
				if (music.song && changed)
				{
					if (auto source = CreateSongSource(player, music.song))
						device->PlayMusic(std::move(source));
				}
				player->Transition() = MTRAN_None;
			}
		}

		float volume = player->Value<uint8_t>(props.PercentMusicVolume) * musicVolume / (255.0f * 100.0f) * music.fade;
		music.volume = std::clamp(volume, 0.0f, 1.0f);
		return music.volume;
	}

	void RegisterSoundNatives()
	{
		OverrideNative(567, [] { RegisterVMNativeFunc_5("Actor", "ModifySound", &NModifySound, 567); });
		OverrideNative(568, [] { RegisterVMNativeFunc_2("Actor", "StopSound", &NStopSound, 568); });
	}
}
