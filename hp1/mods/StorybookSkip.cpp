#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UConsole.h"
#include "Engine.h"

// Storybook skip (on by default, off with --vanilla): while a storybook plays (the New Game intro, the chapter
// interludes), "Press Space to skip" is shown and Space ends the whole story.
//
// FEStoryBookPage (HPMenu) turns its pages on a timer (tick: TimeSeconds - _TimeSecondsSave >= _PageTimer ->
// GotoNextPage). Moving it to its last page with the page time used up makes its own SetStoryAndPage finish the
// story: it stops the narration and runs the queued map (_URLToLoad) or fires _EventWhenDone, exactly as when the
// story ends by itself. In the original, Space only skips single pages, and only in builds with bAllowPageSkipping.

namespace HP1::Mods
{
	static UObject* ActiveStorybook()
	{
		UObject* page = ObjectProperty(ObjectProperty(engine->console, "menuBook"), "StoryBookPage");
		return page && BoolProperty(page, "bWindowVisible") ? page : nullptr;
	}

	void TickStorybookSkip()
	{
		if (!Enabled() || !SpacePressed())
			return;
		UObject* page = ActiveStorybook();
		if (!page)
			return;
		int story = *static_cast<int*>(page->GetProperty("iCurrentStory"));
		const int* numPages = static_cast<int*>(page->GetProperty("NumStoryPages"));
		if (story < 0 || story >= 17)
			return;
		*static_cast<int*>(page->GetProperty("iCurrentPage")) = std::max(numPages[story] - 1, 0);
		*static_cast<float*>(page->GetProperty("_PageTimer")) = -1000.0f;
	}

	void DrawStorybookSkip(UCanvas* canvas)
	{
		if (Enabled() && ActiveStorybook())
			DrawPrompt(canvas, "Press Space to skip");
	}
}
