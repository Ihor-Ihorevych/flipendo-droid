#include "Precomp.h"
#include "Test.h"
#include "mods/HP1Mods.h"

TEST(CutsceneSkip_NormalFrameKeepsItsElapsedTime)
{
	float elapsed = 0.25f;
	CHECK_EQ(HP1::Mods::CutsceneSkipSteps(elapsed, false), 1);
	CHECK_NEAR(elapsed, 0.25f, 1e-6);
}

TEST(CutsceneSkip_LowFrameRateUsesBoundedSteps)
{
	float elapsed = 1.0f / 15.0f;
	int steps = HP1::Mods::CutsceneSkipSteps(elapsed, true);
	CHECK_NEAR(elapsed, 8.0f / 15.0f, 1e-6);
	CHECK(elapsed / steps <= 1.0f / 60.0f);
}

TEST(CutsceneSkip_StalledFrameHasBoundedWork)
{
	float elapsed = 5.0f;
	int steps = HP1::Mods::CutsceneSkipSteps(elapsed, true);
	CHECK_NEAR(elapsed, 0.8f, 1e-6);
	CHECK(steps <= 48);
	CHECK(elapsed / steps <= 1.0f / 60.0f);
}

TEST(CutsceneSkip_ZeroElapsedDoesNotDivideByZero)
{
	float elapsed = 0.0f;
	CHECK_EQ(HP1::Mods::CutsceneSkipSteps(elapsed, true), 1);
	CHECK_EQ(elapsed, 0.0f);
}
