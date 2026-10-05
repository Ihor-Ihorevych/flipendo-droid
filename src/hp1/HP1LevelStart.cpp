#include "Precomp.h"
#include "HP1.h"
#include "Packages/Core/UObject.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/UConsole.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <vector>

// --level=<map> (developer launch flag, docs/playtest.md): start in a level as a player arrives there in the story.
// --url only loads the map: HPConsole.bInHubFlow stays false (the flying lesson runs as its menu replay, the
// Quidditch matches as League matches) and Harry has a new game's defaults. --level also sets bInHubFlow, as
// New Game and Load do (FEBook, FESlotPage), and gives Harry the travel state (baseHarry's `travel` properties) an
// average player carries in from the levels before, counted from the maps (docs/playtest.md):
// - wizard cards: every card of the earlier levels (docs/re/hp1/collectibles.md);
// - beans: 80% of the beans placed and in chests in the earlier levels, less 25 for each card Fred sold
//   (WizardCardCut) and the 25 TakeAllBeansTrigger takes in Lev4_Sneak2;
// - house points: every lesson completed (5 + 10 + 15 + 20), 10 for each star challenge and the flying lesson
//   (they pay 5, 10 or 20, and 5 to 20). The other houses are set by baseHarry.AddHousePoints' rule at its average;
// - the quest items picked up earlier (no script reads them, but saves carry them).
// Applied once, to the first level, before Possess (where BroomHarry tells the referee whether it is in the story).

namespace HP1
{
	namespace
	{
		// What a level adds to what Harry carries out of it, in story order (Dobby.int's level indices)
		struct LevelGains
		{
			const char* map;
			std::vector<int> cards;	// WizzardCardIcon IDs
			int beans;				// placed beans + chest beans
			int points;				// average house points
			int fredCards;			// cards bought from Fred for 25 beans
			int beansTaken;
			std::vector<const char*> items; // baseHarry bHas* flags
		};

		const LevelGains Story[] =
		{
			{ "Lev_Tut1",        { 101 },        29,  50, 1,  0, {} },
			{ "Lev_Tut1b",       {},             11,  10, 0,  0, {} },
			{ "Lev_Tut2",        { 1 },           4,  10, 0,  0, {} },
			{ "Lev_Tut3",        { 28, 10 },     52, 110, 0,  0, {} },
			{ "Lev_Tut3b",       { 18, 24, 8 },  19,   0, 0,  0, {} },
			{ "Lev2_HogFront",   { 41, 2 },      55,   0, 1,  0, {} },
			{ "Lev2_Inc_A",      { 19 },         28,  50, 0,  0, {} },
			{ "Lev2_Inc_B",      { 47 },         28,  10, 0,  0, {} },
			{ "Lev2_HogFront_2", {},             16,   0, 0,  0, {} },
			{ "Lev2_RemChase",   {},              0,   0, 0,  0, {} },
			{ "Lev2_HogFront_3", {},              0,   0, 0,  0, {} },
			{ "Lev2_Fire2",      { 35, 11 },     40,   0, 0,  0, {} },
			{ "Lev2_fire1",      { 17, 48 },     36,   0, 0,  0, { "bHasFlute" } },
			{ "Lev2_Quid1",      {},              0,   0, 0,  0, {} },
			{ "Lev3_Intro",      { 69 },         28,  50, 1,  0, {} },
			{ "Lev3_Lumos",      { 37 },         41,  10, 0,  0, {} },
			{ "Lev3_PreDungeon", { 62, 57 },     11,   0, 1,  0, {} },
			{ "Lev3_Dungeon",    { 49 },         27,   0, 0,  0, { "bHasDittany", "bHasMoly", "bHasBark" } },
			{ "Lev3_DungeonB",   { 96 },         28,   0, 0,  0, { "bHasMucus" } },
			{ "Lev3_PreTroll",   {},              0,   0, 0,  0, {} },
			{ "Lev3_Troll",      {},             89,   0, 0,  0, {} },
			{ "Lev3_Quid2",      {},              0,   0, 0,  0, {} },
			{ "Lev4_Sneak",      { 72 },         15,   0, 0,  0, {} },
			{ "Lev4_Sneak2",     { 82 },         18,   0, 0, 25, {} },
			{ "Lev5_fluffy",     { 83 },          2,   0, 0,  0, {} },
			{ "Lev5_Snare",      {},              0,   0, 0,  0, {} },
			{ "Lev5_FlyKeys",    {},              0,   0, 0,  0, {} },
			{ "Lev5_Chess",      {},              0,   0, 0,  0, {} },
			{ "Lev5_Final",      {},              1,   0, 0,  0, {} },
			{ "Snapes_Office",   {},              0,   0, 0,  0, {} },
		};

		bool SameMap(std::string a, std::string b)
		{
			auto lower = [](std::string s) {
				if (s.size() > 4 && s.compare(s.size() - 4, 4, ".unr") == 0)
					s.resize(s.size() - 4);
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				return s;
			};
			return lower(a) == lower(b);
		}

		void SetBool(UObject* obj, const char* name, bool value)
		{
			if (obj->HasProperty(name))
				obj->BoolValue(obj->Class->GetPropertyDataOffset(name)) = value;
		}

		void SetInt(UObject* obj, const char* name, int value)
		{
			if (obj->HasProperty(name))
				*static_cast<int*>(obj->GetProperty(name)) = value;
		}
	}

	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map)
	{
		static bool done = false;
		if (done || !commandline || !commandline->HasArg("", "--level"))
			return;
		done = true;
		if (!SameMap(commandline->GetArg("", "--level"), map))
			return;

		int level = -1;
		for (int i = 0; i < (int)std::size(Story); i++)
		{
			if (SameMap(Story[i].map, map))
				level = i;
		}
		if (level < 0)
		{
			LogMessage("--level " + map + ": not a story level (a Quidditch League match?), loaded as --url");
			return;
		}

		if (engine->console && engine->console->HasProperty("bInHubFlow"))
			SetBool(engine->console, "bInHubFlow", true);

		int beansFound = 0, points = 0, spent = 0, cards = 0;
		for (int i = 0; i < level; i++)
		{
			const LevelGains& gains = Story[i];
			beansFound += gains.beans;
			points += gains.points;
			spent += gains.fredCards * 25 + gains.beansTaken;
			for (int id : gains.cards)
			{
				CallEvent(pawn, NameString("addcard"), { ExpressionValue::IntValue(id) });
				cards++;
			}
			for (const char* item : gains.items)
				SetBool(pawn, item, true);
		}
		int beans = std::max((int)std::lround(beansFound * 0.8f) - spent, 0);
		SetInt(pawn, "numBeans", beans);

		// baseHarry.AddHousePoints: Gryffindor = Harry's points; Slytherin ahead by 1 + Rand(min(G, 58)), Hufflepuff
		// G * (0.5 + FRand() * 0.2), Ravenclaw G * (0.7 + FRand() * 0.2); here at the averages of the random parts
		if (points > 0)
		{
			SetInt(pawn, "numHousePointsHarry", points);
			SetInt(pawn, "numHousePointsGryffindor", points);
			SetInt(pawn, "numHousePointsSlytherin", points + 1 + std::min(points, 58) / 2);
			SetInt(pawn, "numHousePointsHufflepuff", (int)(points * 0.6f));
			SetInt(pawn, "numHousePointsRavenclaw", (int)(points * 0.8f));
		}

		LogMessage("--level " + map + ": in the story flow, " + std::to_string(cards) + " wizard cards, " + std::to_string(beans) +
			" beans, " + std::to_string(points) + " house points");
	}
}
