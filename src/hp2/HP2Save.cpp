#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "KW.h"
#include "VM/ScriptCall.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include <ctime>
#include <fstream>

// HP2 saves from a queue: PlayerPawn.SaveGame() only sets bQueuedToSaveGame ("the engine will detect that we want a
// save game at the end of the update loop") and the slot page always reads save number 0. At the end of a frame with the
// flag set the engine clears it, tells every actor (PreSaveGame / PostSaveGame, which harry uses) and writes the level.
// NOT reversed: UGameEngine::SaveGame of HP2 (HP2 0x103AB430, changed from HP1) was not read; this writes the same
// level package HP1's save does (KW::SaveGame), as Save0 of the save folder, and reloads it for "LoadGame 0".
namespace HP2
{
	// The slot the player picked in the launcher (0..5): the file Save<slot> is where the queued save goes and "LoadGame"
	// loads from, so the slots are separate games.
	static int activeSlot = 0;

	int ActiveSlot()
	{
		return activeSlot;
	}

	void SetActiveSlot(int slot)
	{
		activeSlot = slot < 0 ? 0 : slot;
	}

	static void RaiseOnAllActors(const char* eventName)
	{
		if (!engine->Level)
			return;
		const size_t count = engine->Level->Actors.size();
		for (size_t i = 0; i < count; i++)
		{
			UActor* actor = engine->Level->Actors[i];
			if (actor)
				CallEvent(actor, NameString(eventName));
		}
	}

	void TickSave()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (!player || !engine->Level)
			return;
		UClass* playerClass = engine->packages->FindClass(NameString("Engine.PlayerPawn"));
		if (!playerClass)
			return;
		static PropertyDataOffset queued = playerClass->GetPropertyDataOffset(NameString("bQueuedToSaveGame"));
		if (queued.DataOffset == (size_t)-1 || !player->BoolValue(queued))
			return;
		player->BoolValue(queued) = false;

		RaiseOnAllActors("PreSaveGame");
		KW::SaveGame(activeSlot, std::string());
		RaiseOnAllActors("PostSaveGame");
		LogMessage("HP2 save: Save" + std::to_string(activeSlot) + " written");

		// What the launcher shows for the slot: the map (its picture is System/Thumbnails/<map>.bmp) and the time.
		try
		{
			std::ofstream info(engine->packages->GetSaveFolderPath() / ("Slot" + std::to_string(activeSlot) + ".txt"));
			info << engine->LevelInfo->URL.Map << "\n" << (long long)std::time(nullptr) << "\n";
		}
		catch (...)
		{
		}
	}

	// LoadGame N (harry, SleepingGoyle after a death): the save is opened like a map. The script always says 0; the
	// slot is the one this game was started with.
	bool LoadGame(int)
	{
		int slot = activeSlot;
		std::string url = KW::SaveGameLoadURL("Save" + std::to_string(slot) + "." + engine->packages->GetSaveExtension());
		if (url.empty())
		{
			LogMessage("LoadGame " + std::to_string(slot) + ": no save");
			return false;
		}
		engine->ClientTravel(url, ETravelType::TRAVEL_Absolute, false);
		return true;
	}
}
