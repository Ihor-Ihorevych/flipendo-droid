#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class UPlayerPawn;

// Harry Potter 2 only: what HP2's engine has that HP1's doesn't (docs/one-engine.md). The shared KnowWonder engine code is
// in src/knowwonder/KW.h, HP1-only code in src/hp1/HP1.h. IsKnowWonder() is true for HP1 and HP2 (GameFolder.h).

namespace HP2
{
	// Called at the end of KW::RegisterNatives() when HP2 runs: HP2-only natives, and the natives whose script
	// signature changed in HP2 (they override src/knowwonder/'s HP1-signature registration at the same index).
	void RegisterNatives();

	// HP2's script bytecode (src/hp2/HP2Bytecode.cpp, docs/re/engine/scripting.md). HP2 inserted a DebugInfo token at 0x38
	// and moved GlobalFunction..FloatToBool up by one. ToStockToken maps an HP2 token byte to the stock UE1 value
	// SurrealEngine's ExprToken uses.
	// Whether the running game is HP2, so BytecodeStream (VM/Bytecode.h) reads this table. False for every other game.
	bool BytecodeActive();
	constexpr uint8_t DebugInfoToken = 0x38;
	uint8_t ToStockToken(uint8_t hp2Token);
	// Size in bytes of the DebugInfo record starting at `data` (the 0x38 byte), or 0 if it isn't one.
	size_t DebugInfoSize(const uint8_t* data, size_t available);

	// HP2's "XA" sound format (src/hp2/HP2Sound.cpp): the data as a 16-bit PCM WAV file image.
	std::vector<uint8_t> DecodeXASound(const uint8_t* data, size_t size, int sampleCount, int channels, int frequency);

	// HP2's story-state screening of the actors after a map has loaded (src/hp2/HP2GameState.cpp).
	void ScreenActorsByGameState(UPlayerPawn* player);

	// SavePActors (src/hp2/HP2GameState.cpp): keeps what the persistent characters carry to the next level.
	void SavePersistentActors();

	// A character left with a stopped frame in stateLeadingActor (old saves) is started again; about once a second.
	void RestartStoppedLeaders();

	// Opens the main menu once the first frame has created the book, when the level is startup.unr (per frame).
	void OpenStartMenu();

	// Touch and pad help (src/hp2/HP2Touch.cpp): the overlay's cutscene state each frame, and a Space key-down that
	// skips a cutscene (HP2's own skip is bound to Enter).
	void TickTouch();
	void KeyDown(int key);

	// Console commands HP2's scripts send that the engine answers (src/hp2/HP2Console.cpp). True when handled; `result` is what
	// ConsoleCommand returns to the script.
	bool ConsoleCommand(const std::string& commandline, std::string& result);

	// Reads HP2's float volumes from [ALAudio.ALAudioSubsystem] into the audio device (after the settings load).
	void LoadAudioVolumes();

	// Left and Right strafe on the phone (src/hp2/HP2Console.cpp), after the key bindings are loaded.
	void TweakKeybindings();

	// The queued save (src/hp2/HP2Save.cpp): per frame at the end of the tick, and the LoadGame console command.
	void TickSave();
	bool LoadGame(int slot);

	// The launcher's save slot (0..5) the queued save goes to and LoadGame reads (src/hp2/HP2Save.cpp).
	int ActiveSlot();
	void SetActiveSlot(int slot);
}
