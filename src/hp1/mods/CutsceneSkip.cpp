#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Core/UStruct.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Core/Properties/UStrProperty.h"
#include "Engine.h"
#include <algorithm>

// Cutscene skip (on by default, off with --vanilla): while a cutscene holds Harry, "Press Space to skip" is shown;
// Space fast-forwards the cutscene to its end.
//
// HP1 cutscenes are CutScene/CutScriptII actors (HPBase) running per-actor command lists. When one captures Harry
// it sets the HUD's bCutSceneMode and curCutScene, and clears both when it releases him. Jumping to the end
// would skip doors opening, actors moving and triggers firing, so instead the cutscene is played very fast:
// Level.TimeDilation goes up (what the original's debug-only "hold Space to fast-forward" did with SloMo(8)) and the
// cast's timed waits (Sleep, Talk) are cut short together, so dialogue pauses pass at once.
//
// The cast members only stay in step through their waits: in Lev3_Quid2's common room cutscene the camera's
// "Sleep 9.0, Sleep 2.0, FadeOut 1.0" runs out before the narrator's lines end with "Trigger QuidIntro", whose
// stadium cutscene fades back in. The script's own CutSkip() zeroes every wait, so the narrator got there first and
// the camera's FadeOut came after the stadium's FadeIn: the match was played on a black screen. So the waits are cut
// by the same amount: the shortest one left, and only while no cast member is in a move or turn (those finish on
// their own time, 8 times as fast).
//
// Spell lessons and scrolls also set bCutSceneMode but no curCutScene, so they are never skipped.

namespace HP1::Mods
{
	static const float SkipTimeDilation = 8.0f;

	// After a cutscene releases Harry, its camera track can still be gliding in CutState (its moves don't shorten
	// like the waits do); keep the fast-forward until the camera is back, but never longer than this.
	static const float MaxCameraTail = 3.0f;

	static bool Skipping = false;
	static float SavedTimeDilation = 1.0f;
	static float CameraTail = 0.0f;

	// The cutscene currently holding Harry, or nullptr.
	static UObject* ActiveCutscene()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* hud = player ? player->myHUD() : nullptr;
		if (!hud || !BoolProperty(hud, "bCutSceneMode"))
			return nullptr;
		return ObjectProperty(hud, "curCutScene");
	}

	// A struct member of a script struct, or nullptr.
	static UProperty* StructMember(UStructProperty* prop, const char* name)
	{
		for (UProperty* member : prop->Struct->Properties)
		{
			if (member->Name == name)
				return member;
		}
		return nullptr;
	}

	// Cut every cast member's timed wait by the shortest one left, which keeps them in the order they'd run in
	// unskipped. CutScene.cast[7] and CutScriptII.actors[6] (HPBase) hold the same fields. A move or turn waits for
	// a cue named after the actor (CutScene: action number + name, CutScriptII: the name); then nothing is cut.
	static void CutWaits(UObject* cutscene)
	{
		UStructProperty* list = UObject::Cast<UStructProperty>(cutscene->GetMemberProperty(NameString("cast")));
		if (!list)
			list = UObject::Cast<UStructProperty>(cutscene->GetMemberProperty(NameString("actors")));
		if (!list)
			return;
		UProperty* actorName = StructMember(list, "actorName");
		UProperty* nextTime = StructMember(list, "fNextActionTime");
		UBoolProperty* waiting = UObject::Cast<UBoolProperty>(StructMember(list, "bWaiting"));
		UBoolProperty* finished = UObject::Cast<UBoolProperty>(StructMember(list, "bFinished"));
		UProperty* waitingFor = StructMember(list, "strWaitingFor");
		if (!actorName || !nextTime || !waiting || !finished || !waitingFor)
			return;

		uint8_t* data = static_cast<uint8_t*>(cutscene->GetProperty(list));
		std::vector<float*> timed;
		float shortest = 0.0f;
		for (int i = 0; i < list->ArrayDimension; i++)
		{
			uint8_t* element = static_cast<uint8_t*>(list->GetElement(data, i));
			UObject* actor = *reinterpret_cast<UObject**>(element + actorName->DataOffset.DataOffset);
			if (!actor || finished->GetBool(element + finished->DataOffset.DataOffset))
				continue;
			if (waiting->GetBool(element + waiting->DataOffset.DataOffset))
			{
				std::string cue = *reinterpret_cast<std::string*>(element + waitingFor->DataOffset.DataOffset);
				std::string name = actor->Name.ToString();
				auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
				if (cue.size() >= name.size() && lower(cue.substr(cue.size() - name.size())) == lower(name))
					return; // a move or turn
				continue;
			}
			float* time = reinterpret_cast<float*>(element + nextTime->DataOffset.DataOffset);
			if (*time <= 0.0f)
				return; // acts this tick anyway
			shortest = timed.empty() ? *time : std::min(shortest, *time);
			timed.push_back(time);
		}
		for (float* time : timed)
			*time -= shortest;
	}

	// Harry's camera (baseHarry.cam) is still run by a cutscene.
	static bool CameraInCutscene()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* cam = ObjectProperty(player, "cam");
		return cam && cam->GetStateName() == NameString("CutState");
	}

	void TickCutsceneSkip(float realElapsed)
	{
		if (!Enabled() || !engine->LevelInfo)
			return;

		UObject* cutscene = ActiveCutscene();
		bool pressed = SpacePressed();

		if (Skipping)
		{
			if (!cutscene)
			{
				CameraTail += realElapsed;
				if (!CameraInCutscene() || CameraTail > MaxCameraTail)
				{
					engine->LevelInfo->TimeDilation() = SavedTimeDilation;
					Skipping = false;
				}
				return;
			}
			engine->LevelInfo->TimeDilation() = SkipTimeDilation;
			CutWaits(cutscene);
		}
		else if (cutscene && pressed)
		{
			Skipping = true;
			CameraTail = 0.0f;
			SavedTimeDilation = engine->LevelInfo->TimeDilation();
			engine->LevelInfo->TimeDilation() = SkipTimeDilation;
			CutWaits(cutscene);
		}
	}

	void DrawCutsceneSkip(UCanvas* canvas)
	{
		if (Enabled() && ActiveCutscene())
			DrawPrompt(canvas, Skipping ? "Skipping..." : "Press Space to skip");
	}
}
