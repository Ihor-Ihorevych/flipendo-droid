#pragma once

#include <cstdint>
#include <string>

// Harry Potter 1 only: the optional extras (src/hp1/mods/) and --level. Called from flipendo: hooks gated by
// engine->LaunchInfo.IsHarryPotter1(). The shared KnowWonder engine code is in src/knowwonder/KW.h.

class UCanvas;
class UPlayerPawn;
class UTexture;
class UnrealMipmap;
enum class TextureFormat : uint32_t;
template<typename T> class Array;

namespace HP1
{
	// Additions the original game doesn't have (src/hp1/mods/). Engine::Tick, after the console tick (HP1 and HP2; HP2
	// runs only the mods that read no HP1 scripts).
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value); true consumes the press.
	bool ModsKeyDown(int key);
	// HP1 cutscene skip: real-time transition, bounded world steps and reset before map destruction.
	void BeginModsFrame(float realElapsed);
	void ModsLevelChanging();
	int ModsSimulationSteps(float& elapsed);
	bool ModsSimulationTick();
	bool ModsWorldPaused();
	bool ModsMuteCutsceneSpeech();
	bool ModsPlayerTick(UPlayerPawn* player);
	// Android touch build (src/hp1/HP1Touch.cpp), called from TickMods: Auto Jump on, Options page Controls hidden.
	void TickTouchDefaults();
#ifdef __ANDROID__
	// Spell lessons by touch: the wand follows the finger in the Draw state.
	void TickLessonTouch();
	// The SAVE button: saves in the player's slot (HPConsole.doLevelSave) and says so.
	void TickSaveRequest();
#endif
	// Engine::Tick, when the frame's game time is worked out: how much faster the game runs this frame (1 = normal).
	// HP1 and HP2.
	float ModsTimeScale();
	// HP2's front end (src/hp2/HP2FrontEnd.cpp), before Game.exe's EA and WB splashes: whether to skip them
	// (src/hp1/mods/LaunchSkips.cpp).
	bool ModsSkipSplash();
	// --list-mods (GameApp, before the launcher; any game): every mod (src/hp1/mods/HP1Mods.cpp ModList) with its
	// games, its default and whether this command line turns it on.
	std::string ModsList();
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);
	// Vulkan UploadManager::UploadTexture: a replacement image for the texture (Textures/<package>/<group>/<name>.dds,
	// src/hp1/mods/TextureOverrides.cpp). False uploads the texture's own mipmaps.
	bool LoadTextureOverride(UTexture* texture, TextureFormat& format, Array<UnrealMipmap>& mips);

	void InitCyrillic(); // --cyrillic: Russian text in Windows-1251, glyphs drawn from Noto Sans (HP1Cyrillic.cpp)
	// --level=<map> (src/hp1/HP1LevelStart.cpp). Engine::LoginPlayer, before Possess: the first level started with
	// --level gets the story flow and the state a player carries in.
	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map);
	// HP1::TickMods (runs with --vanilla too): selects --level's save slot once the menu book exists.
	void LevelStartTick();
}
