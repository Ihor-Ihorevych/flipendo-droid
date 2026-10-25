#include "Precomp.h"
#include "HP1Mods.h"
#include "Utils/CommandLine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef __ANDROID__
extern std::atomic<bool> g_cutsceneActive; // a cutscene holds Harry (android_main.cpp)
#include <dlfcn.h>
#include <unistd.h>
#endif

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
// Android's FPS limit goes up to 120 (the phone's display), but a cutscene always runs at 60 at most: the slope bug above
// is in the cutscene walk, so the scenes play exactly as at 60 fps and only the gameplay is smoother.
namespace HP1::Mods
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::atomic<int> runtimeLimit{ -1 }; // SetFrameLimit; -1: --fps decides

		int FrameLimit()
		{
			int runtime = runtimeLimit.load();
			if (runtime >= 0)
				return Enabled() ? runtime : 0;
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
				if (!IsOn("frame-limit"))
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

#ifdef __ANDROID__
	// Android's performance hint (APerformanceHint, API 33, found at run time so the app still runs on older systems): tells
	// the scheduler the game thread is the one that sets the frame time, with what each frame cost, so it is put on a fast
	// core and the cores are clocked up in time, instead of the governor finding out a few frames late. Without it a phone
	// may run the game thread on a small core.
	namespace
	{
		using GetManager = void* (*)();
		using CreateSession = void* (*)(void*, const int32_t*, size_t, int64_t);
		using ReportDuration = int (*)(void*, int64_t);
		using UpdateTarget = int (*)(void*, int64_t);
		void* hintSession = nullptr;
		ReportDuration hintReport = nullptr;
		UpdateTarget hintUpdate = nullptr;
		int64_t hintTarget = 0;
		bool hintTried = false;
		Clock::time_point workStart;

		void HintStart()
		{
			hintTried = true;
			void* lib = dlopen("libandroid.so", RTLD_NOW);
			if (!lib)
				return;
			auto getManager = (GetManager)dlsym(lib, "APerformanceHint_getManager");
			auto create = (CreateSession)dlsym(lib, "APerformanceHint_createSession");
			hintReport = (ReportDuration)dlsym(lib, "APerformanceHint_reportActualWorkDuration");
			hintUpdate = (UpdateTarget)dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
			if (!getManager || !create || !hintReport || !hintUpdate)
				return;
			void* manager = getManager();
			if (!manager)
				return;
			int32_t tid = (int32_t)gettid();
			hintTarget = 16666666;
			hintSession = create(manager, &tid, 1, hintTarget);
		}

		// Once per frame, before the limiter waits: what the frame's own work took, against the time one frame may take
		void HintFrame(int fps)
		{
			if (!hintTried)
				HintStart();
			if (!hintSession)
				return;
			auto now = Clock::now();
			if (workStart.time_since_epoch().count() != 0)
			{
				int64_t work = std::chrono::duration_cast<std::chrono::nanoseconds>(now - workStart).count();
				int64_t target = fps > 0 ? (int64_t)(1.0e9 / fps) : 16666666;
				if (target != hintTarget)
				{
					hintTarget = target;
					hintUpdate(hintSession, target);
				}
				hintReport(hintSession, work);
			}
		}
	}
#endif

	void SetFrameLimit(int fps)
	{
		runtimeLimit = std::max(fps, 0);
	}

	void TickFrameLimit()
	{
		int fps = FrameLimit();
#ifdef __ANDROID__
		HintFrame(fps);
#endif
		if (fps <= 0)
		{
#ifdef __ANDROID__
			workStart = Clock::now();
#endif
			return;
		}
#ifdef __ANDROID__
		if (fps > 60 && g_cutsceneActive.load())
			fps = 60;
#endif

		static Clock::time_point nextFrame;
		auto frameTime = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
		auto now = Clock::now();
		if (nextFrame.time_since_epoch().count() == 0 || now - nextFrame > frameTime)
			nextFrame = now; // first frame, or a long frame (a level load): start pacing again from here
		else
			WaitUntil(nextFrame);
		nextFrame += frameTime;
#ifdef __ANDROID__
		workStart = Clock::now(); // the next frame's work starts after the wait
#endif
	}
}
