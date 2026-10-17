#include "Precomp.h"
#include "HP2.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"
#include <algorithm>

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
	// such a map). The original also takes a GAMESTATE= command line option (a debug override; not ported).
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
