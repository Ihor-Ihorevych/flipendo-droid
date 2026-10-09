#include "Precomp.h"
#include "HP1.h"
#include "Engine.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "VM/ScriptCall.h"
#include "KW.h"
#include "Utils/Logger.h"
#include "Package/PackageManager.h"
#include <filesystem>
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include <atomic>
#include <cmath>

// Touch-screen defaults for the Android build (spike). The key bindings can't be changed on a phone, so the
// Options page's Controls column is hidden, and Auto Jump (a checkbox in that column) is on from the start.
// HP1Touch.cpp is HP1-only: the page is HPMenu.FEOptionsPage (MenuBook.OptionsPage).

#ifdef __ANDROID__
// Defined in android/jni/android_main.cpp (touch bridge to the Java overlay).
extern std::atomic<bool> g_debugMode; // the settings panel's debug mode switch
extern std::atomic<bool> g_lessonDrawing; // a spell lesson is in its Draw state: the finger draws
extern std::atomic<bool> g_touchDown;
extern std::atomic<float> g_touchX, g_touchY; // finger position, 0..1 of the view
#else
static std::atomic<bool> g_debugMode{true}; // no settings panel off Android: debug mode stays on
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

		// Debug mode (what typing "harrydebugmodeon" does): adds Level Select to the main menu and the console.
		// There is no keyboard on a phone to type it: the settings panel switches it (on by default).
		static UObject* debugConsole = nullptr;
		static int debugApplied = -1;
		int debugWanted = g_debugMode.load() ? 1 : 0;
		if (engine->console && (engine->console != debugConsole || debugWanted != debugApplied))
		{
			debugConsole = engine->console;
			debugApplied = debugWanted;
			engine->console->SetPropertyFromString(NameString("bDebugMode"), debugWanted ? "True" : "False");
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

#ifdef __ANDROID__
extern std::atomic<bool> g_saveRequested; // android/jni/android_main.cpp
void AndroidNotifySaved(int slot);

namespace HP1
{
	// The SAVE button (save anywhere): the same call the save books make, HPConsole.doLevelSave(slot), which runs the
	// SaveGame console command and writes the slot's GameSaveInfo (beans, house points, level name) so the slot shows in
	// Load Game. The slot is the one picked at New Game (FESlotPage.nSelectedSlot). Without one (-1, a game started
	// from Level Select) the original saves to the scratch slot 99, which the menu never lists: the first empty one of
	// the six slots is taken instead and becomes the game's slot (as New Game's pick would), the first one if all are used.
	static int FirstEmptySlot()
	{
		std::filesystem::path saveDir = (engine->packages->GetSystemFolderPath() / ".." / "save").lexically_normal();
		for (int slot = 0; slot < 6; slot++) // FESlotPage.NUM_SAVE_SLOTS
		{
			if (!std::filesystem::exists(saveDir / ("GameSaveInfo" + std::to_string(slot))))
				return slot;
		}
		return 0;
	}

	void TickSaveRequest()
	{
		if (!g_saveRequested.exchange(false) || !engine->console)
			return;

		int slot = -1;
		int selectedBefore = -1;
		UObject* slotPage = KW::ObjectProperty(KW::ObjectProperty(engine->console, "MenuBook"), "SlotPage");
		if (slotPage)
		{
			if (int* selected = static_cast<int*>(slotPage->GetProperty(NameString("nSelectedSlot"))))
				selectedBefore = *selected;
		}
		// New Game / Load Game picks 0..5. Anything else (-1: none, or a value a Level Select start leaves there: a save went
		// to slot 10 once) is no slot of the six the menu lists.
		if (selectedBefore >= 0 && selectedBefore < 6)
			slot = selectedBefore;
		if (slot < 0)
		{
			slot = FirstEmptySlot();
			if (slotPage)
				CallEvent(slotPage, NameString("SetSelectedSlot"), { ExpressionValue::IntValue(slot) });
		}

		LogMessage("SAVE button: FESlotPage.nSelectedSlot = " + std::to_string(selectedBefore) + ", saving to slot " + std::to_string(slot));
		CallEvent(engine->console, NameString("doLevelSave"), { ExpressionValue::IntValue(slot) });
		AndroidNotifySaved(slot);
	}
}
#endif
