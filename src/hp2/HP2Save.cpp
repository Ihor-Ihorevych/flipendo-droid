#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/Logger.h"
#include "Utils/StrTools.h"
#include "Utils/Convert.h"

// HP2's saves (docs/re/engine/savegames.md). The scripts never save directly: PlayerPawn.SaveGame() only sets
// bQueuedToSaveGame and the engine saves at the end of the tick, always as save 0 of the slot folder the front end
// chose (Save/Slot<n>/Save0.usa). Loading after death is the LoadGame console command.

namespace HP2
{
	// UGameEngine::Tick, after the levels tick: a save queued by the viewport's player is written now. The original
	// then runs RestartSounds, flushes the engine's caches and leaves the rest of the tick out.
	// IDA Engine.dll: ?Tick@UGameEngine@@UAEXM@Z [HP2 0x103A9910] (the bQueuedToSaveGame test at 0x103A9BB7)
	void TickQueuedSave()
	{
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (!player || !KW::BoolProperty(player, "bQueuedToSaveGame"))
			return;
		LogMessage("Saving game...(before call) Filename: " + (engine->packages->GetSaveFolderPath() / ("Save0." + engine->packages->GetSaveExtension())).string());
		player->BoolValue(player->GetPropertyDataOffset("bQueuedToSaveGame")) = false;
		KW::SaveGame(0, {});
	}

	// UGameEngine::Exec's LOADGAME <n>: travels to the save folder's Save<n>.usa (the original calls
	// LevelInfo.ServerTravel with its path, after loading the persistent actor cache, which isn't ported).
	// IDA Engine.dll: ?Exec@UGameEngine@@UAEHPBGAAVFOutputDevice@@@Z [HP2 0x103A20F0] (the "LOADGAME" branch)
	bool SaveCommand(const Array<std::string>& args)
	{
		if (args.size() < 2 || !StrTools::equals_ignore_case(args[0], "loadgame"))
			return false;
		if (args[1].empty() || args[1][0] < '0' || args[1][0] > '9')
			return true;
		int save = Convert::to_int32(args[1]);
		LogMessage("Loading game... filename: " + (engine->packages->GetSaveFolderPath() / ("Save" + std::to_string(save) + "." + engine->packages->GetSaveExtension())).string());
		engine->ClientTravel("?load=" + std::to_string(save), ETravelType::TRAVEL_Absolute, false);
		return true;
	}
}
