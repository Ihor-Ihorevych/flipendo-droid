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
		Mods::UpdateSpace();
		Mods::TickLaunchSkips();
		LevelStartTick();
		Mods::TickStorybookSkip();
		Mods::TickCutsceneSkip(realElapsed);
		Mods::TickDiscordPresence(realElapsed);
		Mods::TickPathFixes();
		Mods::TickImprovedMovement();
		Mods::TickFrameLimit();
	}

	float ModsTimeScale()
	{
		return Mods::FastForwardTimeScale();
	}

	bool ModsSkipSplash()
	{
		return Mods::SkipSplash();
	}

	void ModsKeyDown(int key)
	{
		if (key == IK_Space)
			Mods::SpaceQueued = true;
		KW::DebugKeyDown(key);
	}

	void PostRenderMods(UCanvas* canvas)
	{
		if (!canvas)
			return;
		Mods::DrawStorybookSkip(canvas);
		Mods::DrawCutsceneSkip(canvas);
	}
}
