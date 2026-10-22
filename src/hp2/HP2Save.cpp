#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include <ctime>
#include <fstream>
#include "Utils/Logger.h"
#include "Utils/StrTools.h"
#include "Utils/Convert.h"

// HP2's saves (docs/re/engine/savegames.md). The scripts never save directly: PlayerPawn.SaveGame() only sets
// bQueuedToSaveGame and the engine saves at the end of the tick, always as save 0 of the slot folder the front end
// chose (Save/Slot<n>/Save0.usa). Loading after death is the LoadGame console command.

namespace HP2
{
	// The slot the player picked in the launcher (0..5): the file Save<slot> is where the queued save goes and "LoadGame"
	// loads from, so the slots are separate games. (The desktop front end points the save folder at a slot's instead,
	// and leaves this 0.)
	static int activeSlot = 0;

	int ActiveSlot()
	{
		return activeSlot;
	}

	void SetActiveSlot(int slot)
	{
		activeSlot = slot < 0 ? 0 : slot;
	}

	// UGameEngine::Tick, after the levels tick: a save queued by the viewport's player is written now. The original
	// then runs RESTARTSOUNDS (its streamed sources may have run dry during the save), flushes the engine's caches
	// (nothing to flush here) and leaves the rest of the tick out: no travel and no drawing that frame.
	// IDA Engine.dll: ?Tick@UGameEngine@@UAEXM@Z [HP2 0x103A9910] (the bQueuedToSaveGame test at 0x103A9BB7)
	bool TickQueuedSave()
	{
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (!player || !KW::BoolProperty(player, "bQueuedToSaveGame"))
			return false;
		LogMessage("Saving game...(before call) Filename: " + (engine->packages->GetSaveFolderPath() / ("Save" + std::to_string(activeSlot) + "." + engine->packages->GetSaveExtension())).string());
		player->BoolValue(player->GetPropertyDataOffset("bQueuedToSaveGame")) = false;
		KW::SaveGame(activeSlot, {});

		// What the launcher shows for the slot: the map (its picture is System/Thumbnails/<map>.bmp) and the time.
		try
		{
			std::ofstream info(engine->packages->GetSaveFolderPath() / ("Slot" + std::to_string(activeSlot) + ".txt"));
			info << engine->LevelInfo->URL.Map << "\n" << (long long)std::time(nullptr) << "\n";
		}
		catch (...)
		{
		}
		SoundPauseCommand("RESTARTSOUNDS");
		return true;
	}

	// UGameEngine::Exec's LOADGAME <n>: loads the persistent actor cache and travels to the save folder's
	// Save<n>.usa (the original calls LevelInfo.ServerTravel with its path). SAVEPACTORS: the level's persistent
	// actors, under LevelInfo.LevelEnterText (harry.LoadLevel, before it travels).
	// IDA Engine.dll: ?Exec@UGameEngine@@UAEHPBGAAVFOutputDevice@@@Z [HP2 0x103A20F0] (the "LOADGAME" and "SAVEPACTORS" branches)
	bool SaveCommand(const Array<std::string>& args)
	{
		if (args.size() >= 1 && StrTools::equals_ignore_case(args[0], "savepactors"))
		{
			if (engine->LevelInfo)
				SavePersistentActors(engine->LevelInfo->LevelEnterText());
			return true;
		}
		if (args.size() < 2 || !StrTools::equals_ignore_case(args[0], "loadgame"))
			return false;
		if (args[1].empty() || args[1][0] < '0' || args[1][0] > '9')
			return true;
		int save = activeSlot + Convert::to_int32(args[1]);
		LogMessage("Loading game... filename: " + (engine->packages->GetSaveFolderPath() / ("Save" + std::to_string(save) + "." + engine->packages->GetSaveExtension())).string());
		LoadPersistentActorCache();
		engine->ClientTravel("?load=" + std::to_string(save), ETravelType::TRAVEL_Absolute, false);
		return true;
	}

	// The launcher's choice (HP2GameState.cpp OpenStartMenu): the slot's save is opened like a map, after the persistent
	// actor cache is back. The script's LoadGame always says 0; the slot is the one this game was started with.
	bool LoadGame(int)
	{
		std::string url = KW::SaveGameLoadURL("Save" + std::to_string(activeSlot) + "." + engine->packages->GetSaveExtension());
		if (url.empty())
		{
			LogMessage("LoadGame " + std::to_string(activeSlot) + ": no save");
			return false;
		}
		LoadPersistentActorCache();
		engine->ClientTravel(url, ETravelType::TRAVEL_Absolute, false);
		return true;
	}
}
