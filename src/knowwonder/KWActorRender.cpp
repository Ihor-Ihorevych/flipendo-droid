#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "Render/VisibleFrame.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/UBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "RenderDevice/RenderDevice.h"
#include <cmath>
#include <algorithm>

namespace KW
{
	// Actor shadows (ActorShadow, a Decal in Actor.Shadow: HarryShadow, BroomShadow, every baseChar's) are placed by
	// the renderer. While it gathers the frame's actors, every actor it would draw that has a Shadow gets
	// Shadow.Update(None) when the space under it is on screen: a box CollisionRadius wide from the actor's
	// location down 8192 units (ActorShadow.MaxShadowDist), skipped in the editor's ortho views (RendMap 13-15).
	// Update moves the decal under the actor and re-attaches it; ActorShadow.Tick detaches it again on a frame with
	// no Update, so without this call no shadow ever shows.
	// The filter is the one in front of the sprite list: not the view target (unless behind view or in a portal),
	// not hidden, inside VisibilityRadius/VisibilityHeight, the owner-see rules, and not a moving brush. The player's
	// IsActorVisible check (vtable +196) isn't ported; HP1's PlayerPawn has no override of it.
	// It runs before the BSP walk, not during it: Update relinks the shadow actor into the BSP actor lists and the
	// node decal lists, which the walk iterates. HP1 also runs SetupDynamics before drawing anything.
	// IDA Render.dll: ?SetupDynamics@URender@@UAEXPAUFSceneNode@@PAVAActor@@@Z [HP1 Render 0x10B2FB30] (the Shadow block: FindFunctionChecked(ENGINE_Update))
	void UpdateActorShadows(VisibleFrame* frame)
	{
		UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(engine->viewport->Actor());
		if (!player || !engine->Level)
			return;
		int rendMap = player->RendMap();
		if (rendMap == 13 || rendMap == 14 || rendMap == 15)
			return;

		bool behindView = player->bBehindView();
		UActor* viewTarget = player->ViewTarget();
		vec3 eye = frame->ViewLocation.xyz();

		auto& actors = engine->Level->Actors;
		for (size_t i = 0; i < actors.size(); i++)
		{
			UActor* actor = actors[i];
			if (!actor || actor->bDeleteMe())
				continue;
			UActor* shadow = Shadow(actor);
			if (!shadow || shadow->bDeleteMe())
				continue;

			if (frame->PortalDepth == 0 && !behindView && actor == viewTarget)
				continue;
			if (actor->bHidden())
				continue;

			vec3 d = actor->Location() - eye;
			float radius = VisibilityRadius(actor), height = VisibilityHeight(actor);
			if (radius != 0.0f && std::sqrt(d.x * d.x + d.y * d.y) >= radius)
				continue;
			if (height != 0.0f && std::abs(d.z) >= height)
				continue;

			bool owned = actor->IsOwnedBy(player);
			if (actor->bOnlyOwnerSee() && (!owned || behindView))
				continue;
			if (owned && actor->bOwnerNoSee() && !behindView)
				continue;

			UBrush* brush = UObject::TryCast<UBrush>(actor);
			if (brush && !actor->bStatic())
				continue;

			float r = actor->CollisionRadius();
			vec3 loc = actor->Location();
			BBox bounds(vec3(loc.x - r, loc.y - r, loc.z - 8192.0f), vec3(loc.x + r, loc.y + r, loc.z));
			if (!frame->Clipper.IsAABBVisible(bounds))
				continue;

			CallEvent(shadow, NameString("Update"), { ExpressionValue::ObjectValue(nullptr) });
		}
	}
}

namespace KW
{
	// Actor.Opacity (1 = opaque): a mesh with Opacity < 1 is alpha blended. URender::DrawLodMesh adds
	// PF_Highlighted | PF_Translucent to every face and gives each vertex alpha Opacity; KnowWonder's D3DDrv blends that
	// pair as SRCALPHA / INVSRCALPHA and drops PF_Masked (PF_Translucent clears it), and the actor sorts with the
	// translucents. HP1 fades InvisibleHarry (the cloak), Nearly Headless Nick, the Bloody Baron and Peeves with it.
	// SurrealEngine's devices blend PF_Highlighted as premultiplied alpha (ONE / INVSRCALPHA) and let PF_Translucent
	// win over it, so here the face is PF_Highlighted only and light and fog are scaled by Opacity, which is the same
	// blend (the software path of DrawLodMesh scales the light the same way).
	// Not ported: DrawLodMesh's back-to-front sort of the faces of a translucent mesh.
	// IDA Render.dll: ?DrawLodMesh@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@PAVAActor@@ABVFCoords@@K@Z [HP1 Render 0x10B0FF00] (Opacity < 1: the extra flags, vertex Light.W)
	float MeshOpacity(UActor* actor)
	{
		return std::clamp(Opacity(actor), 0.0f, 1.0f);
	}

	void ApplyOpacityFlags(float opacity, uint32_t& polyFlags)
	{
		if (opacity < 1.0f)
			polyFlags = (polyFlags & ~(PF_Translucent | PF_Modulated | PF_Masked)) | PF_Highlighted;
	}

	void ApplyOpacityVertices(float opacity, GouraudVertex* vertices, int count)
	{
		if (opacity >= 1.0f)
			return;
		for (int i = 0; i < count; i++)
		{
			vertices[i].Light *= opacity;
			vertices[i].Fog.x *= opacity;
			vertices[i].Fog.y *= opacity;
			vertices[i].Fog.z *= opacity;
			vertices[i].Alpha = opacity;
		}
	}
}
