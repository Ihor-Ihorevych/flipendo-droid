#pragma once

#include <string>

class UObject;
class UCanvas;

// Additions on top of the original game (see README.md in this folder).
namespace HP1::Mods
{
	// False when the game runs with --vanilla: default-on mods stay off.
	bool Enabled();
	// The command line has this flag (e.g. "--skip-intro").
	bool HasFlag(const char* flag);

	// Script state helpers: a property of a script object by name, or nullptr if the object or property is missing.
	UObject* ObjectProperty(UObject* obj, const char* name);
	bool BoolProperty(UObject* obj, const char* name);

	// Space went down since the last tick (from the window's key events, so it works in menus and in game).
	bool SpacePressed();
	// A small "Press Space to skip"-style line in the bottom right corner of the screen.
	void DrawPrompt(UCanvas* canvas, const std::string& text);

	// LaunchSkips.cpp
	void TickLaunchSkips();

	// CutsceneSkip.cpp
	void TickCutsceneSkip(float realElapsed);
	void DrawCutsceneSkip(UCanvas* canvas);

	// StorybookSkip.cpp
	void TickStorybookSkip();
	void DrawStorybookSkip(UCanvas* canvas);
}
