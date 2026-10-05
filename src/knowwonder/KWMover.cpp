#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Engine.h"
#include <unordered_map>

// HP1's Mover.uc re-declares PhysAlpha and PhysRate, shadowing Actor's. Mover script (InterpolateTo etc.) and
// KnowWonder's native mover physics use the Mover copies; upstream's TickMovingBrush uses Actor's, which stay 0, so doors were triggered but never moved.
// The hook in TickMovingBrush copies the Mover values in before upstream's logic and back out afterwards.

namespace KW
{
	struct MoverProps
	{
		PropertyDataOffset ActorPhysAlpha, ActorPhysRate;
		PropertyDataOffset MoverPhysAlpha, MoverPhysRate;
	};

	// UObject::GetPropertyDataOffset returns the first (base-most) property with a name; we want the
	// Mover's own, which comes last in the class property list.
	static PropertyDataOffset LastPropertyOffset(UClass* cls, const NameString& name)
	{
		PropertyDataOffset result;
		for (UProperty* prop : cls->Properties)
			if (prop->Name == name)
				result = prop->DataOffset;
		return result;
	}

	static const MoverProps& GetMoverProps()
	{
		static MoverProps props;
		static bool initialized = false;
		if (!initialized)
		{
			UClass* actorCls = engine->packages->FindClass("Engine.Actor");
			UClass* moverCls = engine->packages->FindClass("Engine.Mover");
			props.ActorPhysAlpha = actorCls->GetPropertyDataOffset("PhysAlpha");
			props.ActorPhysRate = actorCls->GetPropertyDataOffset("PhysRate");
			props.MoverPhysAlpha = LastPropertyOffset(moverCls, "PhysAlpha");
			props.MoverPhysRate = LastPropertyOffset(moverCls, "PhysRate");
			if (props.MoverPhysAlpha.DataOffset == props.ActorPhysAlpha.DataOffset)
				Exception::Throw("HP1: expected Mover.PhysAlpha to shadow Actor.PhysAlpha");
			initialized = true;
		}
		return props;
	}

	// Mover values at Begin. If script changed them during the tick (InterpolateEnd -> InterpolateTo for the
	// next key), End keeps the script's values instead of copying the finished move back over them.
	struct Snapshot { float Alpha, Rate; };
	static std::unordered_map<UActor*, Snapshot> Snapshots;

	// IDA Engine.dll: ?physMovingBrush@AActor@@QAEXM@Z [HP1 0x104061F0] (reads the Mover-class PhysAlpha/PhysRate)
	void MoverPhysicsBegin(UActor* mover)
	{
		const MoverProps& p = GetMoverProps();
		float alpha = mover->Value<float>(p.MoverPhysAlpha);
		float rate = mover->Value<float>(p.MoverPhysRate);
		mover->Value<float>(p.ActorPhysAlpha) = alpha;
		mover->Value<float>(p.ActorPhysRate) = rate;
		Snapshots[mover] = { alpha, rate };
	}

	void MoverPhysicsEnd(UActor* mover)
	{
		const MoverProps& p = GetMoverProps();
		auto it = Snapshots.find(mover);
		bool scriptChanged = it != Snapshots.end() &&
			(mover->Value<float>(p.MoverPhysAlpha) != it->second.Alpha || mover->Value<float>(p.MoverPhysRate) != it->second.Rate);
		if (!scriptChanged)
		{
			mover->Value<float>(p.MoverPhysAlpha) = mover->Value<float>(p.ActorPhysAlpha);
			mover->Value<float>(p.MoverPhysRate) = mover->Value<float>(p.ActorPhysRate);
		}
		if (it != Snapshots.end())
			Snapshots.erase(it);
	}
}
