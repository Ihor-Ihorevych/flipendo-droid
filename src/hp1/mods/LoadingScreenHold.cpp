#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Render/RenderSubsystem.h"
#include "Engine.h"
#include <algorithm>
#include <chrono>
#include <thread>

// Loading screen hold (on by default; --no-mod=loading-screen-hold or --vanilla for none). The original shows the
// parchment with the level's title and objective (HPConsole.DrawLevelInfo) only while the level loads, which took seconds
// on 2001 machines and flashes by today. This keeps it up until HoldSeconds after it first appeared, load time included:
// on the first tick after the load, before the new level's first frame, it draws it again until then. The frame time
// after it is clamped to 1 s by Engine::CalcTimeElapsed, like after any slow load.
namespace HP1::Mods
{
	void TickLoadingScreenHold()
	{
		const float HoldSeconds = 3.5f;
		static const bool on = IsOn("loading-screen-hold");
		float age = KW::TakeLoadingScreenAge();
		if (!on || age < 0.0f || age >= HoldSeconds || !engine->LevelInfo || !engine->console || !engine->viewport)
			return;

		auto until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(HoldSeconds - age));
		std::string text = engine->LevelInfo->LevelEnterText();
		// Drawn again a few times a second, not every frame: HPConsole.DrawLevelInfo logs the URL each time
		for (auto now = std::chrono::steady_clock::now(); now < until; now = std::chrono::steady_clock::now())
		{
			engine->render->DrawLevelInfo(text);
			std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(until - now, std::chrono::milliseconds(250)));
		}
	}
}
