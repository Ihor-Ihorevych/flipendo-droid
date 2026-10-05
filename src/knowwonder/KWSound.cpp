#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/UMusic.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/UViewport.h"
#include "KWActor.h"
#include "KWCheck.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "Audio/AudioSource.h"
#include "Audio/AudioDevice.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include "VM/NativeFunc.h"
#include "Engine.h"

// Actor.PlaySound: the sound reaches the audio device only through the players that hear it (CheckHearSound,
// ClientHearSound), each with its own distance and BSP occlusion test.
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

	// IDA Engine.dll: ?execClientHearSound@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x10407F30]
	// Only a PlayerPawn with a local viewport plays it. Parameters = (Volume * 100, Radius, Pitch * 100), a zero
	// radius plays at 1600. A sound of an actor being destroyed plays unattached (at SoundLocation).
	static void ClientHearSound(UPawn* hearer, UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters)
	{
		UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(hearer);
		if (!player || !UObject::TryCast<UViewport>(player->Player()) || !engine->audiodev)
			return;
		if (actor && actor->bDeleteMe())
			actor = nullptr;
		float radius = parameters.y != 0.0f ? parameters.y : 1600.0f;
		engine->audiodev->PlaySound(actor, id, sound, soundLocation, parameters.x * 0.01f, radius, parameters.z * 0.01f, (id & 14) == SLOT_Talk * 2);
	}

	static void NClientHearSound(UObject* Self, UObject* Actor, int Id, UObject* S, const vec3& SoundLocation, const vec3& Parameters)
	{
		ClientHearSound(UObject::Cast<UPawn>(Self), UObject::TryCast<UActor>(Actor), Id, UObject::TryCast<USound>(S), SoundLocation, Parameters);
	}

	// IDA Engine.dll: ?CheckHearSound@AActor@@QAEXPAVAPawn@@HPAVUSound@@VFVector@@M@Z [HP1 0x1040AB90]
	// A player hears from its ViewTarget (Harry's camera) when it has one. Within 1/1.3 of the radius squared
	// (0.877 radius) a sound with a clear BSP line plays as it is; through BSP it plays at 0.35 volume and only
	// within 0.6 of that range squared (0.679 radius), unless the hearer is the sound's Instigator.
	// The original raises the ClientHearSound event; Pawn.ClientHearSound is a native event no script overrides,
	// so its body is called directly.
	static void CheckHearSound(UActor* source, UPawn* hearer, int id, USound* sound, vec3 parameters, float radiusSquared)
	{
		UActor* listener = hearer;
		if (UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(hearer))
		{
			if (player->ViewTarget())
				listener = player->ViewTarget();
		}

		float range = radiusSquared * 0.76923078f;
		vec3 d = listener->Location() - source->Location();
		float distSquared = dot(d, d);
		if (distSquared >= range)
			return;

		if (!ModelFastLineCheck(source->XLevel()->Model, listener->Location(), source->Location()))
		{
			if (source->Instigator() != hearer)
				range *= 0.6f;
			parameters.x *= 0.35f;
			if (distSquared > range)
				return;
		}
		ClientHearSound(hearer, source, id, sound, source->Location(), parameters);
	}

	// IDA Engine.dll: ?execPlaySound@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040B000]
	// IDA Engine.dll: ?execPlayOwnedSound@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040B890]
	// Every bIsPlayer pawn may hear it (CheckHearSound). The original asks the viewports instead on a network
	// client or from a simulated function, and PlayOwnedSound leaves out a remote owner; in a standalone game
	// both come to the same player. DemoPlaySound (demo recording) is left out.
	// The Id is SoundId (the original packs the actor's object index the same way) plus bNoOverride in bit 0.
	// TODO HP2: PlaySound gained Disable3D and Loop (ALAudio.dll, not reversed yet); HP1 passes neither.
	void PlaySound(UActor* actor, USound* sound, uint8_t slot, float volume, bool noOverride, float radius, float pitch, bool disable3D, bool loop)
	{
		if (!sound)
			return;
		int id = SoundId(actor, slot) + (noOverride ? 1 : 0);
		float playRadius = radius != 0.0f ? radius : 1600.0f;
		vec3 parameters(volume * 100.0f, radius, pitch * 100.0f);
		for (UPawn* pawn = actor->Level()->PawnList(); pawn; pawn = pawn->nextPawn())
		{
			if (pawn->bIsPlayer())
				CheckHearSound(actor, pawn, id, sound, parameters, playRadius * playRadius);
		}
	}

	static void NPlaySound(UObject* Self, UObject* Sound, std::optional<uint8_t> Slot, std::optional<float> Volume, std::optional<bool> bNoOverride, std::optional<float> Radius, std::optional<float> Pitch)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		PlaySound(actor, UObject::TryCast<USound>(Sound), Slot.value_or(SLOT_Misc), Volume.value_or(actor->TransientSoundVolume()), bNoOverride.value_or(false),
			Radius.value_or(actor->TransientSoundRadius()), Pitch.value_or(TransientSoundPitch(actor) / 64.0f), false, false);
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
		OverrideNative(264, [] { RegisterVMNativeFunc_6("Actor", "PlaySound", &NPlaySound, 264); });
		RegisterVMNativeFunc_6("Actor", "PlayOwnedSound", &NPlaySound, 0);
		RegisterVMNativeFunc_5("Pawn", "ClientHearSound", &NClientHearSound, 0);
		OverrideNative(567, [] { RegisterVMNativeFunc_5("Actor", "ModifySound", &NModifySound, 567); });
		OverrideNative(568, [] { RegisterVMNativeFunc_2("Actor", "StopSound", &NStopSound, 568); });
	}
}
