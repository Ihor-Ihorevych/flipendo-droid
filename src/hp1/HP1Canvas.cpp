#include "Precomp.h"
#include "HP1.h"
#include "KW.h"
#include "Engine.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/UConsole.h"
#include "VM/NativeFunc.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// Widescreen / high resolution 2D for HP1. Not a reimplementation of Engine.dll code: the original only ever ran
// at 4:3 resolutions up to 1024x768 with a canvas of exactly one unit per pixel.
//
// - UI scale: the canvas is kept 768 units tall (what the game looked like at 1024x768) with a fractional scale,
//   instead of upstream's whole-number steps (2x at 1440p gave a 720-unit canvas).
// - HUD (PlayerPawn.PostRender -> HUD): uses the full window width. Every HUD element is anchored to an edge or
//   centred on SizeX/2, and the cutscene letterbox bars are SizeX wide, so this is what makes them cover the screen.
// - Console.PostRender (the FEBook menus, story book, message boxes, loading text): drawn in a centred 4:3 area.
//   HPConsole sets Root.GUIScale = width / 640 and lays the book out at 640x480, so a wider canvas would push the
//   book off the bottom of the screen.
// - Resolutions: GetRes lists the display's real modes and FEOptionsPage.IsSupportedResolution (script, allows
//   512x384..1024x768 only) is replaced with a native that accepts anything from 640x480 up.

#ifdef __ANDROID__
#include <atomic>
extern std::atomic<float> g_uiScale; // android/jni/android_main.cpp: the menu slider for the subtitles and HUD size
#endif

namespace HP1
{
	namespace
	{
		// The 4:3 menu area as last set up, for mapping the OS mouse position into it.
		int MenuLeftPixels = 0;
		float MenuUIScale = 1.0f;
		bool ResolutionOverrideInstalled = false;
	}

	float CanvasUIScale(int viewportHeight)
	{
#ifdef __ANDROID__
		// Phone screens are tiny: lay the 2D UI out as a 640x480 game so subtitles and the HUD stay readable. The menu
		// slider (Subtitles and HUD size) scales that; the menu book still fills its 4:3 area (HPConsole's GUIScale follows).
		return std::max(viewportHeight * g_uiScale.load() / 480.0f, 0.5f);
#else
		return std::max(viewportHeight / 768.0f, 1.0f);
#endif
	}

	void SetCanvasArea(SceneNode& frame, float uiscale, bool menuArea)
	{
		int viewportWidth = engine->viewport->ViewportWidth();
		int viewportHeight = engine->viewport->ViewportHeight();

		int width = viewportWidth;
		if (menuArea)
			width = std::min(viewportWidth, (int)std::lround(viewportHeight * (4.0 / 3.0)));
		int left = (viewportWidth - width) / 2;

		frame.XB = left;
		frame.X = width;
		frame.FX = (float)width;
		frame.FX2 = frame.FX * 0.5f;

		int sizeX = (int)(width / uiscale);
		int sizeY = (int)(viewportHeight / uiscale);
		engine->canvas->SizeX() = sizeX;
		engine->canvas->SizeY() = sizeY;
		engine->canvas->ClipX() = (float)sizeX;
		engine->canvas->ClipY() = (float)sizeY;
		engine->console->FrameX() = (float)sizeX;
		engine->console->FrameY() = (float)sizeY;

		if (menuArea)
		{
			MenuLeftPixels = left;
			MenuUIScale = uiscale;
		}
	}

	void MenuMousePosition(float& x, float& y)
	{
		x = (x - MenuLeftPixels) / MenuUIScale;
		y = y / MenuUIScale;
	}

	static void NIsSupportedResolution(UObject* self, const std::string& TempStr, BitfieldBool& ReturnValue)
	{
		int width = 0, height = 0;
		ReturnValue = std::sscanf(TempStr.c_str(), "%dx%d", &width, &height) == 2 && width >= 640 && height >= 480;
	}

	// FEOptionsPage is only loaded once the options page exists, which is also when it first calls GetRes. Turning
	// the script function into a native by name works because Frame::Call checks FUNC_Native on every call.
	static void InstallResolutionOverride()
	{
		if (ResolutionOverrideInstalled)
			return;

		UClass* cls = engine->packages->FindClass("HPMenu.FEOptionsPage");
		if (!cls)
			return;

		for (UField* field = cls->Children; field != nullptr; field = field->Next)
		{
			UFunction* func = UObject::TryCast<UFunction>(field);
			if (func && func->Name == "IsSupportedResolution")
			{
				RegisterVMNativeFunc_2("FEOptionsPage", "IsSupportedResolution", &NIsSupportedResolution, 0);
				func->NativeStruct = cls;
				func->NativeFuncIndex = 0;
				func->FuncFlags = func->FuncFlags | FunctionFlags::Native;
				ResolutionOverrideInstalled = true;
				return;
			}
		}
	}

	std::string AvailableResolutions(GameWindow* window)
	{
		InstallResolutionOverride();
		return window->GetAvailableResolutions();
	}
}
