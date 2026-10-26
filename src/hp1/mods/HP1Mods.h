#pragma once
#include "KW.h"

#include <functional>
#include <string>
#include <vector>

class UObject;
class UCanvas;
class UClass;
class UFunction;
class ExpressionValue;

// Additions on top of the original game (see docs/modding.md).
namespace HP1::Mods
{
	// One line of the mod list (HP1Mods.cpp, docs/modding.md): the id --mod= / --no-mod= take, whether it is on
	// without either, and the older flags kept working (nullptr for none).
	struct ModInfo
	{
		const char* Id;
		bool DefaultOn;
		const char* Games;
		const char* OnFlag;
		const char* OffFlag;
		const char* Summary;
	};
	const std::vector<ModInfo>& ModList();

	// Whether the mod with this id (ModList) runs, from the command line once.
	bool IsOn(const char* id);

	// IsOn's rule on plain values, strongest first: the id in offList (--no-mod=<ids>), in onList (--mod=<ids>), the
	// mod's OffFlag, its OnFlag, "all" in offList or --vanilla, "all" in onList, then DefaultOn. Lists are comma
	// separated.
	bool ResolveMod(const ModInfo& mod, const std::function<bool(const std::string&)>& hasFlag, const std::string& onList, const std::string& offList);

	// Script state helpers: a property of a script object by name, or nullptr if the object or property is missing.
	using KW::ObjectProperty; // src/knowwonder/KWActor.cpp
	using KW::BoolProperty;

	// Replaces a script function of the class (of its state, or its own with state nullptr) with a native handler
	// (args: one ExpressionValue per parameter) and returns the script function, which the handler can still run with
	// Frame::Call. nullptr, and nothing replaced, if the state or function is missing or already native. Package
	// files are left alone, so saves don't depend on it.
	UFunction* WrapScriptFunction(UClass* cls, const char* state, const char* function, void (*handler)(UObject* self, ExpressionValue* args));

	// Space went down since the last tick (from the window's key events, so it works in menus and in game).
	bool SpacePressed();
	// A small "Press Space to skip"-style line in the bottom right corner of the screen.
	void DrawPrompt(UCanvas* canvas, const std::string& text);

	// LaunchSkips.cpp
	void TickLaunchSkips();
	bool SkipSplash();

	// CutsceneSkip.cpp
	void TickCutsceneSkip(float realElapsed);
	void DrawCutsceneSkip(UCanvas* canvas);

	// StorybookSkip.cpp
	void TickStorybookSkip();
	void DrawStorybookSkip(UCanvas* canvas);

	// DiscordPresence.cpp
	void TickDiscordPresence(float realElapsed);

	// FrameLimit.cpp
	void TickFrameLimit();

	// PathFixes.cpp
	void TickPathFixes();

	// FastForward.cpp
	float FastForwardTimeScale();

	// FreeCamera.cpp
	void TickFreeCamera();

	// ImprovedMovement.cpp
	void TickImprovedMovement();

	// LoadingScreenHold.cpp
	void TickLoadingScreenHold();
}
