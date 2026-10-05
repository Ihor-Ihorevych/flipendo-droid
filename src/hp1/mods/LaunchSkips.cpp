#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UConsole.h"
#include "Engine.h"

// Opt-in launch shortcuts (developer conveniences, not affected by --vanilla):
//   --skip-splash  straight to the main menu (no EA/KnowWonder/title splash screens)
//   --skip-intro   New Game goes straight to Lev_Tut1 (no storybook)

namespace HP1::Mods
{
	// HPConsole opens its FEBook (menuBook) on the "Splash" page, which steps through SplashScreens[] on a timer
	// and then changes to the main page (FEBook.Tick). Jump to the last splash screen with its time used up, so the
	// book's own script switches to the main menu on its next tick.
	static void TickSkipSplash()
	{
		static const bool skip = HasFlag("--skip-splash");
		if (!skip)
			return;
		UObject* book = ObjectProperty(engine->console, "menuBook");
		if (!book || !BoolProperty(book, "bShowSplash"))
			return;
		int num = *static_cast<int*>(book->GetProperty("numSplashScreens"));
		*static_cast<int*>(book->GetProperty("curSplashScreen")) = std::max(num - 1, 0);
		*static_cast<float*>(book->GetProperty("fShowSplashTime")) = -1.0f;
	}

	// New Game (FESlotPage) opens FEStoryBookPage on story 3 with _URLToLoad = "Lev_Tut1.unr". Move it to its last
	// page with the page time used up; its tick then turns the page, finds the story done and runs the URL.
	// Storybook interludes without a URL (triggered from inside levels) are left alone.
	static void TickSkipIntro()
	{
		static const bool skip = HasFlag("--skip-intro");
		if (!skip)
			return;
		UObject* page = ObjectProperty(ObjectProperty(engine->console, "menuBook"), "StoryBookPage");
		if (!page || !BoolProperty(page, "bWindowVisible"))
			return;
		if (static_cast<std::string*>(page->GetProperty("_URLToLoad"))->empty())
			return;
		int story = *static_cast<int*>(page->GetProperty("iCurrentStory"));
		const int* numPages = static_cast<int*>(page->GetProperty("NumStoryPages"));
		if (story < 0 || story >= 17)
			return;
		*static_cast<int*>(page->GetProperty("iCurrentPage")) = std::max(numPages[story] - 1, 0);
		*static_cast<float*>(page->GetProperty("_PageTimer")) = -1000.0f;
	}

	void TickLaunchSkips()
	{
		TickSkipSplash();
		TickSkipIntro();
	}
}
