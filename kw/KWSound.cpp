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
#include <atomic>
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
	// Shared by HP1 (NStopSound below) and HP2 (hp2/HP2Natives.cpp, which adds FadeOutTime).
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

	// Music. Galaxy plays an mp2 Song as a streamed sound, looped unless PlayerPawn.bDontLoopSong (ClientSetMusic's
	// bDontLoop; MusicEvent passes bPlayOnceOnly). Every update it sets PlayerPawn.bSongFinished when no music plays
	// or the stream has ended, and on the end clears Song with Transition = MTRAN_Instant, so the song stops cleanly.
	// MusicEvent.WaitForSongToFinish waits on bSongFinished. Upstream plays the stream once and leaves the music
	// thread replaying stale buffers at the end (a stuck ~1 s loop).
	namespace
	{
		struct PlayerMusicProps
		{
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

		std::shared_ptr<std::atomic<bool>> songFinished; // the playing song's flag; null when no song plays
	}

	// IDA Galaxy.dll: ?Update@UGalaxyAudioSubsystem@@UAEXUFPointRegion@@AAVFCoords@@@Z [HP1 Galaxy 0x106081F0] (starting the mp2 song: USound(Music, !bDontLoopSong))
	std::unique_ptr<AudioSource> MusicSource(UPlayerPawn* player, UMusic* song, std::unique_ptr<AudioSource> source)
	{
		if (!source)
		{
			songFinished.reset();
			return source;
		}
		bool loop = !player->BoolValue(GetPlayerMusicProps().bDontLoopSong);
		bool isMp3 = song->Format == "mp3" || song->Format == "mp2";
		songFinished = std::make_shared<std::atomic<bool>>(false);
		return std::make_unique<SongSource>(std::move(source), isMp3 ? song->Data : Array<uint8_t>(), isMp3, loop, songFinished);
	}

	// IDA Galaxy.dll: ?Update@UGalaxyAudioSubsystem@@UAEXUFPointRegion@@AAVFCoords@@@Z [HP1 Galaxy 0x106081F0] (bSongFinished at 0x10608F21)
	void TickMusic(UPlayerPawn* player, UMusic* currentSong)
	{
		if (!currentSong)
			songFinished.reset();
		// No music playing: only the flag. A song that played to its end is also cleared.
		bool ended = songFinished && songFinished->load();
		player->BoolValue(GetPlayerMusicProps().bSongFinished) = !songFinished || ended;
		if (ended)
		{
			songFinished.reset();
			player->Transition() = MTRAN_Instant;
			player->Song() = nullptr;
		}
	}

	void RegisterSoundNatives()
	{
		OverrideNative(567, [] { RegisterVMNativeFunc_5("Actor", "ModifySound", &NModifySound, 567); });
		OverrideNative(568, [] { RegisterVMNativeFunc_2("Actor", "StopSound", &NStopSound, 568); });
	}
}
