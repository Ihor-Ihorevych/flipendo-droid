#include "Precomp.h"
#include "KW.h"
#include "KWGame.h"
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

// Widescreen / high resolution 2D for HP1 and HP2. Not a reimplementation of Engine.dll code: the originals only ever
// ran at 4:3 resolutions up to 1024x768 (HP2's debug mode also 1280x1024) with a canvas of exactly one unit per pixel.
//
// - UI scale: the canvas is kept 768 units tall (what the game looked like at 1024x768) with a fractional scale,
//   instead of upstream's whole-number steps (2x at 1440p gave a 720-unit canvas).
// - HUD (PlayerPawn.PostRender -> HUD): uses the full window width. Every HUD element is anchored to an edge or
//   centred on SizeX/2, and the cutscene letterbox bars are SizeX wide, so this is what makes them cover the screen.
// - Console.PostRender (the FEBook menus, story book, message boxes, loading text): drawn in a centred 4:3 area.
//   HPConsole sets Root.GUIScale = width / 640 and lays the book out at 640x480 (HP1's HPMenu and HP2's hgame alike),
//   so a wider canvas would push the book off the bottom of the screen.
// - Resolutions: GetRes lists the common modes up to the desktop size and the options page's IsSupportedResolution
//   (script: HP1's FEOptionsPage allows 512x384..1024x768, HP2's FESoundVideoPage 640x480..1024x768) is replaced with
//   a native that accepts anything from 640x480 up.

#ifdef __ANDROID__
#include <atomic>
extern std::atomic<float> g_uiScale; // android/jni/android_main.cpp: the menu slider for the subtitles and HUD size
extern std::atomic<bool> g_vendorActive; // HP2: a vendor's Yes/No offer is drawn on the HUD canvas (src/hp2/HP2Touch.cpp)
#endif

namespace KW
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
#ifdef __ANDROID__
		// HP2's vendor offer (the beans for a duel or a broom) is drawn on the full-width HUD canvas but clicked with the
		// console's mouse: its hit test compares that mouse with HUD units, so the mouse goes into the HUD canvas, not the 4:3 menu area
		if (g_vendorActive.load())
		{
			const float hud = CanvasUIScale(engine->viewport->ViewportHeight());
			x = x / hud;
			y = y / hud;
			return;
		}
#endif
		x = (x - MenuLeftPixels) / MenuUIScale;
		y = y / MenuUIScale;
	}

	void MenuToPixels(float& x, float& y)
	{
		x = MenuLeftPixels + x * MenuUIScale;
		y = y * MenuUIScale;
	}

	static void NIsSupportedResolution(UObject* self, const std::string& TempStr, BitfieldBool& ReturnValue)
	{
		int width = 0, height = 0;
		ReturnValue = std::sscanf(TempStr.c_str(), "%dx%d", &width, &height) == 2 && width >= 640 && height >= 480;
	}

	// The options page class is only loaded once the page exists, which is also when it first calls GetRes. Turning
	// the script function into a native by name works because Frame::Call checks FUNC_Native on every call.
	static void InstallResolutionOverride()
	{
		if (ResolutionOverrideInstalled)
			return;

		const char* className = IsHP1() ? "FEOptionsPage" : "FESoundVideoPage";
		UClass* cls = engine->packages->FindClass(IsHP1() ? "HPMenu.FEOptionsPage" : "HGame.FESoundVideoPage");
		if (!cls)
			return;

		for (UField* field = cls->Children; field != nullptr; field = field->Next)
		{
			UFunction* func = UObject::TryCast<UFunction>(field);
			if (func && func->Name == "IsSupportedResolution")
			{
				RegisterVMNativeFunc_2(className, "IsSupportedResolution", &NIsSupportedResolution, 0);
				func->NativeStruct = cls;
				func->NativeFuncIndex = 0;
				func->FuncFlags = func->FuncFlags | FunctionFlags::Native;
				// Frame::CallNative uses a native's frame as one ExpressionValue per property (UFunction::Load sets
				// this up for natives loaded from a package). Keeping the script layout leaves the argument slots
				// unconstructed and too small, so they read stale variable-stack bytes and overrun the frame.
				func->IsNativeFunc = true;
				func->StructAlignment = alignof(ExpressionValue);
				func->StructSize = func->Properties.size() * sizeof(ExpressionValue);
				ResolutionOverrideInstalled = true;
				return;
			}
		}
	}

	// SurrealEngine only offers the desktop size, in DPI-scaled units, which can leave the options page with an empty
	// list (its combo list then divides by zero). List the common modes that fit the desktop, the desktop itself and
	// the current window size, in pixels like SetRes takes them.
	std::string AvailableResolutions(GameWindow* window)
	{
		InstallResolutionOverride();

		std::string result;
		for (auto& mode : ResolutionModes(window->GetDpiScale(), window->GetPixelWidth(), window->GetPixelHeight()))
		{
			if (!result.empty())
				result += " ";
			result += std::to_string(mode.first) + "x" + std::to_string(mode.second);
		}
		return result.empty() ? "640x480" : result;
	}

	std::vector<std::pair<int, int>> ResolutionModes(double dpiScale, int currentX, int currentY)
	{
		Size screen = Widget::GetScreenSize();
		int desktopX = (int)std::lround(screen.width * dpiScale);
		int desktopY = (int)std::lround(screen.height * dpiScale);

		static const int commonModes[][2] = {
			{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 960 }, { 1366, 768 }, { 1600, 900 },
			{ 1600, 1200 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }
		};
		std::vector<std::pair<int, int>> modes;
		for (auto& mode : commonModes)
		{
			// the DPI round trip can come back a pixel short (2559x1439 for 2560x1440)
			if (std::abs(mode[0] - desktopX) <= 2 && std::abs(mode[1] - desktopY) <= 2)
			{
				desktopX = mode[0];
				desktopY = mode[1];
			}
			if (mode[0] <= desktopX && mode[1] <= desktopY)
				modes.push_back({ mode[0], mode[1] });
		}
		modes.push_back({ desktopX, desktopY });
		if (currentX > 0 && currentY > 0)
			modes.push_back({ currentX, currentY });

		std::sort(modes.begin(), modes.end());
		modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
		modes.erase(std::remove_if(modes.begin(), modes.end(), [](auto& mode) { return mode.first < 640 || mode.second < 480; }), modes.end());
		return modes;
	}
}
