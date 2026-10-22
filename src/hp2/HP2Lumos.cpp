#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <algorithm>
#include <cmath>

// HP2's Lumos surfaces (docs/re/hp2/engine.md): BSP surfaces with PF_HighShadowDetail | PF_Modulated are walls Lumos
// fades out (with PF_Gouraud: hidden things it brings in). The player's lit Lumos marks the ones near it with
// PF_DirtyShadows for good, and from then on they fade by the player's distance.

namespace HP2
{
	namespace
	{
		constexpr uint32_t LumosFlags = PF_HighShadowDetail | PF_Modulated;

		struct PlayerProps
		{
			PropertyDataOffset bLumosOn;
			PropertyDataOffset fLumosRadius;
		};

		const PlayerProps& GetPlayerProps()
		{
			static PlayerProps props;
			static bool initialized = false;
			if (!initialized)
			{
				UClass* cls = engine->packages->FindClass("Engine.PlayerPawn");
				props.bLumosOn = cls->GetPropertyDataOffset("bLumosOn");
				props.fLumosRadius = cls->GetPropertyDataOffset("fLumosRadius");
				initialized = true;
			}
			return props;
		}

		// The viewport's actor when it is a PlayerPawn
		UActor* LumosPlayer()
		{
			UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
			return player && player->IsA("PlayerPawn") ? player : nullptr;
		}
	}

	// IDA: [HP2 Render 0x10B21DD0] URender::OccludeBsp (the bLumosOn pass before the BSP walk)
	void MarkLumosSurfaces(int viewZone)
	{
		UActor* player = LumosPlayer();
		if (!player || !player->BoolValue(GetPlayerProps().bLumosOn))
			return;

		float radius = player->Value<float>(GetPlayerProps().fLumosRadius);
		vec3 location = player->Location();
		UModel* model = engine->Level->Model;
		for (const BspNode& node : model->Nodes)
		{
			if (node.Zone0 != viewZone || node.Zone1 != viewZone)
				continue;

			BspSurface& surface = model->Surfaces[node.Surf];
			if ((surface.PolyFlags & LumosFlags) == 0 || (surface.PolyFlags & PF_DirtyShadows))
				continue;

			const BspVert* verts = &model->Vertices[node.VertPool];
			for (int i = 0; i < node.NumVertices; i++)
			{
				if (length(model->Points[verts[i].Vertex] - location) < radius)
				{
					surface.PolyFlags |= PF_DirtyShadows;
					break;
				}
			}
		}
	}

	// IDA: [HP2 Render 0x10B24F30] URender::DrawFrame (the hardware path of a BSP surface with PF_HighShadowDetail or
	// PF_Modulated), with [HP2 D3DDrv 0x10009270] UD3DRenderDevice::SetBlending, which blends PF_HighShadowDetail |
	// PF_Modulated as INVSRCALPHA / SRCALPHA by the alpha DrawFrame passes
	bool LumosSurface(const BspSurface& surface, const vec3* points, int count, uint32_t& polyFlags, float& opacity)
	{
		uint32_t flags = surface.PolyFlags;
		if ((flags & LumosFlags) == 0)
			return true;

		// Only drawn when the viewport's actor is a PlayerPawn
		UActor* player = LumosPlayer();
		if (!player)
			return false;

		bool appear = (flags & PF_Gouraud) != 0;
		int alpha;
		if ((flags & PF_DirtyShadows) == 0)
		{
			alpha = appear ? 255 : 0;
		}
		else
		{
			if (count == 0)
				return false;
			vec3 location = player->Location();
			float nearest = 99999.0f;
			for (int i = 0; i < count; i++)
				nearest = std::min(nearest, length(points[i] - location));
			float ratio = std::min(nearest / (player->Value<float>(GetPlayerProps().fLumosRadius) * 0.8f), 1.0f);
			if (appear)
				alpha = std::max((int)(ratio * 255.0f), 100);
			else
				alpha = std::min((int)(255.0f - ratio * 255.0f), 200);
		}

		// The blend needs both flags; a surface with one of them draws as its flags say
		if ((flags & LumosFlags) != LumosFlags)
			return true;

		// The surface's colour weighs 1 - alpha, what is behind it alpha: premultiplied PF_Highlighted with that
		// opacity. At 0 it is drawn solid, at 255 not at all.
		if (alpha >= 255)
			return false;
		polyFlags &= ~(LumosFlags | PF_Translucent);
		if (alpha > 0)
		{
			polyFlags |= PF_Highlighted;
			opacity = 1.0f - alpha / 255.0f;
		}
		return true;
	}
}
