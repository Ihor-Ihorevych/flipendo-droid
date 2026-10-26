#include "Precomp.h"
#include "HP1.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UState.h"
#include "Packages/Core/UFunction.h"
#include "VM/NativeFunc.h"
#include "VM/ExpressionValue.h"
#include "GC/GC.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Native/NCanvas.h"
#include "Engine.h"

#include <cstdio>
#include <cstring>

namespace HP1::Mods
{
	const std::vector<ModInfo>& ModList()
	{
		static const std::vector<ModInfo> mods = {
			{ "cutscene-skip", true, "HP1", nullptr, nullptr, "Space skips cutscenes" },
			{ "storybook-skip", true, "HP1", nullptr, nullptr, "Space skips storybooks" },
			{ "discord", true, "HP1", nullptr, "--no-discord", "Discord Rich Presence" },
			{ "fast-forward", true, "HP1, HP2", nullptr, nullptr, "hold Shift to run 2.5x as fast" },
			{ "frame-limit", true, "HP1, HP2", nullptr, nullptr, "at most --fps=<n> (60) frames a second" },
			{ "path-fixes", true, "HP1", nullptr, nullptr, "reach specs the original maps are missing" },
			{ "texture-overrides", true, "HP1, HP2", nullptr, nullptr, "Textures/<package>/<group>/<name>.dds packs" },
			{ "free-cam", false, "HP1", nullptr, nullptr, "camera-relative movement and independent mouse orbit" },
			{ "improved-movement", true, "HP1", nullptr, "--classic-movement", "HP2's controls: strafe, move while aiming" },
			{ "splash-skip", true, "HP1, HP2", "--skip-splash", nullptr, "no logo screens at start" },
			{ "intro-skip", false, "HP1", "--skip-intro", nullptr, "New Game goes straight to Lev_Tut1" },
			{ "loading-screen-hold", true, "HP1", nullptr, nullptr, "the loading screen stays up at least 3.5 s" },
		};
		return mods;
	}

	static bool InList(const std::string& list, const std::string& id)
	{
		size_t start = 0;
		while (start <= list.size())
		{
			size_t end = list.find(',', start);
			if (end == std::string::npos)
				end = list.size();
			if (list.compare(start, end - start, id) == 0)
				return true;
			start = end + 1;
		}
		return false;
	}

	bool ResolveMod(const ModInfo& mod, const std::function<bool(const std::string&)>& hasFlag, const std::string& onList, const std::string& offList)
	{
		if (InList(offList, mod.Id))
			return false;
		if (InList(onList, mod.Id))
			return true;
		if (mod.OffFlag && hasFlag(mod.OffFlag))
			return false;
		if (mod.OnFlag && hasFlag(mod.OnFlag))
			return true;
		if (InList(offList, "all") || hasFlag("--vanilla"))
			return false;
		if (InList(onList, "all"))
			return true;
		return mod.DefaultOn;
	}

	static bool ResolveFromCommandLine(const ModInfo& mod)
	{
		auto hasFlag = [](const std::string& flag) { return commandline && commandline->HasArg("", flag); };
		std::string onList = commandline ? commandline->GetArg("", "--mod", "") : "";
		std::string offList = commandline ? commandline->GetArg("", "--no-mod", "") : "";
		return ResolveMod(mod, hasFlag, onList, offList);
	}

	// An id in --mod= / --no-mod= that names no mod is logged once, so a typo doesn't pass silently
	static void WarnUnknownIds(const std::string& flag)
	{
		std::string list = commandline ? commandline->GetArg("", flag, "") : "";
		size_t start = 0;
		while (!list.empty() && start <= list.size())
		{
			size_t end = list.find(',', start);
			if (end == std::string::npos)
				end = list.size();
			std::string id = list.substr(start, end - start);
			bool known = id == "all";
			for (const ModInfo& mod : ModList())
				known = known || id == mod.Id;
			if (!known)
				LogMessage(flag + ": no mod named '" + id + "' (--list-mods lists them)");
			start = end + 1;
		}
	}

	bool IsOn(const char* id)
	{
		static std::vector<bool> on;
		if (on.empty())
		{
			for (const ModInfo& mod : ModList())
				on.push_back(ResolveFromCommandLine(mod));
			WarnUnknownIds("--mod");
			WarnUnknownIds("--no-mod");
		}
		const std::vector<ModInfo>& mods = ModList();
		for (size_t i = 0; i < mods.size(); i++)
		{
			if (std::strcmp(mods[i].Id, id) == 0)
				return on[i];
		}
		LogMessage(std::string("Mods::IsOn: no mod named '") + id + "'");
		return false;
	}

}

namespace HP1
{
	std::string ModsList()
	{
		using namespace Mods;
		std::string text = "Mods: --mod=<id,...> turns them on, --no-mod=<id,...> off (all: every mod), --vanilla turns off every one not named\n";
		for (const ModInfo& mod : ModList())
		{
			char line[256];
			std::snprintf(line, sizeof(line), "  %-18s %-9s default %-3s now %-3s %s\n", mod.Id, mod.Games,
				mod.DefaultOn ? "on" : "off", ResolveFromCommandLine(mod) ? "on" : "off", mod.Summary);
			text += line;
		}
		return text;
	}
}

namespace HP1::Mods
{
	// Set by a Space key-down (HP1::ModsKeyDown), consumed by the next BeginModsFrame.
	static bool SpaceQueued = false;
	static bool SpacePressedNow = false;

	static void UpdateSpace()
	{
		SpacePressedNow = SpaceQueued;
		SpaceQueued = false;
	}

	UFunction* WrapScriptFunction(UClass* cls, const char* stateName, const char* functionName, void (*handler)(UObject* self, ExpressionValue* args))
	{
		if (!cls)
			return nullptr;
		UState* state = stateName ? cls->GetState(NameString(stateName)) : nullptr;
		if (stateName && !state)
			return nullptr;
		std::map<NameString, UFunction*>& functions = state ? state->Functions : cls->Functions;
		auto it = functions.find(NameString(functionName));
		if (it == functions.end() || AllFlags(it->second->FuncFlags, FunctionFlags::Native))
			return nullptr;
		UFunction* original = it->second;

		// A native as Frame::CallNative runs it: found by name and state or class (NativeStruct), its frame one
		// ExpressionValue per property (what UFunction::Load sets up for natives loaded from a package)
		auto wrapper = GC::Alloc<UFunction>(original->Name, original->Class, original->Flags);
		wrapper->StructParent = original->StructParent; // Frame::GetName: Harry.PlayerWalking.PlayerMove in call stacks
		wrapper->BaseStruct = original->BaseStruct;
		wrapper->Line = original->Line;
		wrapper->Properties = original->Properties;
		wrapper->ParmsSize = original->ParmsSize;
		wrapper->NumParms = original->NumParms;
		wrapper->ReturnValueOffset = original->ReturnValueOffset;
		wrapper->FuncFlags = original->FuncFlags | FunctionFlags::Native;
		wrapper->NativeStruct = state ? static_cast<UStruct*>(state) : cls;
		wrapper->NativeFuncIndex = 0;
		wrapper->IsNativeFunc = true;
		wrapper->StructAlignment = alignof(ExpressionValue);
		wrapper->StructSize = wrapper->Properties.size() * sizeof(ExpressionValue);
		NativeFunctions::NativeByName[{ original->Name, wrapper->NativeStruct->Name }] = handler;
		it->second = wrapper;
		return original;
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
		if (engine->LaunchInfo.IsHarryPotter2())
		{
			// The other mods read HP1's script classes
			Mods::TickFrameLimit();
			return;
		}
		Mods::TickLoadingScreenHold();
		Mods::TickLaunchSkips();
		LevelStartTick();
		Mods::TickStorybookSkip();
		Mods::TickDiscordPresence(realElapsed);
		Mods::TickPathFixes();
		Mods::TickImprovedMovement();
		Mods::TickFreeCamera();
		Mods::TickFrameLimit();
#ifdef __ANDROID__
		TickTouchDefaults();
		TickLessonTouch();
		TickSaveRequest();
#endif
	}

	float ModsTimeScale()
	{
		return engine->LaunchInfo.IsHarryPotter1() && Mods::CutsceneSkipBusy() ? 1.0f : Mods::FastForwardTimeScale();
	}

	bool ModsSkipSplash()
	{
		return Mods::SkipSplash();
	}

	void BeginModsFrame(float realElapsed)
	{
		Mods::UpdateSpace();
		Mods::TickCutsceneSkip(realElapsed);
	}

	void ModsLevelChanging()
	{
		Mods::ResetCutsceneSkip();
		Mods::SpaceQueued = Mods::SpacePressedNow = false;
	}

	int ModsSimulationSteps(float& elapsed)
	{
		return Mods::CutsceneSkipSteps(elapsed, Mods::CutsceneSkipAccelerating());
	}

	bool ModsSimulationTick()
	{
		Mods::CutsceneSkipSimulationTick();
		return Mods::CutsceneSkipAccelerating();
	}

	bool ModsWorldPaused() { return Mods::CutsceneSkipWorldPaused(); }
	bool ModsMuteCutsceneSpeech() { return Mods::CutsceneSkipSpeechMuted(); }
	bool ModsPlayerTick(UPlayerPawn* player) { return Mods::CutsceneSkipPlayerTick(player); }

	bool ModsKeyDown(int key)
	{
		if (Mods::CutsceneSkipKeyDown(key))
		{
			if (key == IK_Space && !Mods::CutsceneSkipBusy())
				Mods::SpaceQueued = true;
			return true;
		}
		if (key == IK_Space)
			Mods::SpaceQueued = true;
		KW::DebugKeyDown(key);
		return false;
	}

	void PostRenderMods(UCanvas* canvas)
	{
		if (!canvas)
			return;
		Mods::DrawStorybookSkip(canvas);
		Mods::DrawCutsceneSkip(canvas);
	}
}
