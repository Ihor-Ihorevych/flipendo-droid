#include "Precomp.h"
#include "HP1Mods.h"
#include "Utils/CommandLine.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// Frame limit (on by default at 60 fps; --fps=<n> for another limit, --fps=0 or --vanilla for none). The original was
// played at 60 Hz or less, and some of its per-frame movement breaks at high frame rates (docs/re/hp1/original_bugs.md):
// baseChar.CutMovingTo slides a walking NPC back down a slope by a fixed amount each frame, so above ~110 fps Lev_Tut3's
// students stop at a ramp. Without the limit the game runs at the monitor's refresh rate (vsync), or uncapped.
// TickMods runs once per frame after the console tick, so waiting there for the next frame's start paces the whole loop;
// the engine's frame time (Engine::CalcTimeElapsed) then includes the wait.
namespace HP1::Mods
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		int FrameLimit()
		{
			static int fps = -1;
			if (fps < 0)
			{
				fps = 60;
				if (commandline)
				{
					std::string arg = commandline->GetArg("", "--fps", "");
					if (!arg.empty())
						fps = std::max(std::atoi(arg.c_str()), 0);
				}
				if (!Enabled())
					fps = 0;
			}
			return fps;
		}

		// Sleeps until shortly before the deadline (Windows' default sleep is only accurate to ~15 ms, a
		// high-resolution waitable timer to well under 1 ms), then spins the rest
		void WaitUntil(Clock::time_point deadline)
		{
#ifdef _WIN32
			static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			auto remaining = deadline - Clock::now() - std::chrono::microseconds(500);
			if (timer && remaining > Clock::duration::zero())
			{
				LARGE_INTEGER due;
				due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100; // relative, 100 ns units
				if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
					WaitForSingleObject(timer, INFINITE);
			}
#else
			auto remaining = deadline - Clock::now() - std::chrono::milliseconds(1);
			if (remaining > Clock::duration::zero())
				std::this_thread::sleep_for(remaining);
#endif
			while (Clock::now() < deadline)
				std::this_thread::yield();
		}
	}

	void TickFrameLimit()
	{
		int fps = FrameLimit();
		if (fps <= 0)
			return;

		static Clock::time_point nextFrame;
		auto frameTime = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
		auto now = Clock::now();
		if (nextFrame.time_since_epoch().count() == 0 || now - nextFrame > frameTime)
			nextFrame = now; // first frame, or a long frame (a level load): start pacing again from here
		else
			WaitUntil(nextFrame);
		nextFrame += frameTime;
	}
}
