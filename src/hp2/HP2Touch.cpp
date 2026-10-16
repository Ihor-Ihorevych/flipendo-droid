#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "KW.h"
#include "VM/Frame.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include <atomic>
#include <string>
#include <vector>

// Skipping a cutscene. HP2 has it in the game itself: harry.PlayerInput runs HPConsole.StartFastForward() while the HUD
// is in cutscene mode and the bSkipCutScene button is down (Enter in DefUser.ini), which fast-forwards every playing
// CutScene. A touch screen or gamepad has no Enter; the overlay's SKIP button and the pad's A press Space, which
// HP2 binds to a duel spell, so the key is turned into the same call here.
// NOT reversed: written from the hgame scripts only (harry.PlayerInput, HPConsole.StartFastForward).

#ifdef __ANDROID__
extern std::atomic<bool> g_vendorActive;   // harry.CurrVendorManager is set: a vendor's offer takes the finger as the mouse
extern std::atomic<bool> g_cutsceneActive; // a cutscene holds Harry: the overlay shows SKIP and hides the gameplay controls
extern std::atomic<int> g_frameCount;      // frames drawn: the overlay's FPS counter
extern std::atomic<bool> g_autoJump;       // the settings panel's Auto Jump switch
std::vector<std::string> AndroidTakeDebugCommands(); // the HP2 debug menu's commands (android_main.cpp)
extern std::atomic<bool> g_saveRequested;  // the overlay's SAVE button (HP1Touch.cpp takes it for HP1)
#endif

// The frame limit (the Android FPS slider, --fps) is the one the HP1 mods have; it only paces the loop.
namespace HP1::Mods { void TickFrameLimit(); } // src/hp1/mods/FrameLimit.cpp
// --cyrillic: Windows-1251 texts drawn with glyphs from Noto Sans (src/hp1/HP1Cyrillic.cpp; to move to src/knowwonder/, both games use it)
namespace HP1 { void InitCyrillic(); }

namespace HP2
{
	static bool CutsceneMode()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* hud = player ? player->myHUD() : nullptr;
		return hud && KW::BoolProperty(hud, "bCutSceneMode");
	}

	void TickTouch()
	{
		// once per frame, after the console tick: waits for the next frame's start
		HP1::Mods::TickFrameLimit();
		// the Russian edition's texts (harry2/*.rus, put in by the launcher) are Windows-1251; HP2's fonts have no Cyrillic
		HP1::InitCyrillic();

		static int restartTicks = 0;
		if (++restartTicks % 60 == 0)
			RestartStoppedLeaders();

#ifdef __ANDROID__
		g_frameCount.fetch_add(1, std::memory_order_relaxed); // the overlay's FPS counter
		if (!engine->Level || !engine->viewport)
		{
			g_cutsceneActive = false;
			g_vendorActive = false;
			return;
		}
		g_cutsceneActive = CutsceneMode();
		{
			UPlayerPawn* harry = engine->viewport->Actor();
			g_vendorActive = harry && KW::ObjectProperty(harry, "CurrVendorManager") != nullptr;
		}

		// Auto Jump (on by default, the settings panel switches it): set on every new player pawn and when it changes, as HP1 does
		// (HP1::TickTouchDefaults). Both games declare PlayerPawn.bAutoJump.
		static UObject* autoJumpPawn = nullptr;
		static int autoJumpApplied = -1;
		const int autoJumpWanted = g_autoJump.load() ? 1 : 0;
		UObject* pawn = engine->viewport->Actor();
		if (pawn && (pawn != autoJumpPawn || autoJumpWanted != autoJumpApplied))
		{
			autoJumpPawn = pawn;
			autoJumpApplied = autoJumpWanted;
			pawn->SetPropertyFromString(NameString("bAutoJump"), autoJumpWanted ? "True" : "False");
		}

		// The debug menu (FlipendoActivity.openHp2Debug): "level <map>", "state GSTATE020", "spells"
		for (const std::string& command : AndroidTakeDebugCommands())
		{
			UPlayerPawn* harry = engine->viewport->Actor();
			if (!harry)
				continue;
			const size_t space = command.find(' ');
			const std::string verb = command.substr(0, space);
			const std::string argument = space == std::string::npos ? std::string() : command.substr(space + 1);
			LogMessage("HP2 debug: " + command);
			if (verb == "level" && !argument.empty())
				CallEvent(harry, NameString("LoadLevel"), { ExpressionValue::StringValue(argument) });
			else if (verb == "state" && !argument.empty())
				CallEvent(harry, NameString("SetGameState"), { ExpressionValue::StringValue(argument) });
			else if (verb == "spells")
				CallEvent(harry, NameString("AddAllSpellsToSpellBook"));
		}

		// The SAVE button queues a save the way a SavePoint does; TickSave writes it at the end of the frame.
		if (g_saveRequested.exchange(false) && engine->viewport && engine->viewport->Actor())
			CallEvent(engine->viewport->Actor(), NameString("SaveGame"));
#endif
	}

	void KeyDown(int key)
	{
		// IK_Space
		if (key != 0x20 || !engine->console || !CutsceneMode())
			return;
		CallEvent(engine->console, NameString("StartFastForward"));
	}
}
