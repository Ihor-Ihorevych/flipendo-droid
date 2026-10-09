#include "Precomp.h"
#include "HP1.h"
#include "Engine.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include <atomic>
#include <cmath>

// Touch-screen defaults for the Android build (spike). The key bindings can't be changed on a phone, so the
// Options page's Controls column is hidden, and Auto Jump (a checkbox in that column) is on from the start.
// HP1Touch.cpp is HP1-only: the page is HPMenu.FEOptionsPage (MenuBook.OptionsPage).

#ifdef __ANDROID__
// Defined in android/jni/android_main.cpp (touch bridge to the Java overlay).
extern std::atomic<bool> g_lessonDrawing; // a spell lesson is in its Draw state: the finger draws
extern std::atomic<bool> g_touchDown;
extern std::atomic<float> g_touchX, g_touchY; // finger position, 0..1 of the view
#endif

namespace HP1
{
	namespace
	{
		void HideWindowOf(UObject* window)
		{
			if (window)
				CallEvent(window, NameString("HideWindow"));
		}

		void HideWindowArray(UObject* page, const char* name, int count)
		{
			UObject** items = static_cast<UObject**>(page->GetProperty(NameString(name)));
			if (!items)
				return;
			for (int i = 0; i < count; i++)
			{
				HideWindowOf(items[i]);
				// HideWindow alone leaves HPMenuRaisedButton drawn: also move it off the page (no draw, no hit).
				if (items[i])
					items[i]->SetPropertyFromString(NameString("WinLeft"), "5000");
			}
		}

		void HideOptionsControls()
		{
			UObject* book = engine->console ? engine->console->GetUObject("MenuBook") : nullptr;
			UObject* page = book ? book->GetUObject("OptionsPage") : nullptr;
			if (!page)
				return;

			HideWindowOf(page->GetUObject("ControlLabel"));
			HideWindowArray(page, "KeyNames", 8);
			HideWindowArray(page, "KeyButtons", 8);
			HideWindowOf(page->GetUObject("AutoJumpCheck"));
			HideWindowOf(page->GetUObject("InvertBroomCheck"));
		}
	}

	void TickTouchDefaults()
	{
		if (!engine->viewport)
			return;

		// Auto Jump on, once per player pawn.
		static UObject* autoJumpPawn = nullptr;
		UObject* pawn = engine->viewport->Actor();
		if (pawn && pawn != autoJumpPawn)
		{
			autoJumpPawn = pawn;
			pawn->SetPropertyFromString(NameString("bAutoJump"), "True");
		}

		// Debug mode on by default (what typing "harrydebugmodeon" does): adds Level Select to the main menu and the
		// console. There is no keyboard on a phone to type it.
		static UObject* debugConsole = nullptr;
		if (engine->console && engine->console != debugConsole)
		{
			debugConsole = engine->console;
			engine->console->SetPropertyFromString(NameString("bDebugMode"), "True");
		}

		// The page can be rebuilt when the menu is opened: re-apply regularly while it exists.
		static int ticks = 0;
		if (++ticks % 10 == 0)
			HideOptionsControls();
	}
}

#ifdef __ANDROID__
namespace HP1
{
	// Spell lessons (HPBase.SpellLearnTrigger, state Draw): the script moves the wand from mouse deltas
	// (WandX/WandY in -1..1, the wand sits at Cam + (1, WandX*FOVRatio, WandY*FOVRatio)*WandDist) and stores the
	// point (WandX+0.5, 0.5-WandY) while AltFire is held. With a finger there is no mouse: the wand is put under
	// the finger instead by inverting that projection, and the Java overlay holds AltFire while the finger is down.
	void TickLessonTouch()
	{
		if (!engine->Level || !engine->viewport)
			return;

		UActor* lesson = nullptr;
		for (UActor* a : engine->Level->Actors)
		{
			if (a && a->IsA("SpellLearnTrigger") && a->GetStateName() == "Draw")
			{
				lesson = a;
				break;
			}
		}

		g_lessonDrawing = lesson != nullptr;
		if (!lesson || !g_touchDown)
			return;

		float* wandX = static_cast<float*>(lesson->GetProperty(NameString("WandX")));
		float* wandY = static_cast<float*>(lesson->GetProperty(NameString("WandY")));
		float* fovRatio = static_cast<float*>(lesson->GetProperty(NameString("FOVRatio")));
		if (!wandX || !wandY || !fovRatio || *fovRatio <= 0.0f)
			return;

		float width = (float)engine->viewport->ViewportWidth();
		float height = (float)engine->viewport->ViewportHeight();
		float tanHalfFov = std::tan(engine->CameraFovAngle * 0.5f * 3.14159265f / 180.0f);
		float pixelsPerUnit = *fovRatio * (width * 0.5f) / tanHalfFov;

		float px = g_touchX * width, py = g_touchY * height;
		*wandX = std::clamp((px - width * 0.5f) / pixelsPerUnit, -1.0f, 1.0f);
		*wandY = std::clamp((height * 0.5f - py) / pixelsPerUnit, -1.0f, 1.0f);
	}
}
#endif
