#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "Utils/Array.h"

class AudioDevice;
class ULevel;
class UActor;
class UPawn;
class BspSurface;
struct PlayingSound;
template<typename T> struct vec3T;
typedef vec3T<float> vec3;

// Harry Potter 2 only: what HP2's engine has that HP1's doesn't (docs/one-engine.md). The shared KnowWonder engine code is
// in src/knowwonder/KW.h, HP1-only code in src/hp1/HP1.h. Nothing here runs yet: IsKnowWonder() is still HP1 only (GameFolder.h).

namespace HP2
{
	// Called at the end of KW::RegisterNatives() when HP2 runs: HP2-only natives, and the natives whose script
	// signature changed in HP2 (they override src/knowwonder/'s HP1-signature registration at the same index).
	void RegisterNatives();

	// HP2's music (src/hp2/HP2Music.cpp): Actor.PlayMusic / StopMusic / StopAllMusic, and the per-frame fade that
	// KW::UpdateMusic hands over to. Returns the music volume (0..1).
	void RegisterMusicNatives();
	float UpdateMusic(AudioDevice* device, uint8_t musicVolume);
	// ALAudio's console commands: MUTESOUNDS / UNMUTESOUNDS (HPConsole sends them while a skipped cutscene fast-forwards;
	// while muted the sound effects play at volume 0, the music goes on) and SETVOLUMES SOUND=<f> MUSIC=<f> (the options page).
	bool AudioCommand(const Array<std::string>& args);
	// MUTESOUNDS, or the front end's "Disable all sound"
	bool SoundsMuted();
	// HP2's MusicVolume / SoundVolume ini values are floats 0..1 (ALAudio), the audio device's bytes 0..255.
	// USurrealAudioDevice reads and writes its ini volumes through these.
	uint8_t VolumeFromIni(const std::string& value, uint8_t defaultValue);
	std::string VolumeToIni(uint8_t volume);
	// What the engine loads its audio settings from on the first run, before it has its own ini section.
	constexpr const char* AudioIniSection = "ALAudio.ALAudioSubsystem";
	constexpr const char* RenderIniSection = "D3DDrv.D3DRenderDevice";

	// After a map loads (not Entry): sets each actor's bInCurrentGameState from the player's CurrentGameState and raises
	// OnResolveGameState (src/hp2/HP2GameState.cpp)
	void ScreenActorsByGameState(ULevel* level);

	// src/hp2/HP2Tick.cpp: ULevel::TickActor ticks TickBefore(actor, 0..2) before the actor: its TickParent, its
	// TickParent2, then its Owner (unless the actor is one of the owner's tick parents). UActor::Destroy calls
	// ClearTickParents: no actor keeps the destroyed one as a tick parent.
	UActor* TickBefore(UActor* actor, int i);
	void ClearTickParents(UActor* destroyed);

	// src/hp2/HP2Steps.cpp: after a pawn's physics (KW::PerformPhysics), SavedPrePivotZ eases its mesh over a step
	void SmoothSteps(UPawn* pawn);

	// HP2's front end (src/hp2/HP2FrontEnd.cpp, docs/re/hp2/startup.md): Game.exe's EA and WB splashes and its dialog
	// (New Game, Load Game, Options), before the engine starts. Engine::Run calls it for a run without a URL, before the
	// game window opens. New Game sets the URL to NewGameURL, Load Game leaves it empty (the Startup map) and sets
	// LoadGameSave(); both point the save folder at the slot's. False when the player chose Exit or closed it.
	bool RunFrontEnd();
	// What Game.exe starts the engine with after New Game
	constexpr const char* NewGameURL = "PrivetDr.unr";
	// -LOADGAME=<n>: after the first map, Engine::Run travels to the save folder's Save<n>.usa. -1 for none.
	int LoadGameSave();
	// HP2's saves (src/hp2/HP2Save.cpp): Engine::Run after the level ticks writes a save the player queued
	// (PlayerPawn.bQueuedToSaveGame) as the save folder's Save0.usa; the LOADGAME <n> console command loads Save<n>.usa.
	void TickQueuedSave();
	bool SaveCommand(const Array<std::string>& args);
	// Persistent actors across hub travel (src/hp2/HP2Persist.cpp): SAVEPACTORS writes the level's bPersistent actors to
	// <save folder>/cache/<map>_pa.usa, Engine::LoadMap reads them back for every map but Entry, before the GameInfo
	// spawns. Saving copies the cache into the slot folder (KW::SaveGame), starting the game and LOADGAME copy it back.
	void SavePersistentActors(const std::string& map);
	void LoadPersistentActors(const std::string& map);
	void SavePersistentActorCache();
	void LoadPersistentActorCache();
	// [Engine.GameEngine] UseSound=False (the front end's "Disable all sound"): HP2's engine then has no audio subsystem;
	// here sounds and music play at volume 0.
	bool SoundDisabled();

	// HP2's Lumos surfaces (src/hp2/HP2Lumos.cpp): VisibleFrame::Process marks the view zone's ones near the player's lit
	// Lumos, VisibleNode::Draw asks how to draw one: false skips it, else polyFlags and opacity (SurfaceInfo::Opacity)
	// are what to draw it with.
	void MarkLumosSurfaces(int viewZone);
	bool LumosSurface(const BspSurface& surface, const vec3* points, int count, uint32_t& polyFlags, float& opacity);

	// HP2's lip sync (src/hp2/HP2LipSync.cpp): USurrealAudioDevice's UpdateLipSync and ClearLipSync hand a playing
	// sound over; one with lip sync data animates its actor's mouth_root channel by the sound's amplitude, and puts the
	// mouth to rest when it stops.
	void UpdateLipSync(PlayingSound& playing);
	void StopLipSync(PlayingSound& playing);

	// HP2's "XA" sounds (EA-XA ADPCM, src/hp2/HP2Sound.cpp) decoded into a 16-bit WAV file
	Array<uint8_t> DecodeXA(const Array<uint8_t>& data, int sampleCount, int channels, int rate);

	// HP2's script bytecode (src/hp2/HP2Bytecode.cpp, docs/re/engine/scripting.md). HP2 inserted a DebugInfo token at 0x38
	// and moved GlobalFunction..FloatToBool up by one. ToStockToken maps an HP2 token byte to the stock UE1 value
	// SurrealEngine's ExprToken uses.
	constexpr uint8_t DebugInfoToken = 0x38;
	uint8_t ToStockToken(uint8_t hp2Token);
	// Size in bytes of the DebugInfo record starting at `data` (the 0x38 byte), or 0 if it isn't one.
	size_t DebugInfoSize(const uint8_t* data, size_t available);
}
