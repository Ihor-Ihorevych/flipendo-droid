#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "Utils/Array.h"

class AudioDevice;
class ULevel;

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
	// [Engine.GameEngine] UseSound=False (the front end's "Disable all sound"): HP2's engine then has no audio subsystem;
	// here sounds and music play at volume 0.
	bool SoundDisabled();

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
