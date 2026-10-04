#include "Precomp.h"
#include "HP1.h"
#include "HP1ParticleFX.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Render/VisibleFrame.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Light/LightSystem.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include <cmath>

// Drawing of ParticleFX systems (DrawType DT_Particles), from HP1 Render.dll (UnParticleRn.cpp).
// URender::DrawParticleSystem updates the system, then runs one or more passes over the particle list. A pass is a
// functor (vtable: light, fill, primitives per particle, vertices per primitive) that the pass driver sub_10B15090
// calls per particle; it fills screen-space vertices and returns a clip outcode (124 = skip the particle).
// Which passes run depends on RenderPrimitive, and they stack (see docs/re/particles.md, "Rendering"):
//   Line:      line, shard, liquid, billboard     Shard: shard, liquid, billboard
//   Liquid:    liquid, billboard                  Billboard: billboard
//   TriTube:   tube pass, then appFailAssert (a fatal error in the original; no HP1 content uses it)
// bShellOnly replaces the billboard pass by the shell pass (particle positions in screen space).
//
// The original builds its vertices in screen space. Every vertex of a liquid/streak/shell polygon has the depth of
// the particle, so here the screen offsets are turned back into world offsets in the plane facing the camera at
// that depth (pixels / RZ along the view's right and down axes), which projects to the same pixels.

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

		// What the passes share: the frame's view axes and projection, the texture and the system's light colour.
		struct PassContext
		{
			VisibleFrame* Frame = nullptr;
			UActor* Actor = nullptr;
			TextureInfo Texinfo;
			uint32_t PolyFlags = 0;
			float UMax = 1.0f, VMax = 1.0f;
			vec3 ViewLocation, Forward, Right, Down;
			float ProjZ = 1.0f; // pixels per unit at depth 1 (FX2 / tan(FovAngle / 2))
			vec4 LightColor = vec4(1.0f);

			float Depth(const vec3& p) const { return dot(p - ViewLocation, Forward); }

			// A point offset by (dx, dy) world units along the screen's right and down axes.
			vec3 Offset(const vec3& p, float dx, float dy) const { return p + Right * dx + Down * dy; }

			// The world point behind screen pixel (sx, sy) at depth z.
			vec3 Unproject(float sx, float sy, float z) const
			{
				const SceneNode& f = Frame->Frame;
				return ViewLocation + Forward * z + Right * ((sx - f.FX2) * z / ProjZ) + Down * ((sy - f.FY2) * z / ProjZ);
			}
		};

		// Vertex colour of a particle: the system's light colour times the particle colour, faded by alpha.
		// Modulated systems fade towards grey (0.5) instead of black.
		// IDA Render.dll: not exported: sub_10B16AB0 [HP1 Render 0x10B16AB0] (light slot of the billboard/shard/liquid/shell passes; vtable 0x10B388F8)
		vec3 ParticleLight(const PassContext& ctx, const Particle& p)
		{
			vec3 light = ctx.LightColor.xyz() * p.Color.xyz();
			float alpha = ctx.Actor->ScaleGlow() * p.Alpha;
			if (alpha < 1.0f)
			{
				light *= alpha;
				if (ctx.Actor->Style() == STY_Modulated)
					light += vec3((1.0f - alpha) * 0.5f);
			}
			return light;
		}

		// UVs in texels; the original's v=0 row is at the far end of each primitive (the last row of a billboard is at
		// the top of the quad).
		void DrawPolygon(PassContext& ctx, const vec3* points, const vec2* uvs, const vec3* lights, int count)
		{
			GouraudVertex vertices[4];
			for (int i = 0; i < count; i++)
			{
				vertices[i].Point = points[i];
				vertices[i].UV = { uvs[i].x * ctx.UMax, uvs[i].y * ctx.VMax };
				vertices[i].Light = lights[i];
				vertices[i].Fog = vec4(0.0f);
			}
			ctx.Frame->Device->DrawGouraudPolygon(&ctx.Frame->Frame, ctx.Texinfo, vertices, count, ctx.PolyFlags);
		}

		void DrawPolygon(PassContext& ctx, const vec3* points, const vec2* uvs, const vec3& light, int count)
		{
			vec3 lights[4] = { light, light, light, light };
			DrawPolygon(ctx, points, uvs, lights, count);
		}

		// Billboard: a camera-facing quad of Width x Length, turned by Spin when the particle spins. A particle that would
		// cover too much of the screen fades: its screen area weighted by (PriorityTag+1)/4 is compared with half the
		// screen (URender+0x5C, always 1.0, scales the budget), and past it Alpha is lowered for good (min with
		// 2 - weighted/budget); at zero the particle is skipped.
		// IDA Render.dll: not exported: sub_10B184D0 [HP1 Render 0x10B184D0] (fill slot; vtable 0x10B388F8)
		// IDA Render.dll: not exported: sub_10B16A10 [HP1 Render 0x10B16A10] (pass base: budget = FX * FY * URender+0x5C * 0.5)
		void BillboardPass(PassContext& ctx, ParticleSystemState& state)
		{
			const SceneNode& f = ctx.Frame->Frame;
			float budget = f.FX * f.FY * 0.5f;
			for (Particle& p : state.Particles)
			{
				// Particles closer than one unit in front of the camera are rejected.
				float z = ctx.Depth(p.Position);
				if (z < 1.0f)
					continue;

				float rz = ctx.ProjZ / z;
				float area = p.Width * p.Length * rz * rz;
				float weighted = (p.PriorityTag + 1) * area * 0.25f;
				if (weighted > budget)
				{
					p.Alpha = std::min(p.Alpha, (budget + budget - weighted) / budget);
					if (p.Alpha <= 0.0f)
						continue;
				}

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
				static const vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
				vec3 points[4];
				for (int i = 0; i < 4; i++)
					points[i] = ctx.Offset(p.Position, corners[i].x, corners[i].y);
				DrawPolygon(ctx, points, uvs, ParticleLight(ctx, p), 4);
			}
		}

		// Shell: Position is in screen space (X, Y in pixels, Z = depth); a pixel-snapped Width x Length quad that isn't
		// scaled by distance. Only HPConsole's commented-out mouse particle test sets bShellOnly. The original's vertex
		// order (bottom-left, top-right, top-left, bottom-right) is kept; as a fan it leaves out the right wedge.
		// IDA Render.dll: not exported: sub_10B16C70 [HP1 Render 0x10B16C70] (fill slot; vtable 0x10B388E4)
		void ShellPass(PassContext& ctx, ParticleSystemState& state)
		{
			for (const Particle& p : state.Particles)
			{
				float z = p.Position.z;
				if (z < 1.0f)
					continue;
				float hw = (float)(int)(p.Width * 0.5f);
				float hh = (float)(int)(p.Length * 0.5f);
				float sx = p.Position.x, sy = p.Position.y;
				vec3 points[4] = {
					ctx.Unproject(sx - hw, sy + hh, z),
					ctx.Unproject(sx + hw, sy - hh, z),
					ctx.Unproject(sx - hw, sy - hh, z),
					ctx.Unproject(sx + hw, sy + hh, z)
				};
				static const vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f } };
				DrawPolygon(ctx, points, uvs, ParticleLight(ctx, p), 4);
			}
		}

		// A falling drop: a kite stretched along the particle's motion on screen. The tail is Velocity's direction scaled
		// by sqrt(speed) * Length; the radius keeps the volume of a sphere of diameter Width (r = sqrt(W³ / (|tail| + W))).
		// The kite has the radius behind and to the sides of the head and reaches towards the projected tail.
		// IDA Render.dll: not exported: sub_10B15B50 [HP1 Render 0x10B15B50] (called by the liquid fill for particles that stopped dripping)
		void DrawStreak(PassContext& ctx, const Particle& p, float rz, const vec3& light)
		{
			float speed = length(p.Velocity);
			vec3 dir = speed > 1e-8f ? p.Velocity * (1.0f / speed) : vec3(0.0f);
			vec3 tail = dir * (std::sqrt(speed) * p.Length);
			float volume = p.Width * p.Width * p.Width * 1.0471967f;
			float r = std::sqrt(volume * 3.0f / ((length(tail) + p.Width) * 3.1415999f));

			// The tail end in view space, relative to the head.
			vec3 d = (p.Position - tail) - p.Position;
			vec3 dv(dot(d, ctx.Right), dot(d, ctx.Down), dot(d, ctx.Forward));
			float len2d = std::sqrt(dv.x * dv.x + dv.y * dv.y);
			float lenTail = r;
			if (len2d > r)
				lenTail = len2d / length(dv) * (len2d - r) + r;

			// Screen direction from the head to the projected tail.
			vec3 tailPoint = p.Position - tail;
			float zt = ctx.Depth(tailPoint);
			if (zt < 1.0f)
				return;
			float rzt = ctx.ProjZ / zt;
			vec2 headScreen(dot(p.Position - ctx.ViewLocation, ctx.Right) * rz, dot(p.Position - ctx.ViewLocation, ctx.Down) * rz);
			vec2 tailScreen(dot(tailPoint - ctx.ViewLocation, ctx.Right) * rzt, dot(tailPoint - ctx.ViewLocation, ctx.Down) * rzt);
			vec2 u = tailScreen - headScreen;
			float ulen = length(u);
			if (ulen < 1e-6f)
				return;
			u *= 1.0f / ulen;

			vec3 points[4] = {
				ctx.Offset(p.Position, -u.y * r, u.x * r),
				ctx.Offset(p.Position, -u.x * r, -u.y * r),
				ctx.Offset(p.Position, u.y * r, -u.x * r),
				ctx.Offset(p.Position, u.x * lenTail, u.y * lenTail)
			};
			static const vec2 uvs[4] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };
			DrawPolygon(ctx, points, uvs, light, 4);
		}

		// Liquid: a hanging drop (DripTimer > 0) is a diamond under the particle, Width wide and Length tall (widest at
		// 3/4 of its height); a falling one is a streak.
		// IDA Render.dll: not exported: sub_10B17CE0 [HP1 Render 0x10B17CE0] (fill slot; vtable 0x10B3890C)
		void LiquidPass(PassContext& ctx, ParticleSystemState& state)
		{
			for (const Particle& p : state.Particles)
			{
				float z = ctx.Depth(p.Position);
				if (z < 1.0f)
					continue;
				float rz = ctx.ProjZ / z;
				vec3 light = ParticleLight(ctx, p);
				if (p.DripTimer <= 0.0f)
				{
					DrawStreak(ctx, p, rz, light);
					continue;
				}
				float hw = p.Width * 0.5f;
				float hh = p.Length * 0.5f;
				vec3 points[4] = {
					p.Position,
					ctx.Offset(p.Position, hw, hh * 1.5f),
					ctx.Offset(p.Position, 0.0f, hh * 2.0f),
					ctx.Offset(p.Position, -hw, hh * 1.5f)
				};
				static const vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
				DrawPolygon(ctx, points, uvs, light, 4);
			}
		}

		// Shard: a world-space triangle (Length along local X, Width along Y) turned by the rotator
		// (Pitch = Yaw = int(Spin * 65535), Roll = 0) around the particle. NOT verified: the original moves and rotates
		// GMath.UnitCoords by Position and that rotator; the exact handedness of the turn wasn't checked.
		// IDA Render.dll: not exported: sub_10B17350 [HP1 Render 0x10B17350] (fill slot; vtable 0x10B38920)
		void ShardPass(PassContext& ctx, ParticleSystemState& state)
		{
			for (const Particle& p : state.Particles)
			{
				int angle = (int)(int64_t)(p.Spin * 65535.0f);
				vec3 x, y, z;
				Coords::Rotation(Rotator(angle, angle, 0)).GetAxes(x, y, z);
				float hl = p.Length * 0.5f;
				float hw = p.Width * 0.5f;
				vec3 points[3] = {
					p.Position + x * -hl + y * -hw,
					p.Position + x * hl + y * hw,
					p.Position + x * -hl + y * hw
				};
				static const vec2 uvs[3] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } };
				DrawPolygon(ctx, points, uvs, ParticleLight(ctx, p), 3);
			}
		}

		// Line: a ribbon through the particles in list order, Width to each side, facing the camera. The side vector of
		// each joint is perpendicular to the view ray and the direction to the next particle (the last particle keeps the
		// previous direction), flipped to agree with the previous joint. The first particle only starts the ribbon.
		// Vertex colours: the previous joint's particle at its end, the current one at the other (the original's light
		// slot sub_10B1B330 reads the neighbouring particles; this split is approximated).
		// IDA Render.dll: not exported: sub_10B1B7D0 [HP1 Render 0x10B1B7D0] (fill slot; vtable 0x10B38934)
		// IDA Render.dll: not exported: sub_10B1B330 [HP1 Render 0x10B1B330] (light slot; vtable 0x10B38934)
		void LinePass(PassContext& ctx, ParticleSystemState& state)
		{
			auto safeNormal = [](const vec3& v) { return v * (1.0f / std::sqrt(dot(v, v) + 1e-12f)); };
			const Particle* prev = nullptr;
			vec3 prevSide(0.0f), dir(0.0f);
			for (auto it = state.Particles.begin(); it != state.Particles.end(); ++it)
			{
				const Particle& p = *it;
				auto nextIt = std::next(it);
				const Particle* next = nextIt != state.Particles.end() ? &*nextIt : nullptr;
				if (next)
					dir = safeNormal(next->Position - p.Position);
				vec3 toCamera = safeNormal(p.Position - ctx.ViewLocation);
				vec3 side = safeNormal(cross(toCamera, dir));
				if (prev)
				{
					if (dot(side, prevSide) < 0.0f)
						side = -side;
					vec3 prevOffset = prevSide * prev->Width;
					vec3 offset = side * p.Width;
					vec3 points[4] = { prev->Position - prevOffset, p.Position - offset, p.Position + offset, prev->Position + prevOffset };
					static const vec2 uvs[4] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };
					vec3 prevLight = ParticleLight(ctx, *prev);
					vec3 light = ParticleLight(ctx, p);
					vec3 lights[4] = { prevLight, light, light, prevLight };
					DrawPolygon(ctx, points, uvs, lights, 4);
				}
				prev = &p;
				prevSide = side;
			}
		}

		// LightColor: white for unlit systems; otherwise the light manager lights the system at its location. Here that is
		// SurrealEngine's vertex light at Location with a normal facing the camera.
		vec4 SystemLightColor(PassContext& ctx)
		{
			UActor* actor = ctx.Actor;
			if (actor->bUnlit())
				return vec4(1.0f);
			auto lightsys = &engine->Level->Light;
			lightsys->UpdateLightList(actor);
			VertexLight vertexLight;
			lightsys->InitVertexLight(vertexLight, actor, engine->GetZoneActor(actor->Region().ZoneNumber));
			return vec4(vertexLight.GetVertexLight(actor->Location(), -ctx.Forward, false, true), 1.0f);
		}
	}

	// IDA Render.dll: ?DrawParticleSystem@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@@Z [HP1 Render 0x10B15830]
	// IDA Render.dll: not exported: sub_10B15090 [HP1 Render 0x10B15090] (pass driver: texture, poly flags, lighting, batching)
	void DrawParticleSystem(VisibleFrame* frame, UActor* actor)
	{
		if (!UpdateParticleSystem(actor, 0.0f))
			return;
		ParticleSystemState* state = GetParticleSystem(actor);
		if (!state || state->Particles.empty())
			return;

		uint8_t primitive = PFX::RenderPrimitive(actor);
		if (primitive > PPRIM_TriTube)
			return;
		if (primitive == PPRIM_TriTube)
		{
			static bool logged = false;
			if (!logged)
			{
				logged = true;
				LogMessage("HP1: ParticleFX RenderPrimitive PPRIM_TriTube is not drawn (the original fails an assert after drawing it)");
			}
			return;
		}

		UTexture* texture = PFX::Texture0(actor);
		if (!texture)
			return;
		engine->render->UpdateTexture(texture);
		texture = texture->GetAnimTexture();
		if (!texture)
			return;
		engine->render->UpdateTexture(texture);

		PassContext ctx;
		ctx.Frame = frame;
		ctx.Actor = actor;
		ctx.PolyFlags = PF_Unlit | PF_TwoSided;
		switch (actor->Style())
		{
		case STY_Masked: ctx.PolyFlags |= PF_Masked; break;
		case STY_Translucent: ctx.PolyFlags |= PF_Translucent; break;
		case STY_Modulated: ctx.PolyFlags |= PF_Modulated; break;
		default: break;
		}
		ctx.PolyFlags |= texture->PolyFlags();
		if (ctx.PolyFlags & PF_Invisible)
			return;

		SetupTextureInfo(ctx.Texinfo, texture);
		ctx.UMax = (float)std::max(texture->USize() - 1, 1);
		ctx.VMax = (float)std::max(texture->VSize() - 1, 1);
		ctx.ViewLocation = frame->ViewLocation.xyz();
		ctx.Forward = frame->ViewRotation.XAxis;
		ctx.Right = frame->ViewRotation.YAxis;
		ctx.Down = -frame->ViewRotation.ZAxis;
		ctx.ProjZ = frame->Frame.FX2 / std::tan(radians(frame->Frame.FovAngle) * 0.5f);

		PFX::LightColor(actor) = SystemLightColor(ctx);
		ctx.LightColor = PFX::LightColor(actor);

		switch (primitive)
		{
		case PPRIM_Line:
			LinePass(ctx, *state);
			[[fallthrough]];
		case PPRIM_Shard:
			ShardPass(ctx, *state);
			[[fallthrough]];
		case PPRIM_Liquid:
			LiquidPass(ctx, *state);
			[[fallthrough]];
		case PPRIM_Billboard:
		default:
			if (PFX::bShellOnly(actor))
				ShellPass(ctx, *state);
			else
				BillboardPass(ctx, *state);
			break;
		}
		engine->render->Stats.Actors++;
	}
}
