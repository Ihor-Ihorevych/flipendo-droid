#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "VM/NativeFunc.h"
#include "VM/Frame.h"
#include "VM/Iterator.h"
#include "Engine.h"
#include <algorithm>

// Actor.TraceTexture (native 285), from AActor::execTraceTexture in HP1 Engine.dll:
//   Level->SingleLineCheck(Hit, Self, TraceEnd, TraceStart, TRACE_Level, zero extent)
//   if the hit has a BSP surface: Flags = Surf.Texture.PolyFlags | Surf.PolyFlags, return Surf.Texture
//   (bTraceDecals: returns a decal's texture instead when the hit point is inside a decal on that surface)
// Used for footstep sounds, spell targeting, etc.
// TODO(hp1): bTraceDecals isn't implemented, the surface texture is always returned.

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// IDA Engine.dll: ?execTraceTexture@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1037ABA0]
	static void NTraceTexture(UObject* Self, const vec3& TraceEnd, const vec3& TraceStart, int& Flags, std::optional<bool> bTraceDecals, UObject*& ReturnValue)
	{
		Flags = 0;
		ReturnValue = nullptr;

		CollisionHitList hits = engine->Level->Collision.Trace(TraceStart, TraceEnd, 0.0f, 0.0f, false, true, false);
		for (const CollisionHit& hit : hits)
		{
			if (!hit.Node)
				continue;
			const BspSurface& surf = engine->Level->Model->Surfaces[hit.Node->Surf];
			Flags = (surf.Material ? surf.Material->PolyFlags() : 0) | surf.PolyFlags;
			ReturnValue = surf.Material;
			return;
		}
	}

	// Actor.TraceActors (native 309) iterator. Upstream's iterator traced End->Start (hits came back
	// furthest first), returned the out parameters' old values instead of each hit's location/normal (so
	// HitLocation was (0,0,0)) and dropped BSP hits. BaseCam.CheckPosition pulls the camera to the first
	// LevelInfo/blocking hit between the goal point and Harry; with upstream's results it was moved next to
	// the world origin whenever its trace hit anything (looking up/down sent the camera out of the level).
	class HP1TraceActorsIterator : public Iterator
	{
	public:
		struct Hit { UActor* Actor; vec3 Location; vec3 Normal; };

		HP1TraceActorsIterator(UObject** actor, vec3* hitLoc, vec3* hitNorm, Array<Hit> hits)
			: OutActor(actor), OutHitLoc(hitLoc), OutHitNorm(hitNorm), Hits(std::move(hits)) { }

		bool Next() override
		{
			if (Index >= Hits.size())
			{
				*OutActor = nullptr;
				return false;
			}
			const Hit& hit = Hits[Index++];
			*OutActor = hit.Actor;
			*OutHitLoc = hit.Location;
			*OutHitNorm = hit.Normal;
			return true;
		}

		UObject** OutActor;
		vec3* OutHitLoc;
		vec3* OutHitNorm;
		Array<Hit> Hits;
		size_t Index = 0;
	};

	// IDA Engine.dll: ?execTraceActors@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040DF50]
	// IDA Engine.dll: ?MultiLineCheck@ULevel@@UAEPAUFCheckResult@@AAVFMemStack@@VFVector@@11HPAVALevelInfo@@E@Z [HP1 0x103AC620]
	// execTraceActors: MultiLineCheck(GMem, End, Start, Extent, bCheckActors=1, Level, 0) and loops over the
	// FCheckResult list writing Actor/Location/Normal. HP1's build does not filter by BaseClass (the scripts do).
	// MultiLineCheck: the first BSP hit is a hit on LevelInfo and cuts the trace off 5 units past it, then the
	// actors hit before that point are added, all sorted by distance from Start.
	static void NTraceActors(UObject* Self, UObject* BaseClass, UObject*& Actor, vec3& HitLoc, vec3& HitNorm, const vec3& End, std::optional<vec3> Start, std::optional<vec3> Extent)
	{
		UActor* self = UObject::Cast<UActor>(Self);
		vec3 start = Start ? *Start : self->Location();
		vec3 extent = Extent ? *Extent : vec3(0.0f);
		float length = std::sqrt(dot(End - start, End - start));

		Array<HP1TraceActorsIterator::Hit> hits;
		if (length > 0.0f)
		{
			struct Found { float Fraction; HP1TraceActorsIterator::Hit Hit; };
			Array<Found> found;
			float cutoff = 1.0f;
			for (const CollisionHit& hit : self->XLevel()->Collision.Trace(start, End, extent.z, extent.x, false, true, false))
			{
				// first world hit only
				vec3 loc = start + (End - start) * hit.Fraction;
				found.push_back(Found{ hit.Fraction, HP1TraceActorsIterator::Hit{ self->Level(), loc, hit.Normal } });
				cutoff = std::min((length * hit.Fraction + 5.0f) / length, 1.0f);
				break;
			}
			for (const CollisionHit& hit : self->XLevel()->Collision.Trace(start, End, extent.z, extent.x, true, false, false))
			{
				if (!hit.Actor || hit.Actor == self || hit.Fraction >= cutoff)
					continue;
				found.push_back(Found{ hit.Fraction, HP1TraceActorsIterator::Hit{ hit.Actor, start + (End - start) * hit.Fraction, hit.Normal } });
			}
			std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.Fraction < b.Fraction; });
			for (const Found& f : found)
				hits.push_back(f.Hit);
		}
		Frame::CreatedIterator = std::make_unique<HP1TraceActorsIterator>(&Actor, &HitLoc, &HitNorm, std::move(hits));
	}

	void RegisterTraceNatives()
	{
		OverrideNative(309, [] { RegisterVMNativeFunc_7("Actor", "TraceActors", &NTraceActors, 309); });
		// Upstream registers TraceTexture (Deus Ex's iterator version) at 1000; HP1's is 285.
		OverrideNative(285, [] { RegisterVMNativeFunc_5("Actor", "TraceTexture", &NTraceTexture, 285); });
	}
}
