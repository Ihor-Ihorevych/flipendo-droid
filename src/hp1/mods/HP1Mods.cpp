#include "Precomp.h"
#include "HP1.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Utils/CommandLine.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Native/NCanvas.h"
#include "Engine.h"

namespace HP1::Mods
{
	bool Enabled()
	{
		static int enabled = -1;
		if (enabled < 0)
			enabled = HasFlag("--vanilla") ? 0 : 1;
		return enabled != 0;
	}

	bool HasFlag(const char* flag)
	{
		return commandline && commandline->HasArg("", flag);
	}


	// Set by a Space key-down (HP1::ModsKeyDown), consumed by the next TickMods.
	static bool SpaceQueued = false;
	static bool SpacePressedNow = false;

	static void UpdateSpace()
	{
		SpacePressedNow = SpaceQueued;
		SpaceQueued = false;
	}

	bool SpacePressed()
	{
		return SpacePressedNow;
	}

	void DrawPrompt(UCanvas* canvas, const std::string& text)
	{
		UFont* savedFont = canvas->Font();
		Color savedColor = canvas->DrawColor();
		uint8_t savedStyle = canvas->Style();
		float savedX = canvas->CurX(), savedY = canvas->CurY();
		float savedOrgX = canvas->OrgX(), savedOrgY = canvas->OrgY();
		float savedClipX = canvas->ClipX(), savedClipY = canvas->ClipY();

		canvas->OrgX() = 0.0f;
		canvas->OrgY() = 0.0f;
		canvas->ClipX() = (float)canvas->SizeX();
		canvas->ClipY() = (float)canvas->SizeY();
		if (UFont* font = UObject::TryCast<UFont>(ObjectProperty(engine->console, "LocalSmallFont")))
			canvas->Font() = font;
		canvas->DrawColor() = { 200, 200, 200, 255 };
		canvas->Style() = 1; // STY_Normal

		float width = 0.0f, height = 0.0f;
		NCanvas::StrLen(canvas, text, width, height);
		// Bottom right; in a cutscene that is inside the letterbox bar.
		canvas->CurX() = canvas->ClipX() - width - 16.0f;
		canvas->CurY() = canvas->ClipY() - height - 10.0f;
		NCanvas::DrawText(canvas, text, false);

		canvas->Font() = savedFont;
		canvas->DrawColor() = savedColor;
		canvas->Style() = savedStyle;
		canvas->CurX() = savedX;
		canvas->CurY() = savedY;
		canvas->OrgX() = savedOrgX;
		canvas->OrgY() = savedOrgY;
		canvas->ClipX() = savedClipX;
		canvas->ClipY() = savedClipY;
	}
}

namespace HP1
{
	void TickMods(float realElapsed)
	{
		if (!engine->console)
			return;
		Mods::UpdateSpace();
		Mods::TickLaunchSkips();
		Mods::TickStorybookSkip();
		Mods::TickCutsceneSkip(realElapsed);
		Mods::TickDiscordPresence(realElapsed);
	}

	float ModsTimeScale()
	{
		return Mods::FastForwardTimeScale();
	}

	void ModsKeyDown(int key)
	{
		if (key == IK_Space)
			Mods::SpaceQueued = true;
	}

	void PostRenderMods(UCanvas* canvas)
	{
		if (!canvas)
			return;
		Mods::DrawStorybookSkip(canvas);
		Mods::DrawCutsceneSkip(canvas);
	}
}
