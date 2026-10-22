#include "Precomp.h"
#include "HP2.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"


#include "VM/Frame.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/UConsole.h"
#include <algorithm>
#include <cctype>
#include <fstream>

// HP2's GameState: the story state screens which actors take part in a map (docs/re/hp2/engine.md). The player's
// PlayerPawn.CurrentGameState is one of the tokens in GameStateMasterList ("GSTATE000,GSTATE010,..."); an actor lists
// the states it belongs to in its Group name ("None,GSTATE000") and the ones it stays out of in ExcludeGameStates. After
// a map loads the engine sets each actor's bInCurrentGameState and raises OnResolveGameState, where CutScene,
// Characters, CreatureGenerator and others enable or disable themselves.

namespace HP2
{
	namespace
	{
		std::string Upper(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)toupper(c); });
			return s;
		}

		const std::string& StringProperty(UObject* obj, const char* name)
		{
			static const std::string empty;
			return obj->HasProperty(name) ? *static_cast<const std::string*>(obj->GetProperty(name)) : empty;
		}
	}

	// IDA Engine.dll: ?ScreenActorsByGameState@ULevel@@QAEXXZ [HP2 0x103B86D0]
	// Called by UGameEngine::LoadMap for every map but Entry, after the player has logged in. The player is the first
	// bIsPlayer pawn in the actor list (KnowWonder's maps place Harry, with the state saved in the map: PrivetDr's
	// Harry1 has "gstate000"). With the state "None" nothing is screened and no event is raised (a new game outside
	// such a map). The command line's GAMESTATE=<token> (here --gamestate=<token>) then replaces a state that isn't
	// "None" when the master list has the token: the player's CurrentGameState becomes it, in capitals.
	void ScreenActorsByGameState(ULevel* level)
	{
		UPawn* player = nullptr;
		for (UActor* actor : level->Actors)
		{
			UPawn* pawn = UObject::TryCast<UPawn>(actor);
			if (pawn && pawn->bIsPlayer())
			{
				player = pawn;
				break;
			}
		}

		std::string current = player ? Upper(StringProperty(player, "CurrentGameState")) : std::string();
		if (current == "NONE")
			return;
		if (!player)
		{
			LogMessage("**** Can't find a valid player!!!! PP == NULL ");
			return;
		}

		const std::string& masterList = StringProperty(player, "GameStateMasterList");
		std::string forced = commandline->GetArg("", "--gamestate", "");
		if (!forced.empty() && masterList.find(forced) != std::string::npos)
		{
			current = Upper(forced);
			*static_cast<std::string*>(player->GetProperty("CurrentGameState")) = current;
		}
		int tokenLen = player->HasProperty("GameStateTokenLen") ? *static_cast<int32_t*>(player->GetProperty("GameStateTokenLen")) : 0;
		if (masterList.find(current) == std::string::npos)
		{
			LogMessage("**** Caution: The player pawn has an Invalid CurrentGameState for the GameStateMasterList: " + current);
			return;
		}

		for (UActor* actor : level->Actors)
		{
			if (!actor)
				continue;

			std::string tokens = Upper(static_cast<NameString*>(actor->GetProperty("Group"))->ToString());
			std::string exclude = Upper(StringProperty(actor, "ExcludeGameStates"));
			bool screened = false;
			bool inState = false;
			if (!exclude.empty() && exclude.find(current) != std::string::npos)
			{
				screened = true; // excluded: out, tokens or not
			}
			else
			{
				size_t pos;
				while ((pos = tokens.find("GSTATE")) != std::string::npos)
				{
					screened = true;
					tokens = tokens.substr(pos);
					std::string token = tokens.substr(0, tokens.find(','));
					if ((int)token.size() == tokenLen && masterList.find(token) != std::string::npos)
					{
						if (token == current)
						{
							inState = true;
							break;
						}
					}
					else
					{
						LogMessage("*** GameState ERROR Actor: " + actor->Name.ToString() + " has an invalid GameState Token: " + token);
					}
					tokens = tokens.substr(token.size());
				}
			}
			if (screened)
				actor->BoolValue(actor->GetPropertyDataOffset("bInCurrentGameState")) = inState;

			CallEvent(actor, "OnResolveGameState");
		}
	}
}

namespace HP2
{
	// HP2's HPConsole creates the FEBook the first time it draws the UWindow root and closes it again at once
	// (menuBook.bGamePlaying = true; CloseBook()). The game only shows its main menu when a level change to startup.unr
	// runs harry.LoadLevel / TriggerChangeLevel (menuBook.bGamePlaying = false; OpenBook("Main"); LaunchUWindow()), which
	// the first boot into startup.unr never does. Done here, once, when the book exists and the level is startup.unr.
	// NOT reversed: what the original does at boot (no HP2 IDA database here); this is what its own travel code does.
	void OpenStartMenu()
	{
		static bool done = false;
		if (done || !engine->console || !engine->LevelInfo)
			return;

		std::string url = Upper(engine->LevelInfo->URL.ToString());
		if (url.find("STARTUP") == std::string::npos)
		{
			done = true; // another map was started directly (--level): no menu
			return;
		}

#ifdef __ANDROID__
		// The launcher's choice (SetupActivity writes /sdcard/FlipendoHP/hp2_start.txt: "new 2" or "load 2", the save slot 0..5):
		// a saved game is loaded, a new game starts in PrivetDr, in that slot. Without the file the main menu opens.
		{
			std::ifstream file("/sdcard/FlipendoHP/hp2_start.txt");
			std::string mode;
			int slot = 0;
			if (file >> mode >> slot)
			{
				done = true;
				SetActiveSlot(slot);
				if (mode == "new") // a restart from inside the game (language, renderer) must not begin again
				{
					std::ofstream again("/sdcard/FlipendoHP/hp2_start.txt");
					again << "load " << slot << "\n";
				}
				if (mode == "load" && LoadGame(slot))
					return;
				engine->ClientTravel("PrivetDr", ETravelType::TRAVEL_Absolute, false);
				return;
			}
		}
#endif

		UClass* consoleClass = engine->packages->FindClass(NameString("HGame.HPConsole"));
		UClass* bookClass = engine->packages->FindClass(NameString("HGame.FEBook"));
		if (!consoleClass || !bookClass)
			return;
		const PropertyDataOffset menuBook = consoleClass->GetPropertyDataOffset(NameString("menuBook"));
		const PropertyDataOffset bGamePlaying = bookClass->GetPropertyDataOffset(NameString("bGamePlaying"));
		if (menuBook.DataOffset == (size_t)-1 || bGamePlaying.DataOffset == (size_t)-1)
		{
			done = true;
			return;
		}

		UObject* book = engine->console->Value<UObject*>(menuBook);
		if (!book)
			return; // not created yet: the first frame with the UWindow root creates it

		done = true;
		book->BoolValue(bGamePlaying) = false;
		CallEvent(book, NameString("OpenBook"), { ExpressionValue::StringValue("Main") });
	}
}

namespace HP2
{
	// Saves made while the state code of a state that extends another (HP2's stateLeadingActor extends patrol) did not run its
	// parent's Begin hold that character with a stopped frame; start the state again so the character leads as it should.
	// Called about once a second (HP2Touch.cpp): a loaded save does not go through ScreenActorsByGameState.
	void RestartStoppedLeaders()
	{
		if (!engine->Level)
			return;
		const size_t count = engine->Level->Actors.size();
		for (size_t i = 0; i < count; i++)
		{
			UActor* actor = engine->Level->Actors[i];
			if (!actor || !actor->StateFrame || actor->StateFrame->GetLatentState() != LatentRunState::Stop || actor->StateFrame->GetStatementIndex() != 0)
				continue;
			const NameString stateName = actor->GetStateName();
			if (stateName != NameString("stateLeadingActor"))
				continue;
			CallEvent(actor, NameString("GotoState"), { ExpressionValue::NameValue(stateName) });
			LogMessage("HP2 restarted " + actor->Name.ToString() + " in " + stateName.ToString() + " (its frame was stopped)");
		}
	}
}
