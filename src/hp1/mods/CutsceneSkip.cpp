#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

// Cutscene skip (on by default, off with --vanilla): while a cutscene holds Harry, "Press Space to skip" is shown;
// Space fast-forwards the cutscene to its end.
//
// HP1 cutscenes are CutScene/CutScriptII actors (HPBase) running per-actor command lists. When one captures Harry
// it sets the HUD's bCutSceneMode and curCutScene, and clears both when it releases him. Jumping to the end
// would skip doors opening, actors moving and triggers firing, so instead the cutscene is played very fast:
// Level.TimeDilation goes up (what the original's debug-only "hold Space to fast-forward" did with SloMo(8)) and
// CutSkip() zeroes every cast member's wait each tick, so dialogue pauses pass at once. Spell lessons and scrolls
// also set bCutSceneMode but no curCutScene, so they are never skipped.

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
			CallEvent(cutscene, NameString("CutSkip"));
		}
		else if (cutscene && pressed)
		{
			Skipping = true;
			CameraTail = 0.0f;
			SavedTimeDilation = engine->LevelInfo->TimeDilation();
			engine->LevelInfo->TimeDilation() = SkipTimeDilation;
			CallEvent(cutscene, NameString("CutSkip"));
		}
	}

	void DrawCutsceneSkip(UCanvas* canvas)
	{
		if (Enabled() && ActiveCutscene())
			DrawPrompt(canvas, Skipping ? "Skipping..." : "Press Space to skip");
	}
}
