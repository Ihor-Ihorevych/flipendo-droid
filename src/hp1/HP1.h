#pragma once

#include <string>

// Harry Potter 1 only: the optional extras (src/hp1/mods/) and --level. Called from flipendo: hooks gated by
// engine->LaunchInfo.IsHarryPotter1(). The shared KnowWonder engine code is in src/knowwonder/KW.h.

class UCanvas;
class UPlayerPawn;

namespace HP1
{
	// Additions the original game doesn't have (src/hp1/mods/). Engine::Tick, after the console tick.
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value).
	void ModsKeyDown(int key);
	// Engine::Tick, when the frame's game time is worked out: how much faster the game runs this frame (1 = normal).
	float ModsTimeScale();
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);

	// --level=<map> (src/hp1/HP1LevelStart.cpp). Engine::LoginPlayer, before Possess: the first level started with
	// --level gets the story flow and the state a player carries in.
	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map);
	// HP1::TickMods (runs with --vanilla too): selects --level's save slot once the menu book exists.
	void LevelStartTick();
}
