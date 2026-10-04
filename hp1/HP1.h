#pragma once

#include <string>

// Harry Potter 1 only: the optional extras (hp1/mods/) and the widescreen canvas for HP1's menu classes
// (hp1/HP1Canvas.cpp). Called from flipendo: hooks gated by engine->LaunchInfo.IsHarryPotter1(). The shared
// KnowWonder engine code is in kw/KW.h.

class UCanvas;
class GameWindow;
struct SceneNode;

namespace HP1
{
	// Additions the original game doesn't have (hp1/mods/). Engine::Tick, after the console tick.
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value).
	void ModsKeyDown(int key);
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);

	// Widescreen 2D (hp1/HP1Canvas.cpp). RenderSubsystem::ResetCanvas: UI scale for the viewport height.
	float CanvasUIScale(int viewportHeight);
	// RenderSubsystem::ResetCanvas / PostRender: full-width canvas (HUD), or the centred 4:3 area (console/menus).
	void SetCanvasArea(SceneNode& frame, float uiscale, bool menuArea);
	// Engine::OnWindowMouseMove: OS mouse position (pixels) to menu canvas units, for HPConsole's WindowsMouseX/Y.
	void MenuMousePosition(float& x, float& y);
	// Engine::ConsoleCommand "getres": the display's modes, with FEOptionsPage's 1024x768 cap lifted.
	std::string AvailableResolutions(GameWindow* window);
}
