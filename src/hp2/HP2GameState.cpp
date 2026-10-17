#include "Precomp.h"
#include "HP2.h"
#include "VM/Frame.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/UConsole.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

// HP2's story state screens the actors of a level (docs/re/hp2/engine.md, "GameState"). After a map has loaded and the
// player has logged in, the engine takes PlayerPawn.CurrentGameState and, for every actor, sets Actor.bInCurrentGameState
// (the actor is in the current state unless the state is listed in its ExcludeGameStates string) and raises the script event
// OnResolveGameState. The scripts depend on it: a CutScene starts in its `disabled` state and only the event moves it to
// `Idle`, so no cutscene would ever run without it.

namespace HP2
{
	static std::string Upper(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::toupper(c); });
		return text;
	}

	// A state token is "GSTATE" + 3 digits. True when the comma separated group has no state token or lists `state`.
	static bool GroupAllowsState(const std::string& group, const std::string& state)
	{
		bool hasToken = false;
		size_t pos = 0;
		while (pos <= group.size())
		{
			size_t end = group.find(',', pos);
			if (end == std::string::npos)
				end = group.size();
			const std::string token = group.substr(pos, end - pos);
			if (token.size() == 9 && token.compare(0, 6, "GSTATE") == 0)
			{
				hasToken = true;
				if (token == state)
					return true;
			}
			pos = end + 1;
		}
		return !hasToken;
	}

	// IDA Engine.dll: ?ScreenActorsByGameState@ULevelBase@@QAEXXZ [HP2 not located: no HP2 database on this machine]
	// Written from the script side only: the engine's check of each token against GameStateMasterList and its "GameState
	// ERROR" message are not ported. Every actor gets the event, not only those with tokens (a CutScene has none).
	void RestorePersistentActors(UPlayerPawn* player);

	void ScreenActorsByGameState(UPlayerPawn* player)
	{
		if (!engine->Level || !player)
			return;
		RestorePersistentActors(player);

		UClass* actorClass = engine->packages->FindClass(NameString("Engine.Actor"));
		UClass* playerClass = engine->packages->FindClass(NameString("Engine.PlayerPawn"));
		if (!actorClass || !playerClass)
			return;
		const PropertyDataOffset currentState = playerClass->GetPropertyDataOffset(NameString("CurrentGameState"));
		const PropertyDataOffset excludeStates = actorClass->GetPropertyDataOffset(NameString("ExcludeGameStates"));
		const PropertyDataOffset inState = actorClass->GetPropertyDataOffset(NameString("bInCurrentGameState"));
		if (currentState.DataOffset == (size_t)-1 || excludeStates.DataOffset == (size_t)-1 || inState.DataOffset == (size_t)-1)
			return;

#ifdef __ANDROID__
		// Development: a first line of /sdcard/FlipendoHP/start_state.txt (GSTATE020) is the story state of the first level
		// loaded, to try a map in a state it normally gets from the story (start_level.txt picks the map).
		static bool stateFileDone = false;
		if (!stateFileDone)
		{
			stateFileDone = true;
			std::ifstream file("/sdcard/FlipendoHP/start_state.txt");
			std::string token;
			if (file && std::getline(file, token))
			{
				while (!token.empty() && (token.back() == '\r' || token.back() == ' '))
					token.pop_back();
				if (!token.empty())
					CallEvent(player, NameString("SetGameState"), { ExpressionValue::StringValue(token) });
			}
		}
#endif

		const PropertyDataOffset groupOffset = actorClass->GetPropertyDataOffset(NameString("Group"));
		const std::string state = Upper(player->Value<std::string>(currentState));

		// the events may spawn actors, which are not screened (they begin play in the current state)
		const size_t count = engine->Level->Actors.size();
		for (size_t i = 0; i < count; i++)
		{
			UActor* actor = engine->Level->Actors[i];
			if (!actor)
				continue;

			bool in = true;
			if (!state.empty())
			{
				const std::string excluded = Upper(actor->Value<std::string>(excludeStates));
				if (!excluded.empty() && excluded.find(state) != std::string::npos)
					in = false;

				// The editor's Group field carries the story state an actor belongs to ("None,GSTATE000"): an actor
				// with GSTATE tokens exists only in those states (PrivetDr: the intro in GSTATE000, the Diagon Alley
				// scene in GState010; without this both cutscenes start together on load).
				if (in && groupOffset.DataOffset != (size_t)-1)
					in = GroupAllowsState(Upper(actor->Value<NameString>(groupOffset).ToString()), state);
			}
			actor->BoolValue(inState) = in;
			CallEvent(actor, NameString("OnResolveGameState"));
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

// Persistent actors across a level change (bPersistent, "SavePActors"). harry.LoadLevel copies each persistent Characters'
// state into three script properties (PersistentState = GetStateName(), PersistentLeadingActor = the name of the actor it
// follows, PersistentNavPName) and sends the console command SavePActors; the next level's Characters.PostBeginPlay reads the
// three back (InitialState = PersistentState, LeadingActor and navP looked up by name). The engine carried them over; without
// it Ron stood where the new level placed him instead of following Harry. Done here by actor name: the values are stored
// on SavePActors and put into the actor of the same name after the next level has loaded, with what PostBeginPlay would do.
// NOT reversed: how the original stores them (a save of the actors' properties; spawned persistent actors such as beans are
// not carried here).
namespace HP2
{
	struct PersistentRecord
	{
		std::string state, leading, nav;
	};
	static std::map<std::string, PersistentRecord> persistentActors;

	static std::string Lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return text;
	}

	void SavePersistentActors()
	{
		persistentActors.clear();
		if (!engine->Level)
			return;
		for (UActor* actor : engine->Level->Actors)
		{
			if (!actor || !actor->Class)
				continue;
			const PropertyDataOffset persistent = actor->Class->GetPropertyDataOffset(NameString("bPersistent"));
			const PropertyDataOffset state = actor->Class->GetPropertyDataOffset(NameString("PersistentState"));
			const PropertyDataOffset leading = actor->Class->GetPropertyDataOffset(NameString("PersistentLeadingActor"));
			const PropertyDataOffset nav = actor->Class->GetPropertyDataOffset(NameString("PersistentNavPName"));
			if (persistent.DataOffset == (size_t)-1 || state.DataOffset == (size_t)-1 || leading.DataOffset == (size_t)-1 || nav.DataOffset == (size_t)-1)
				continue;
			if (!actor->BoolValue(persistent))
				continue;
			PersistentRecord record;
			record.state = actor->Value<NameString>(state).ToString();
			record.leading = actor->Value<NameString>(leading).ToString();
			record.nav = actor->Value<NameString>(nav).ToString();
			persistentActors[Lower(actor->Name.ToString())] = record;
		}
		LogMessage("HP2 SavePActors: " + std::to_string(persistentActors.size()) + " persistent actors kept");
	}

	// After the next level has loaded: what Characters.PostBeginPlay does with the three values.
	void RestorePersistentActors(UPlayerPawn* player)
	{
		if (persistentActors.empty() || !engine->Level)
			return;
		const std::map<std::string, PersistentRecord> records = std::move(persistentActors);
		persistentActors.clear();

		const size_t count = engine->Level->Actors.size();
		for (size_t i = 0; i < count; i++)
		{
			UActor* actor = engine->Level->Actors[i];
			if (!actor || !actor->Class)
				continue;
			auto it = records.find(Lower(actor->Name.ToString()));
			if (it == records.end())
				continue;
			const PropertyDataOffset persistent = actor->Class->GetPropertyDataOffset(NameString("bPersistent"));
			const PropertyDataOffset state = actor->Class->GetPropertyDataOffset(NameString("PersistentState"));
			const PropertyDataOffset leading = actor->Class->GetPropertyDataOffset(NameString("PersistentLeadingActor"));
			const PropertyDataOffset nav = actor->Class->GetPropertyDataOffset(NameString("PersistentNavPName"));
			const PropertyDataOffset leadingActor = actor->Class->GetPropertyDataOffset(NameString("LeadingActor"));
			const PropertyDataOffset navP = actor->Class->GetPropertyDataOffset(NameString("navP"));
			if (persistent.DataOffset == (size_t)-1 || state.DataOffset == (size_t)-1 || !actor->BoolValue(persistent))
				continue;

			const PersistentRecord& record = it->second;
			actor->Value<NameString>(state) = NameString(record.state);
			actor->Value<NameString>(leading) = NameString(record.leading);
			actor->Value<NameString>(nav) = NameString(record.nav);

			// PostBeginPlay only carries a following or idle character over (None is stateIdle's default too)
			const std::string s = Lower(record.state);
			if (s != "stateidle" && s != "stateleadingactorpause" && s != "stateleadingactor")
				continue;

			UActor* leader = nullptr;
			UActor* navPoint = nullptr;
			const std::string wantLeader = Lower(record.leading), wantNav = Lower(record.nav);
			for (UActor* other : engine->Level->Actors)
			{
				if (!other)
					continue;
				const std::string name = Lower(other->Name.ToString());
				if (!wantLeader.empty() && name == wantLeader)
					leader = other;
				if (!wantNav.empty() && name == wantNav)
					navPoint = other;
			}
			// Harry's actor is named after the level's own numbering (Harry1 here, Harry0 there)
			if (!leader && wantLeader.rfind("harry", 0) == 0)
				leader = player;

			if (leadingActor.DataOffset != (size_t)-1 && leader)
				actor->Value<UObject*>(leadingActor) = leader;
			if (navP.DataOffset != (size_t)-1 && navPoint)
				actor->Value<UObject*>(navP) = navPoint;
			if (!record.state.empty() && record.state != "None")
				CallEvent(actor, NameString("GotoState"), { ExpressionValue::NameValue(NameString(record.state)) });
			LogMessage("HP2 persistent actor " + actor->Name.ToString() + " back in " + record.state + " leading " + record.leading);
		}
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
