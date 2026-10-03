#include "Precomp.h"
#include "HP1.h"
#include "Packages/Core/UObject.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/NativeFunc.h"

// KnowWonder's Pawn.FindPath (native 553), used by the station-to-station AI (tut1Peeves and the other
// "basestation" patrollers): the first step from startPoint towards the navigation point named DestName.

namespace HP1
{
	void OverrideNative(int index, void (*registerFunc)());

	// Depth-first search over the level's reach specs, at most 100 deep. A spec is followed in either
	// direction (to whichever end hasn't been visited yet); only navigation points of the start point's class
	// are walked through. The result is the second node of the path (the first step), or the destination
	// itself when it is next to the start. When a node runs out of specs it is scanned once more before it is
	// popped (the original's quirk; the second scan finds nothing new). The original logs every spec it looks
	// at ("startname is %s" / "endname is %s"); that debug output is left out.
	// IDA Engine.dll: ?findPath@APawn@@QAE_NAAPAVANavigationPoint@@PAVAActor@@VFName@@@Z [HP1 0x10402540]
	static UNavigationPoint* FindPath(UPawn* pawn, UActor* start, const NameString& destName)
	{
		if (!start)
			return nullptr;
		ULevel* level = pawn->XLevel();
		if (!level)
			return nullptr;

		constexpr int MaxDepth = 100;
		UActor* path[MaxDepth] = {};
		int edge[MaxDepth];
		for (int& e : edge)
			e = -1;
		Array<UActor*> visited;
		visited.push_back(start);
		path[0] = start;
		UClass* startClass = start->Class;

		int depth = 0;
		bool rescanned = false;
		while (depth >= 0)
		{
			UNavigationPoint* node = UObject::TryCast<UNavigationPoint>(path[depth]);
			int specIndex = -1;
			if (node && ++edge[depth] < 16)
				specIndex = node->Paths()[edge[depth]];
			else
				++edge[depth];

			if (specIndex < 0 || specIndex >= (int)level->ReachSpecs.size())
			{
				if (rescanned)
				{
					path[depth] = nullptr;
					edge[depth] = -1;
					depth--;
					rescanned = false;
				}
				else
				{
					rescanned = true;
				}
				if (depth >= 0)
					edge[depth] = -1;
				continue;
			}

			const LevelReachSpec& spec = level->ReachSpecs[specIndex];
			auto isVisited = [&](UActor* a) { return std::find(visited.begin(), visited.end(), a) != visited.end(); };
			UNavigationPoint* next = nullptr;
			if (!isVisited(spec.startActor))
				next = spec.startActor;
			else if (!isVisited(spec.endActor))
				next = spec.endActor;
			if (!next)
				continue;

			visited.push_back(next);
			if (next->Name == destName)
				return path[1] ? UObject::TryCast<UNavigationPoint>(path[1]) : next;

			if ((next->Class == startClass || !startClass) && depth + 1 < MaxDepth)
			{
				depth++;
				path[depth] = next;
			}
		}
		return nullptr;
	}

	// IDA Engine.dll: ?execFindPath@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D7EC0]
	static void NFindPath(UObject* self, UObject* startPoint, const NameString& destName, UObject*& returnValue)
	{
		returnValue = FindPath(UObject::Cast<UPawn>(self), UObject::TryCast<UActor>(startPoint), destName);
	}

	void RegisterNavigationNatives()
	{
		OverrideNative(553, [] { RegisterVMNativeFunc_3("Pawn", "FindPath", &NFindPath, 553); });
	}
}
