#pragma once

#include <string>

// Harry Potter 1 only: the optional extras (src/hp1/mods/) and the widescreen canvas for HP1's menu classes
// (src/hp1/HP1Canvas.cpp). Called from flipendo: hooks gated by engine->LaunchInfo.IsHarryPotter1(). The shared
// KnowWonder engine code is in src/knowwonder/KW.h.

class UCanvas;
class UPlayerPawn;
class GameWindow;
struct SceneNode;

namespace HP1
{
	// Additions the original game doesn't have (src/hp1/mods/). Engine::Tick, after the console tick.
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value).
	void ModsKeyDown(int key);
	// Android touch build (src/hp1/HP1Touch.cpp), called from TickMods: Auto Jump on, Options page Controls hidden.
	void TickTouchDefaults();
#ifdef __ANDROID__
	// Spell lessons by touch: the wand follows the finger in the Draw state.
	void TickLessonTouch();
	// The SAVE button: saves in the player's slot (HPConsole.doLevelSave) and says so.
	void TickSaveRequest();
#endif
	// Engine::Tick, when the frame's game time is worked out: how much faster the game runs this frame (1 = normal).
	float ModsTimeScale();
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);

	// Widescreen 2D (src/hp1/HP1Canvas.cpp). RenderSubsystem::ResetCanvas: UI scale for the viewport height.
	float CanvasUIScale(int viewportHeight);
	// RenderSubsystem::ResetCanvas / PostRender: full-width canvas (HUD), or the centred 4:3 area (console/menus).
	void SetCanvasArea(SceneNode& frame, float uiscale, bool menuArea);
	// Engine::OnWindowMouseMove: OS mouse position (pixels) to menu canvas units, for HPConsole's WindowsMouseX/Y.
	void MenuMousePosition(float& x, float& y);
	// --level=<map> (src/hp1/HP1LevelStart.cpp). Engine::LoginPlayer, before Possess: the first level started with
	// --level gets the story flow and the state a player carries in.
	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map);
	// HP1::TickMods (runs with --vanilla too): selects --level's save slot once the menu book exists.
	void LevelStartTick();
	// Engine::ConsoleCommand "getres": the display's modes, with FEOptionsPage's 1024x768 cap lifted.
	std::string AvailableResolutions(GameWindow* window);
}
