#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Package/Package.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include <cctype>
#include <cstring>

// Path fixes (on by default, off with --vanilla): reach specs the original maps are missing, so the station-to-station
// AI (Pawn.FindPath) finds the way the level designers meant (docs/re/hp1/original_bugs.md).
// Lev_Tut3b: Peeves leaves after the duel along HPath_F1 to baseStation1, but HPath_F3 was never linked to
// baseStation1, so FindPath fails at HPath_F1 and Peeves drifts off in his reference pose instead of flying out.
namespace HP1::Mods
{
	namespace
	{
		struct MissingSpec
		{
			const char* Map;
			const char* Start;
			const char* End;
		};

		const MissingSpec MissingSpecs[] =
		{
			{ "Lev_Tut3b", "HPath_F3", "baseStation1" },
		};

		bool SameName(const std::string& a, const char* b)
		{
			size_t n = strlen(b);
			if (a.size() != n)
				return false;
			for (size_t i = 0; i < n; i++)
			{
				if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
					return false;
			}
			return true;
		}

		UNavigationPoint* FindNavigationPoint(ULevel* level, const char* name)
		{
			for (UActor* a : level->Actors)
			{
				UNavigationPoint* np = UObject::TryCast<UNavigationPoint>(a);
				if (np && SameName(np->Name.ToString(), name))
					return np;
			}
			return nullptr;
		}

		// A reach spec from start to end, listed in start's Paths (FindPath follows a spec from either end, so one
		// direction is enough for the search).
		void AddSpec(ULevel* level, UNavigationPoint* start, UNavigationPoint* end)
		{
			auto paths = start->Paths();
			int slot = -1;
			for (int i = 0; i < 16; i++)
			{
				if (paths[i] < 0)
				{
					slot = i;
					break;
				}
			}
			if (slot < 0)
				return;

			LevelReachSpec spec;
			spec.distance = (int32_t)length(end->Location() - start->Location());
			spec.startActor = start;
			spec.endActor = end;
			spec.collisionRadius = (int32_t)start->CollisionRadius();
			spec.collisionHeight = (int32_t)start->CollisionHeight();
			spec.reachFlags = R_FLY;
			level->ReachSpecs.push_back(spec);
			paths[slot] = (int)level->ReachSpecs.size() - 1;
			LogMessage("Path fix: " + start->Name.ToString() + " -> " + end->Name.ToString());
		}
	}

	void TickPathFixes()
	{
		static ULevel* checkedLevel = nullptr;
		ULevel* level = engine->Level;
		if (!Enabled() || !level || level == checkedLevel)
			return;
		checkedLevel = level;

		std::string map = level->package ? level->package->GetPackageName().ToString() : std::string();
		for (const MissingSpec& fix : MissingSpecs)
		{
			if (!SameName(map, fix.Map))
				continue;
			UNavigationPoint* start = FindNavigationPoint(level, fix.Start);
			UNavigationPoint* end = FindNavigationPoint(level, fix.End);
			if (start && end)
				AddSpec(level, start, end);
		}
	}
}
