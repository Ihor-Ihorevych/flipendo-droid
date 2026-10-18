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
	// Additions the original game doesn't have (src/hp1/mods/). Engine::Tick, after the console tick.
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value).
	void ModsKeyDown(int key);
	// Engine::Tick, when the frame's game time is worked out: how much faster the game runs this frame (1 = normal).
	float ModsTimeScale();
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);
	// Vulkan UploadManager::UploadTexture: a replacement image for the texture (Textures/<package>/<group>/<name>.dds,
	// src/hp1/mods/TextureOverrides.cpp). False uploads the texture's own mipmaps.
	bool LoadTextureOverride(UTexture* texture, TextureFormat& format, Array<UnrealMipmap>& mips);

	// --level=<map> (src/hp1/HP1LevelStart.cpp). Engine::LoginPlayer, before Possess: the first level started with
	// --level gets the story flow and the state a player carries in.
	void LevelStartPlayer(UPlayerPawn* pawn, const std::string& map);
	// HP1::TickMods (runs with --vanilla too): selects --level's save slot once the menu book exists.
	void LevelStartTick();
}
