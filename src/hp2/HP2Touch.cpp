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
#include "Packages/Core/UState.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include <atomic>
#include <fstream>
#include <cstdio>
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
	static bool g_godMode = false; // the debug menu's God mode: the health is topped up every frame
	static UActor* g_watchedStar = nullptr; // development: the star the pick command picked, followed in the log
	static int g_watchedTicks = 0;

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

		// diagnostics (temporary): the flying cheat's inputs and result, twice a second
		{
			UPlayerPawn* harry = engine->viewport->Actor();
			static float lastFly = 0.0f;
			if (harry && harry->GetStateName() == NameString("CheatFlying") && engine->LevelInfo->TimeSeconds() - lastFly > 0.5f)
			{
				lastFly = engine->LevelInfo->TimeSeconds();
				auto f = [&](const char* name) { const PropertyDataOffset o = harry->Class->GetPropertyDataOffset(NameString(name)); return o.DataOffset == (size_t)-1 ? -999.0f : harry->Value<float>(o); };
				const vec3 a = harry->Acceleration(), v = harry->Velocity(), l = harry->Location();
				LogMessage("FLY aForward " + std::to_string(f("aForward")) + " aStrafe " + std::to_string(f("aStrafe")) + " aUp " + std::to_string(f("aUp")) + " accel " + std::to_string(a.x) + "," + std::to_string(a.y) + "," + std::to_string(a.z) + " vel " + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + " loc " + std::to_string(l.x) + "," + std::to_string(l.y) + "," + std::to_string(l.z) + " phys " + std::to_string((int)harry->Physics()));
			}
		}

		// God mode: the health is kept full (HP2's health is the status manager's count of potion hearts)
		if (g_godMode)
		{
			UPlayerPawn* harry = engine->viewport->Actor();
			UObject* status = harry ? KW::ObjectProperty(harry, "managerStatus") : nullptr;
			if (status)
			{
				const int maxHealth = CallEvent(status, NameString("GetHealthPotentialCount")).ToInt();
				if (maxHealth > 0 && CallEvent(status, NameString("GetHealthCount")).ToInt() < maxHealth)
					CallEvent(status, NameString("SetHealthCount"), { ExpressionValue::IntValue(maxHealth) });
			}
		}

		// development: what the picked star does next (the pick command)
		if (g_watchedStar && ++g_watchedTicks % 20 == 0)
		{
			LogMessage("HP2 debug: star state " + g_watchedStar->GetStateName().ToString() + (g_watchedStar->bDeleteMe() ? " (deleted)" : "") + " hidden " + std::to_string((int)g_watchedStar->bHidden()));
			if (g_watchedStar->bDeleteMe() || g_watchedTicks > 600)
				g_watchedStar = nullptr;
		}

		// The debug menu (FlipendoActivity.openHp2Debug): "level <map>", "state GSTATE020", "spells"
		std::vector<std::string> commands = AndroidTakeDebugCommands();
		{
			// development: the same commands from a file (adb push hp2_debug.txt /sdcard/FlipendoHP/), one per line, taken once
			static int fileTicks = 0;
			if (++fileTicks % 30 == 0)
			{
				const char* path = "/sdcard/FlipendoHP/hp2_debug.txt";
				std::ifstream file(path);
				std::string line;
				bool any = false;
				while (std::getline(file, line))
				{
					while (!line.empty() && (line.back() == 13 || line.back() == 32))
						line.pop_back();
					if (!line.empty())
						commands.push_back(line);
					any = true;
				}
				file.close();
				if (any)
					std::remove(path);
			}
		}
		for (const std::string& command : commands)
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
			else if (verb == "fly" || verb == "ghost" || verb == "walk")
			{
				// PlayerPawn's cheats (exec Fly / Ghost / Walk) need bCheatsEnabled
				harry->SetPropertyFromString(NameString("bCheatsEnabled"), "True");
				if (verb != "walk")
				{
					// HP2's Harry never flies: his AirSpeed is 0, which caps the flying speed at 0
					harry->SetPropertyFromString(NameString("AirSpeed"), "600");
					harry->SetPropertyFromString(NameString("AccelRate"), "2048");
				}
				CallEvent(harry, NameString(verb == "fly" ? "Fly" : verb == "ghost" ? "Ghost" : "Walk"));
				LogMessage("HP2 debug: " + verb + " -> state " + harry->GetStateName().ToString() + ", physics " + std::to_string((int)harry->Physics()) + ", cheats " + (KW::BoolProperty(harry, "bCheatsEnabled") ? "on" : "off"));
			}
			else if (verb == "pick")
			{
				// development: picks up the first challenge star in the level the way a touch does (ChallengeStar / FinalStar)
				for (size_t i = 0; i < engine->Level->Actors.size(); i++)
				{
					UActor* star = engine->Level->Actors[i];
					const std::string className = star ? star->Class->Name.ToString() : std::string();
					if (className == "ChallengeStar" || className == "FinalStar")
					{
						LogMessage("HP2 debug: pick " + className + " in state " + star->GetStateName().ToString());
						CallEvent(star, NameString("DoPickupProp"));
						g_watchedStar = star;
						g_watchedTicks = 0;
						LogMessage("HP2 debug: after DoPickupProp state " + star->GetStateName().ToString() + ", EndState enabled " + std::to_string((int)star->IsEventEnabled(EventName::EndState)) + ", stateFrame " + std::to_string(star->StateFrame != nullptr) + ", func " + std::to_string(star->StateFrame && star->StateFrame->Func) + " probe " + std::to_string(star->StateFrame ? static_cast<UState*>(star->StateFrame->Func)->ProbeMask : 0) + " ignore " + std::to_string(star->StateFrame ? static_cast<UState*>(star->StateFrame->Func)->IgnoreMask : 0) + " classProbe " + std::to_string(star->Class->ProbeMask));
						break;
					}
				}
			}
			else if (verb == "god")
			{
				g_godMode = !g_godMode;
				LogMessage(std::string("HP2 debug: god mode ") + (g_godMode ? "on" : "off"));
			}
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
