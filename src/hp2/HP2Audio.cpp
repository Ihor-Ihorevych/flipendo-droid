#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "Audio/AudioDevice.h"
#include "Package/PackageManager.h"
#include "Utils/StrTools.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/USound.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

// HP2's audio driver, ALAudio.dll (docs/re/hp2/engine.md#audio): how far a sound carries (OpenAL's inverse distance
// clamped model), its priority for a channel, the ambient sounds (started and stopped by the driver, not the engine),
// doppler, and the sounds paused with the game. USurrealAudioDevice hands its HP2 work to these.

namespace HP2
{
	namespace
	{
		// ALAudio's flags for a playing sound: USound's own (0x80 lip sync data, 0x8 played at the music volume, 0x4
		// streamed) and PlaySound's (0x2 loop, 0x10 at the listener, not in 3D, 0x20 its pitch follows the actor's)
		constexpr int FlagLoop = 0x2;
		constexpr int FlagNo3D = 0x10;
		constexpr int FlagActorPitch = 0x20;

		// Core.dll's GAudioMaxRadiusMultiplier: how far beyond its radius a sound still counts (priority, ambience)
		constexpr float MaxRadiusMultiplier = 100.0f;
		// ALAudio works in metres, 0.0127 to an Unreal unit, with OpenAL's 343.3 m/s speed of sound
		constexpr float MetresPerUnit = 0.0127f;

		struct AudioState
		{
			double lastTime = -1.0;
			float dt = 0.0001f;
			float ambientTimer = 0.0f;
			bool wasRealtime = false;
			bool wasPaused = false;
			bool paused = false;
			bool restart = false;
			vec3 lastListener = vec3(0.0f);
		};

		AudioState& State()
		{
			static AudioState state;
			return state;
		}

		double Now()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		float IniFloat(const char* key, float defaultValue)
		{
			std::string value = engine->packages->GetIniValue("System", AudioIniSection, key);
			if (value.empty())
				return defaultValue;
			try
			{
				return std::stof(value);
			}
			catch (...)
			{
				return defaultValue;
			}
		}

		bool IsAmbient(int id)
		{
			return (id & 14) == SLOT_Ambient * 2;
		}

		// Who the player hears with: the viewport actor's ViewTarget, else the actor
		UActor* ViewActor(UViewport* viewport)
		{
			UPlayerPawn* player = viewport ? viewport->Actor() : nullptr;
			if (!player)
				return nullptr;
			return player->ViewTarget() ? player->ViewTarget() : player;
		}

		bool OwnedBy(UActor* actor, UActor* owner)
		{
			for (UActor* a = actor; a; a = a->Owner())
			{
				if (a == owner)
					return true;
			}
			return false;
		}

		// IDA ALAudio.dll: ?FindLeastImportantSound@UALAudioSubsystem@@QAEHPAVUSound@@HM@Z [HP2 ALAudio 0x10007180]
		// No channel has a lower priority: the ambient sound playing in the most copies gives up its least important
		// copy, if it is below the new sound; with no ambience playing, any sound below it (an ambient sound may only
		// push out another ambient sound).
		int FindLeastImportantSound(USurrealAudioDevice* audio, int id, float priority)
		{
			Array<PlayingSound>& sounds = audio->GetPlayingSounds();
			USound* most = nullptr;
			int mostCount = 0;
			std::map<USound*, int> copies;
			for (const PlayingSound& p : sounds)
			{
				if (p.Id != 0 && IsAmbient(p.Id))
				{
					int count = ++copies[p.Sound];
					if (count > mostCount)
					{
						mostCount = count;
						most = p.Sound;
					}
				}
			}

			int found = -1;
			float lowest = 1000.0f;
			for (size_t i = 0; i < sounds.size(); i++)
			{
				const PlayingSound& p = sounds[i];
				if (p.Id == 0 || p.Priority >= priority)
					continue;
				if (most ? p.Sound != most : (IsAmbient(id) && !IsAmbient(p.Id)))
					continue;
				float pri = SoundPriority(audio->GetViewport(), p.Location, p.Volume, p.Radius, p.Sound ? (int)p.Sound->HP2Flags : 0);
				if (pri < lowest)
				{
					lowest = pri;
					found = (int)i;
				}
			}
			return found;
		}

		// IDA ALAudio.dll: ?PauseSounds@UALAudioSubsystem@@QAEXXZ [HP2 ALAudio 0x10005250]
		// IDA ALAudio.dll: ?UnpauseSounds@UALAudioSubsystem@@QAEXXZ [HP2 ALAudio 0x10005290]
		void PauseSounds(USurrealAudioDevice* audio, bool pause)
		{
			Array<PlayingSound>& sounds = audio->GetPlayingSounds();
			for (size_t i = 0; i < sounds.size(); i++)
			{
				PlayingSound& p = sounds[i];
				if (p.Id == 0 || p.Paused == pause)
					continue;
				p.Paused = pause;
				if (p.IsActive)
					audio->GetDevice()->SetPaused((int)i, pause);
			}
		}

		struct AmbientEntry
		{
			UActor* actor;
			float priority;
			int flags;
			int id;
			int playing; // channel, or -1 to start
		};

		// The ambience block of UALAudioSubsystem::Update, every 0.2 seconds while the game runs: every actor with an
		// AmbientSound that isn't playing is a candidate, an actor the player views from (or that it owns) at the
		// listener wherever it is, any other within SoundRadius * 100. With the ambient sounds already playing they are
		// grouped by sound and ranked by priority; at most three copies of a sound play (the rest stop) and the top
		// three that aren't playing start. Then every ambient sound out of range, or whose actor changed its
		// AmbientSound, stops.
		void UpdateAmbience(USurrealAudioDevice* audio, bool running, const vec3& listener)
		{
			UViewport* viewport = audio->GetViewport();
			Array<PlayingSound>& sounds = audio->GetPlayingSounds();
			UActor* viewActor = ViewActor(viewport);
			if (running && viewActor)
			{
				std::vector<AmbientEntry> entries;
				int index = 0;
				for (UActor* actor : viewActor->XLevel()->Actors)
				{
					int i = index++;
					if (!actor || !actor->AmbientSound() || actor->bDeleteMe())
						continue;
					int id = i * 16 + SLOT_Ambient * 2;
					if (std::any_of(sounds.begin(), sounds.end(), [&](const PlayingSound& p) { return p.Id == id; }))
						continue;
					float radius = actor->SoundRadius();
					float volume = actor->SoundVolume() / 255.0f;
					int flags = FlagActorPitch | FlagLoop;
					if (OwnedBy(actor, viewActor))
					{
						flags |= FlagNo3D;
					}
					else
					{
						vec3 d = actor->Location() - listener;
						float range = radius * MaxRadiusMultiplier;
						if (dot(d, d) > range * range)
							continue;
					}
					entries.push_back({ actor, SoundPriority(viewport, actor->Location(), volume, radius, flags), flags, id, -1 });
				}
				for (size_t i = 0; i < sounds.size(); i++)
				{
					const PlayingSound& p = sounds[i];
					if (p.Id != 0 && IsAmbient(p.Id) && p.Actor)
						entries.push_back({ p.Actor, p.Priority, p.Flags, p.Id, (int)i });
				}

				std::stable_sort(entries.begin(), entries.end(), [](const AmbientEntry& a, const AmbientEntry& b) { return a.actor->AmbientSound() < b.actor->AmbientSound(); });
				size_t start = 0;
				while (start < entries.size())
				{
					size_t end = start;
					USound* sound = entries[start].actor->AmbientSound();
					while (end < entries.size() && entries[end].actor->AmbientSound() == sound)
						end++;
					std::stable_sort(entries.begin() + start, entries.begin() + end, [](const AmbientEntry& a, const AmbientEntry& b) { return a.priority > b.priority; });
					for (size_t k = start + 3; k < end; k++)
					{
						if (entries[k].playing != -1)
							audio->StopSoundAt(entries[k].playing);
					}
					for (size_t k = start; k < end && k < start + 3; k++)
					{
						AmbientEntry& e = entries[k];
						if (e.playing != -1)
							continue;
						UActor* a = e.actor;
						bool no3D = (e.flags & FlagNo3D) != 0;
						if (!audio->PlaySound(a, e.id, sound, a->Location(), a->SoundVolume() / 255.0f, a->SoundRadius(), a->SoundPitch() / 64.0f, false, no3D, true))
							break;
					}
					start = end;
				}
			}

			for (size_t i = 0; i < sounds.size(); i++)
			{
				PlayingSound& p = sounds[i];
				if (p.Id == 0 || !IsAmbient(p.Id) || !p.Actor)
					continue;
				vec3 d = p.Actor->Location() - listener;
				float range = p.Radius * MaxRadiusMultiplier;
				if (dot(d, d) > range * range || p.Actor->AmbientSound() != p.Sound)
					audio->StopSoundAt(i);
			}
		}
	}

	// IDA ALAudio.dll: ?Init@UALAudioSubsystem@@UAEHXZ [HP2 ALAudio 0x10002F40] (alDistanceModel, alDopplerFactor)
	// IDA ALAudio.dll: ?PlaySound@UALAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@VFVector@@MMMHMM@Z [HP2 ALAudio 0x100040F0] (the source's settings)
	// The reference distance is the sound's radius, the rolloff [ALAudio.ALAudioSubsystem] RollOff (1 when 0 or less),
	// no maximum distance; DopplerFactor from the same section.
	void InitAudio(AudioDevice* device)
	{
		float rolloff = IniFloat("RollOff", 1.0f);
		if (rolloff <= 0.0f)
			rolloff = 1.0f;
		device->SetInverseFalloff(rolloff, IniFloat("DopplerFactor", 1.0f), 343.3f / MetresPerUnit);
	}

	// IDA ALAudio.dll: ?Init@UALAudioSubsystem@@UAEHXZ [HP2 ALAudio 0x10002F40] (the sources)
	// [ALAudio.ALAudioSubsystem] Channels, at most 32
	int AudioChannels()
	{
		return std::clamp((int)IniFloat("Channels", 32.0f), 1, 32);
	}

	// IDA ALAudio.dll: ?SoundPriority@UALAudioSubsystem@@QAEMPAVUViewport@@VFVector@@MMH@Z [HP2 ALAudio 0x10006F10]
	// Volume times how close it is (1 - distance² / (radius * 100)², from 0.01 to 1; 1 with no radius), plus 4 for
	// lip sync data, 2 for a sound at the music volume, 1 for one at the listener
	float SoundPriority(UViewport* viewport, const vec3& location, float volume, float radius, int flags)
	{
		float closeness = 1.0f;
		UActor* viewActor = ViewActor(viewport);
		if (radius != 0.0f && viewActor)
		{
			vec3 d = viewActor->Location() - location;
			float range = radius * MaxRadiusMultiplier;
			closeness = std::clamp(1.0f - dot(d, d) / (range * range), 0.01f, 1.0f);
		}
		return ((flags >> 5) & 4) + ((flags & FlagNo3D) ? 1 : 0) + ((flags >> 2) & 2) + closeness * volume;
	}

	int SoundFlags(USound* sound, int id, bool loop, bool no3D)
	{
		int flags = sound ? (int)sound->HP2Flags : 0;
		if (loop)
			flags |= FlagLoop;
		if (no3D)
			flags |= FlagNo3D;
		if (IsAmbient(id))
			flags |= FlagActorPitch;
		return flags;
	}

	// IDA ALAudio.dll: ?PlaySound@UALAudioSubsystem@@UAEHPAVAActor@@HPAVUSound@@VFVector@@MMMHMM@Z [HP2 ALAudio 0x100040F0] (the channel)
	// A sound in the Talk slot has priority 100. The channel playing the same actor and slot is taken (unless the new
	// one is bNoOverride: then it doesn't play), else the lowest priority channel below the new sound's, else
	// FindLeastImportantSound. -1: no channel.
	int ChooseSoundChannel(USurrealAudioDevice* audio, int id, float& priority)
	{
		if ((id & 14) == SLOT_Talk * 2)
			priority = 100.0f;
		Array<PlayingSound>& sounds = audio->GetPlayingSounds();
		int found = -1;
		float lowest = priority;
		for (size_t i = 0; i < sounds.size(); i++)
		{
			const PlayingSound& p = sounds[i];
			if (((p.Id ^ id) & ~1) == 0)
			{
				if (id & 1)
					return -1;
				return (int)i;
			}
			if (p.Priority < lowest)
			{
				lowest = p.Priority;
				found = (int)i;
			}
		}
		if (found == -1)
			found = FindLeastImportantSound(audio, id, priority);
		return found;
	}

	// IDA ALAudio.dll: ?Update@UALAudioSubsystem@@UAEXPAUFSceneNode@@@Z [HP2 ALAudio 0x100052D0] (pause, ambience, listener velocity)
	// The sounds pause when the viewport stops being realtime or the game is paused, and go on when either ends (or
	// after RESTARTSOUNDS).
	void UpdateAudio(USurrealAudioDevice* audio)
	{
		AudioState& s = State();
		double now = Now();
		float dt = s.lastTime < 0.0 ? 0.0001f : std::clamp((float)(now - s.lastTime), 0.0001f, 1.0f);
		s.lastTime = now;
		s.dt = dt;

		UViewport* viewport = audio->GetViewport();
		if (!viewport || !viewport->Actor())
			return;

		bool realtime = viewport->IsRealtime();
		bool paused = !viewport->Actor()->Level()->Pauser().empty();
		if ((!realtime && s.wasRealtime) || (paused && !s.wasPaused))
		{
			PauseSounds(audio, true);
			s.paused = true;
		}
		else if ((realtime && !s.wasRealtime) || (!paused && s.wasPaused) || s.restart)
		{
			PauseSounds(audio, false);
			s.paused = false;
			s.restart = false;
		}
		s.wasRealtime = realtime;
		s.wasPaused = paused;

		vec3 listener = engine->CameraLocation;
		s.ambientTimer += dt;
		if (s.ambientTimer > 0.2f)
		{
			s.ambientTimer = 0.0f;
			UpdateAmbience(audio, realtime && !paused, listener);
		}

		// The listener's velocity from how far the camera moved, kept when it jumps (50 m/s or more)
		vec3 velocity = (listener - s.lastListener) * (1.0f / dt);
		s.lastListener = listener;
		if (length(velocity) * MetresPerUnit < 50.0f)
			audio->GetDevice()->SetListenerVelocity(velocity);
	}

	// IDA ALAudio.dll: ?Update@UALAudioSubsystem@@UAEXPAUFSceneNode@@@Z [HP2 ALAudio 0x100052D0] (each sound)
	// An ambient sound takes its actor's SoundVolume and SoundPitch every frame; a sound of an actor with a light is
	// scaled by its LightBrightness (a flickering torch's sound flickers with it). A sound not at the listener moves
	// with its actor's velocity (kept when it is 300 m/s or more).
	float UpdateSound(USurrealAudioDevice* audio, int channel, PlayingSound& playing, float volume)
	{
		UActor* actor = playing.Actor;
		if (!actor)
			return volume;
		if (playing.Flags & FlagActorPitch)
			playing.Pitch = actor->SoundPitch() / 64.0f;
		if (IsAmbient(playing.Id))
		{
			playing.Volume = actor->SoundVolume() / 255.0f;
			volume = playing.Volume;
		}
		if (actor->LightType() != 0)
			volume *= actor->LightBrightness() / 255.0f;
		if (!(playing.Flags & FlagNo3D) && length(actor->Velocity()) * MetresPerUnit < 300.0f)
			audio->GetDevice()->SetSoundVelocity(channel, actor->Velocity());
		return volume;
	}

	// IDA ALAudio.dll: ?Exec@UALAudioSubsystem@@UAEHPBGAAVFOutputDevice@@@Z [HP2 ALAudio 0x100067B0] (PAUSESOUNDS,
	// UNPAUSESOUNDS, RESTARTSOUNDS)
	// PAUSESOUNDS and UNPAUSESOUNDS pause and go on at once; RESTARTSOUNDS (the engine's, after a queued save) goes on
	// at the next update unless the game pauses then
	bool SoundPauseCommand(const std::string& command)
	{
		USurrealAudioDevice* audio = engine->audiodev;
		if (StrTools::equals_ignore_case(command, "PAUSESOUNDS"))
		{
			if (audio)
				PauseSounds(audio, true);
		}
		else if (StrTools::equals_ignore_case(command, "UNPAUSESOUNDS"))
		{
			if (audio)
				PauseSounds(audio, false);
		}
		else if (StrTools::equals_ignore_case(command, "RESTARTSOUNDS"))
		{
			State().restart = true;
		}
		else
		{
			return false;
		}
		return true;
	}

	// While its sound is paused its lip sync waits too (ALAudio moves the lip sync's start on by the paused time)
	bool SoundPaused(PlayingSound& playing)
	{
		if (!playing.Paused)
			return false;
		if (playing.LipSyncStart >= 0.0)
			playing.LipSyncStart += State().dt;
		return true;
	}
}
