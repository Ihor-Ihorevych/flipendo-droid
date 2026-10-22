#pragma once

// Frame phase timing, off unless asked for (Android: Settings > Game > Performance log; desktop: never). Every 2 seconds
// the average milliseconds per frame of each phase go to the log as "PROF ...", so a player with a slow phone can send
// the log and show where the frame goes (the phases are exclusive: a nested one is not counted in the one around it).

#include "Utils/Logger.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace KWProf
{
	enum Phase { Mods, Tick, CalcView, Audio, Lock, BspInfo, Process, Portals, Nodes, Actors, Translucent, Coronas, Overlays, Unlock, Count };
	inline const char* Names[Count] = { "limiter", "logic", "calcview", "audio", "lock", "bspinfo", "bspwalk", "portals", "surfaces", "actors", "translucent", "coronas", "overlays", "present" };
	inline std::atomic<bool> g_on{ false };
	inline int64_t g_ns[Count];
	inline int g_frames = 0;
	inline int64_t g_start = 0, g_frameNs = 0, g_last = 0;
	inline int g_current = -1;
	inline int64_t g_since = 0;

	inline int64_t Now()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	struct Scope
	{
		int prev = -1;
		bool on;
		explicit Scope(Phase phase) : on(g_on.load(std::memory_order_relaxed))
		{
			if (!on)
				return;
			int64_t now = Now();
			if (g_current >= 0)
				g_ns[g_current] += now - g_since;
			prev = g_current;
			g_current = phase;
			g_since = now;
		}
		~Scope()
		{
			if (!on || g_current < 0)
				return;
			int64_t now = Now();
			g_ns[g_current] += now - g_since;
			g_current = prev;
			g_since = now;
		}
	};

	inline void EndFrame()
	{
		if (!g_on.load(std::memory_order_relaxed))
		{
			g_last = g_start = 0;
			return;
		}
		int64_t now = Now();
		if (g_last != 0)
			g_frameNs += now - g_last;
		g_last = now;
		g_frames++;
		if (g_start == 0)
			g_start = now;
		if (now - g_start < 2000000000LL)
			return;
		char buf[64];
		std::snprintf(buf, sizeof(buf), "PROF fps %.1f frame %.2f ms |", g_frames * 1e9 / (double)(now - g_start), g_frameNs / 1e6 / g_frames);
		std::string line = buf;
		for (int i = 0; i < Count; i++)
		{
			std::snprintf(buf, sizeof(buf), " %s %.2f", Names[i], g_ns[i] / 1e6 / g_frames);
			line += buf;
			g_ns[i] = 0;
		}
		LogMessage(line);
		g_frames = 0;
		g_frameNs = 0;
		g_start = now;
	}
}
