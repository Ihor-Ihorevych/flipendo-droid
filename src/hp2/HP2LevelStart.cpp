#include "Precomp.h"
#include "HP2.h"
#include "Packages/Core/UObject.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Package/PackageManager.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include <algorithm>
#include <cstdlib>

// --level=<map> for HP2 (docs/debug-tools.md): Harry gets the spells the story has taught him by the time he reaches
// the map (issue #41). harry.PreBeginPlay gives every new Harry Flipendo, Lumos and Alohomora; the other four are
// learned in the lessons (SpellLessonTrigger.EndLesson), and the lesson's end cutscene moves the story on and sends
// Harry to its challenge:
// - Rictusempra: DADA in Grandstaircase_hub, 02080DADARictaEnd -> GSTATE030, Ch1Rictusempra;
// - Skurge: Charms in Grandstaircase_hub, 05060CharmSkurgEnd -> GSTATE060, Ch2Skurge;
// - Diffindo: Herbology in Grounds_hub, 08060HerbDiffEnd -> GSTATE090, Ch3Diffindo;
// - Spongify: Grandstaircase_hub, 13060SpongeEnd -> GSTATE140, Ch4Spongify.
// The duelling spells are never added in the story (the duels select them with bForceSelection).
// SpellBook is a `travel` property, so the spells stay with Harry into the next maps.
//
// The story state Harry reaches the map in is --gamestate's, else the map's below (from the cutscenes that change the
// state and load the map, and the hub level-start cutscenes that open its entrance), else his own CurrentGameState.
// Without one (the hubs place Harry with "None": every state's actors take part) he gets all four.

namespace HP2
{
	namespace
	{
		struct MapState
		{
			const char* map;
			int state; // GSTATExxx
		};

		const MapState EntryStates[] =
		{
			{ "PrivetDr", 0 },
			{ "FlyingFordCutScene", 0 },
			{ "Adv1Willow", 0 },
			{ "Ch1Rictusempra", 30 },	// 02080DADARictaEnd
			{ "Quidditch_Intro", 30 },
			{ "Ch2Skurge", 60 },		// 05060CharmSkurgEnd
			{ "Adv3DungeonQuest", 65 },	// LevelStart_EntryHallHub_GState065 opens its bars
			{ "Arena", 70 },
			{ "Ch3Diffindo", 90 },		// 08060HerbDiffEnd
			{ "Adv4Greenhouse", 95 },
			{ "Adv6Goyle", 100 },		// 11040BitOfGoyle -> GSTATE110 on the way out
			{ "Adv7SlythComRoom", 120 },
			{ "Ch4Spongify", 140 },		// 13060SpongeEnd
			{ "Ch6WizardCard", 140 },	// bonus challenges: Spongify pads
			{ "Ch7Gryffindor", 140 },
			{ "Adv8Forest", 150 },
			{ "Adv9Aragog", 150 },
			{ "Sepia_Hallway", 150 },
			{ "Grounds_Night", 150 },
			{ "Adv11aCorridor", 170 },
			{ "Adv11bSecrets", 170 },
			{ "Adv12Chamber", 170 },
			{ "GreatHall_G", 180 },
		};

		struct Lesson
		{
			int state; // the state the lesson's end cutscene moves to
			const char* spell;
		};

		const Lesson Lessons[] =
		{
			{ 30, "hgame.spellRictusempra" },
			{ 60, "hgame.spellSkurge" },
			{ 90, "hgame.spellDiffindo" },
			{ 140, "hgame.spellSpongify" },
		};

		std::string Lower(std::string s)
		{
			if (s.size() > 4 && s.compare(s.size() - 4, 4, ".unr") == 0)
				s.resize(s.size() - 4);
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}

		// "GSTATE095" -> 95, anything else -> -1
		int StateNumber(const std::string& token)
		{
			std::string s = Lower(token);
			if (s.size() <= 6 || s.compare(0, 6, "gstate") != 0 || !std::all_of(s.begin() + 6, s.end(), ::isdigit))
				return -1;
			return std::atoi(s.c_str() + 6);
		}
	}

	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map)
	{
		static bool done = false;
		if (done || !commandline || !commandline->HasArg("", "--level"))
			return;
		done = true;
		if (Lower(commandline->GetArg("", "--level")) != Lower(map))
			return;

		int state = StateNumber(commandline->GetArg("", "--gamestate", ""));
		for (const MapState& entry : EntryStates)
		{
			if (state < 0 && Lower(entry.map) == Lower(map))
				state = entry.state;
		}
		if (state < 0 && pawn->HasProperty("CurrentGameState"))
			state = StateNumber(*static_cast<std::string*>(pawn->GetProperty("CurrentGameState")));

		std::string learned;
		for (const Lesson& lesson : Lessons)
		{
			if (state >= 0 && state < lesson.state)
				continue;
			UClass* spell = engine->packages->FindClass(lesson.spell);
			if (!spell)
				continue;
			CallEvent(pawn, NameString("AddToSpellBook"), { ExpressionValue::ObjectValue(spell) });
			learned += std::string(learned.empty() ? "" : ", ") + (lesson.spell + 11); // after "hgame.spell"
		}

		std::string at = state >= 0 ? "GSTATE" + std::string(state < 100 ? "0" : "") + (state < 10 ? "0" : "") + std::to_string(state) : "no story state";
		LogMessage("--level " + map + ": " + at + ", spells learned in the lessons: " + (learned.empty() ? "none" : learned));
	}
}
