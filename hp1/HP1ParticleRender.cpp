#include "Precomp.h"
#include "HP1.h"
#include "HP1ParticleFX.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Render/VisibleFrame.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include <cmath>

// Drawing of ParticleFX systems (DrawType DT_Particles), from HP1 Render.dll (UnParticleRn.cpp).
// URender::DrawParticleSystem updates the system, then runs per-primitive passes over the particle list
// that fill FTransTexture quads for the render device. Only the billboard pass is ported; the other primitives
// (Line, Liquid, Shard, TriTube, and the bShellOnly pass) are drawn as billboards for now.

namespace HP1
{
	namespace
	{
		enum EParticlePrimitive : uint8_t { PPRIM_Line, PPRIM_Billboard, PPRIM_Liquid, PPRIM_Shard, PPRIM_TriTube };

		void SetupTextureInfo(TextureInfo& texinfo, UTexture* texture)
		{
			texinfo.Texture = texture;
			texinfo.CacheID = (uint64_t)(ptrdiff_t)texture;
			texinfo.bRealtimeChanged = texture->TextureModified;
			if (texture->TextureModified)
				texture->TextureModified = false;
			texinfo.Format = texture->UsedFormat;
			texinfo.Mips = texture->UsedMipmaps.data();
			texinfo.NumMips = (int)texture->UsedMipmaps.size();
			texinfo.USize = texture->USize();
			texinfo.VSize = texture->VSize();
			if (texture->Palette())
				texinfo.Palette = (TextureColor*)texture->Palette()->Colors.data();
		}
	}

	// Vertex colour of a particle: the system's light colour times the particle colour, faded by alpha.
	// Modulated systems fade towards grey (0.5) instead of black.
	// IDA Render.dll: not exported: sub_10B16AB0 [HP1 Render 0x10B16AB0] (first vtable slot of every particle pass; vtable 0x10B388F8)
	static vec3 ParticleLight(UActor* actor, const vec4& lightColor, const Particle& p)
	{
		vec3 light = lightColor.xyz() * p.Color.xyz();
		float alpha = actor->ScaleGlow() * p.Alpha;
		if (alpha < 1.0f)
		{
			light *= alpha;
			if (actor->Style() == STY_Modulated)
				light += vec3((1.0f - alpha) * 0.5f);
		}
		return light;
	}

	// IDA Render.dll: ?DrawParticleSystem@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@@Z [HP1 Render 0x10B15830]
	// IDA Render.dll: not exported: sub_10B15090 [HP1 Render 0x10B15090] (pass driver: texture, poly flags, lighting, batching)
	// IDA Render.dll: not exported: sub_10B184D0 [HP1 Render 0x10B184D0] (billboard quad; vtable 0x10B388F8 slot 1)
	void DrawParticleSystem(VisibleFrame* frame, UActor* actor)
	{
		if (!UpdateParticleSystem(actor, 0.0f))
			return;
		ParticleSystemState* state = GetParticleSystem(actor);
		if (!state || state->Particles.empty())
			return;

		uint8_t primitive = PFX::RenderPrimitive(actor);
		if (primitive != PPRIM_Billboard || PFX::bShellOnly(actor))
		{
			static bool logged[6] = {};
			int slot = PFX::bShellOnly(actor) ? 5 : std::min<int>(primitive, 4);
			if (!logged[slot])
			{
				logged[slot] = true;
				LogMessage("HP1: ParticleFX " + std::string(PFX::bShellOnly(actor) ? "shell" : "primitive " + std::to_string(primitive)) + " is drawn as billboards (not ported yet)");
			}
		}

		UTexture* texture = PFX::Texture0(actor);
		if (!texture)
			return;
		engine->render->UpdateTexture(texture);
		texture = texture->GetAnimTexture();
		if (!texture)
			return;
		engine->render->UpdateTexture(texture);

		uint32_t polyflags = PF_Unlit | PF_TwoSided;
		switch (actor->Style())
		{
		case STY_Masked: polyflags |= PF_Masked; break;
		case STY_Translucent: polyflags |= PF_Translucent; break;
		case STY_Modulated: polyflags |= PF_Modulated; break;
		default: break;
		}
		polyflags |= texture->PolyFlags();
		if (polyflags & PF_Invisible)
			return;

		// TODO(hp1): lit systems (bUnlit=False) get their LightColor from the light manager in the original.
		vec4 lightColor(1.0f);

		TextureInfo texinfo;
		SetupTextureInfo(texinfo, texture);
		float umax = (float)std::max(texture->USize() - 1, 1);
		float vmax = (float)std::max(texture->VSize() - 1, 1);

		vec3 viewLocation = frame->ViewLocation.xyz();
		vec3 viewForward = frame->ViewRotation.XAxis;
		vec3 viewRight = frame->ViewRotation.YAxis;
		vec3 viewDown = -frame->ViewRotation.ZAxis;

		for (const Particle& p : state->Particles)
		{
			// Particles closer than one unit in front of the camera are rejected.
			if (dot(p.Position - viewLocation, viewForward) < 1.0f)
				continue;

			float w = p.Width * 0.5f;
			float h = p.Length * 0.5f;
			// Corners in view space (X right, Y down), turned by the particle's spin when it has a spin rate.
			vec2 corners[4] = { { -w, -h }, { w, -h }, { w, h }, { -w, h } };
			if (p.SpinRate != 0.0f)
			{
				float c = std::cos(p.Spin);
				float s = std::sin(p.Spin);
				for (vec2& v : corners)
					v = vec2(c * v.x - s * v.y, s * v.x + c * v.y);
			}
			// The texture's last row is at the top of the quad.
			static const vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };

			vec3 light = ParticleLight(actor, lightColor, p);
			GouraudVertex vertices[4];
			for (int i = 0; i < 4; i++)
			{
				vertices[i].Point = p.Position + viewRight * corners[i].x + viewDown * corners[i].y;
				vertices[i].UV = { uvs[i].x * umax, uvs[i].y * vmax };
				vertices[i].Light = light;
				vertices[i].Fog = vec4(0.0f);
			}
			frame->Device->DrawGouraudPolygon(&frame->Frame, texinfo, vertices, 4, polyflags);
		}
		engine->render->Stats.Actors++;
	}
}
