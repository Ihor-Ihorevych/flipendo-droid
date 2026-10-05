#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "KWCheck.h"
#include "Render/VisibleFrame.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/UBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "RenderDevice/RenderDevice.h"
#include "Render/RenderSubsystem.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/UPalette.h"
#include <chrono>
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

	// Coronas. Each frame (main view only) the renderer gathers them only from the lights permeating the BSP leaf of
	// the viewport's actor (its Region.iLeaf, not the camera's: in a cutscene that's where Harry stands), the map's
	// Leaves[leaf].iPermeating list plus the leaf's dynamic lights: a light with bCorona, a Skin, not destroyed, and a
	// clear line (TRACE_Movers | TRACE_Level) from the camera to it. SurrealEngine drew every visible bCorona actor,
	// which put glows on Lev_Tut2's fountain arches and Lev_Tut1's stair lamps that the original doesn't show.
	// A table of 32 coronas keeps a fade per light: every frame it drops by 3 * the real time passed, a light gathered
	// again adds twice that (new ones start there), both capped at 1, and a faded-out entry is freed. Every live entry
	// in front of the camera is drawn, occluded or not, with its Skin at the projected light location, DrawScale *
	// FX * 0.8 wide, coloured by the light's hue and saturation (no brightness) times its fade.
	// The leaf's dynamic lights (URender::LeafLights) aren't ported: SurrealEngine has no per-leaf dynamic light list.
	// IDA Render.dll: ?DrawFrame@URender@@QAEXPAUFSceneNode@@@Z [HP1 Render 0x10B254F0] (the corona block at its end)
	// IDA Render.dll: not exported: sub_10B26F10 [HP1 Render 0x10B26F10] (gathers one light; called from DrawFrame for
	// the camera leaf's permeating lights and LeafLights)
	void DrawCoronas(VisibleFrame* frame)
	{
		struct Corona { UActor* light = nullptr; float fade = 0.0f; };
		static Corona table[32];
		static ULevel* tableLevel = nullptr;
		static auto last = std::chrono::steady_clock::now();

		auto now = std::chrono::steady_clock::now();
		float step = std::chrono::duration<float>(now - last).count() * 3.0f;
		last = now;

		if (tableLevel != engine->Level)
		{
			for (Corona& c : table)
				c = Corona();
			tableLevel = engine->Level;
		}
		if (!engine->Level)
			return;

		for (Corona& c : table)
		{
			if (c.light && (c.fade -= step) < 0.0f)
				c = Corona();
		}

		UModel* model = engine->Level->Model;
		vec3 eye = frame->ViewLocation.xyz();
		UActor* viewActor = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (!viewActor || !viewActor->Region().Zone)
			return;
		int leaf = viewActor->Region().BspLeaf;
		if (leaf >= 0 && leaf < (int)model->Leaves.size() && model->Leaves[leaf].Permeating >= 0)
		{
			for (int i = model->Leaves[leaf].Permeating; i < (int)model->Lights.size() && model->Lights[i]; i++)
			{
				UActor* light = model->Lights[i];
				if (!light->bCorona() || !light->Skin() || light->bDeleteMe())
					continue;
				CheckResult hit;
				if (!SingleLineCheck(hit, nullptr, light->Location(), eye, TRACE_Movers | TRACE_Level, vec3(0.0f)))
					continue;
				Corona* entry = nullptr;
				for (Corona& c : table)
				{
					if (c.light == light)
					{
						entry = &c;
						break;
					}
				}
				if (entry)
				{
					entry->fade = std::min(entry->fade + step + step, 1.0f);
					continue;
				}
				for (Corona& c : table)
				{
					if (!c.light)
					{
						c.light = light;
						c.fade = std::min(step + step, 1.0f);
						break;
					}
				}
			}
		}

		for (Corona& c : table)
		{
			UActor* light = c.light;
			if (!light || light->bDeleteMe() || !light->Skin())
				continue;
			vec4 pos = frame->Frame.WorldToView * frame->Frame.ObjectToWorld * vec4(light->Location(), 1.0f);
			if (pos.z <= 1.0f)
				continue;
			vec4 clip = frame->Frame.Projection * pos;
			float x = frame->Frame.FX2 + clip.x / clip.w * frame->Frame.FX2;
			float y = frame->Frame.FY2 + clip.y / clip.w * frame->Frame.FY2;
			float size = frame->Frame.FX * light->DrawScale() * 0.8f;

			// FGetHSV's colour without its brightness curve.
			uint8_t hue = light->LightHue();
			float r, g, b;
			if (hue < 86)
			{
				r = (85 - hue) * 0.011764706f;
				g = hue * 0.011764706f;
				b = 0.0f;
			}
			else if (hue < 171)
			{
				r = 0.0f;
				g = (170 - hue) * 0.011764706f;
				b = (hue - 85) * 0.011764706f;
			}
			else
			{
				r = (hue - 170) * 0.011764706f;
				g = 0.0f;
				b = (255 - hue) * 0.011904762f;
			}
			float sat = light->LightSaturation() * 0.0039215689f;
			vec3 color = vec3((1.0f - r) * sat + r, (1.0f - g) * sat + g, (1.0f - b) * sat + b) * c.fade;

			UTexture* texture = light->Skin()->GetAnimTexture();
			engine->render->UpdateTexture(texture);
			TextureInfo info;
			info.CacheID = (uint64_t)(ptrdiff_t)light->Skin();
			info.Texture = texture;
			info.Format = texture->UsedFormat;
			info.Mips = texture->UsedMipmaps.data();
			info.NumMips = (int)texture->UsedMipmaps.size();
			info.USize = texture->USize();
			info.VSize = texture->VSize();
			if (texture->Palette())
				info.Palette = (TextureColor*)texture->Palette()->Colors.data();
			float width = (float)texture->UsedMipmaps.front().Width;
			float height = (float)texture->UsedMipmaps.front().Height;
			frame->Device->DrawTile(&frame->Frame, info, x - size * 0.5f, y - size * 0.5f, size, size, 0.0f, 0.0f, width, height, 2.0f,
				vec4(color, 1.0f), vec4(0.0f), PF_Translucent);
		}
	}
}
