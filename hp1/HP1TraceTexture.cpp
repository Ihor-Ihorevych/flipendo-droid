#include "Precomp.h"
#include "HP1.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "VM/NativeFunc.h"
#include "Engine.h"

// Actor.TraceTexture (native 285), from AActor::execTraceTexture in HP1 Engine.dll:
//   Level->SingleLineCheck(Hit, Self, TraceEnd, TraceStart, TRACE_Level, zero extent)
//   if the hit has a BSP surface: Flags = Surf.Texture.PolyFlags | Surf.PolyFlags, return Surf.Texture
//   (bTraceDecals: returns a decal's texture instead when the hit point is inside a decal on that surface)
// Used for footstep sounds, spell targeting, etc.
// TODO(hp1): bTraceDecals isn't implemented, the surface texture is always returned.

namespace HP1
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

	void RegisterTraceNatives()
	{
		// Upstream registers TraceTexture (Deus Ex's iterator version) at 1000; HP1's is 285.
		OverrideNative(285, [] { RegisterVMNativeFunc_5("Actor", "TraceTexture", &NTraceTexture, 285); });
	}
}
