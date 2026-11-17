#include "Precomp.h"
#include "KWMeshLight.h"
#include "KWActor.h"
#include "KWCheck.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Core/UClass.h"
#include "Math/coords.h"
#include "Engine.h"
#include <algorithm>
#include <random>
#include <unordered_map>

namespace KW
{
	namespace
	{
		enum { LD_Point, LD_Plane, LD_Ambient };

		struct LightFade
		{
			UActor* Light;
			int Fade; // 0..255; bit 0 = the light could see the actor at the last check
		};

		struct ActorLightCache
		{
			std::vector<LightFade> Lights;
			float LastTime = 0.0f;
		};

		// Per frame: HP1 refreshes these once per rendered frame.
		struct MeshLightState
		{
			ULevel* Level = nullptr;
			float Time = -1.0f;
			uint32_t Frame = 0;
			int FadeStep = 0;
			bool Strobe = false;
			float Flicker[256] = {};
			std::vector<UActor*> Lights;
			std::unordered_map<UActor*, ActorLightCache> Cache;
			std::minstd_rand Random;
		};

		MeshLightState& State()
		{
			static MeshLightState state;
			return state;
		}

		void BeginFrame(UActor* actor)
		{
			MeshLightState& s = State();
			if (s.Level != engine->Level)
			{
				s.Level = engine->Level;
				s.Time = -1.0f;
				s.Cache.clear();
			}

			float now = actor->Level()->TimeSeconds();
			if (now == s.Time)
				return;

			// The fade step: 768 per second, so a light fades fully in or out in a third of a second.
			float elapsed = s.Time < 0.0f ? 0.0f : now - s.Time;
			s.FadeStep = std::clamp((int)(elapsed * 768.0f), 0, 255);
			s.Time = now;
			s.Frame++;
			s.Strobe = !s.Strobe;
			std::uniform_real_distribution<float> frand(0.0f, 1.0f);
			for (float& f : s.Flicker)
				f = frand(s.Random);

			s.Lights.clear();
			for (UActor* light : s.Level->Actors)
			{
				if (light && light->LightType() != LT_None && light->LightBrightness() != 0)
					s.Lights.push_back(light);
			}

			// Forget actors that weren't drawn for a while (destroyed ones too).
			if ((s.Frame & 255) == 0)
			{
				for (auto it = s.Cache.begin(); it != s.Cache.end();)
				{
					if (it->second.LastTime < now - 10.0f || it->second.LastTime > now)
						it = s.Cache.erase(it);
					else
						++it;
				}
			}
		}

		// (int)x for the light effect phases, wrapping like the original's 32-bit float to int.
		int PhaseInt(double x)
		{
			return (int)(int64_t)x;
		}

		float SinPhase(int phase)
		{
			// GMath's sine table: 16384 entries per turn, indexed by phase >> 2 (65536 phase units per turn).
			return std::sin(((phase >> 2) & 0x3FFF) * (2.0f * 3.14159265359f / 16384.0f));
		}

		vec3 PaletteColor(UTexture* skin, int index, float& brightness)
		{
			UPalette* palette = skin->Palette();
			if (!palette || index < 0 || index >= (int)palette->Colors.size())
				return vec3(0.0f);
			uint32_t c = palette->Colors[index];
			float r = (float)(c & 0xff), g = (float)((c >> 8) & 0xff), b = (float)((c >> 16) & 0xff);
			brightness *= (g * 3.0f + r * 2.0f + b) * 0.0018229167f;
			float len = std::sqrt(r * r + g * g + b * b);
			return len > 0.0f ? vec3(r, g, b) / len : vec3(0.0f);
		}
	}

	// IDA Engine.dll: ?FGetHSV@@YA?AVFPlane@@EEE@Z [HP1 0x10420DD0]
	vec3 GetHSV(uint8_t hue, uint8_t saturation, uint8_t brightness)
	{
		float v = brightness * 0.0054901959f;
		float scale = std::clamp(0.7f / (std::sqrt(v) + 0.01f) * v, 0.0f, 1.0f);
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
		float s = saturation * 0.0039215689f;
		return vec3(((1.0f - r) * s + r) * scale, ((1.0f - g) * s + g) * scale, ((1.0f - b) * s + b) * scale);
	}

	// IDA Render.dll: ?GlobalLighting@URender@@UAEXHPAVAActor@@AAMAAVFPlane@@@Z [HP1 0x10B06810]
	// IDA Render.dll: not exported: the LightType table funcs_10B06897 [HP1 0x10B38288] that GlobalLighting calls:
	//   sub_10B06370 (None), nullsub (Steady, Backdrop), sub_10B06390 (Pulse), sub_10B06410 (Blink), sub_10B06470
	//   (Flicker), sub_10B064B0 (Strobe), sub_10B06510 (SubtlePulse), sub_10B06590/sub_10B066D0 (TexturePalette)
	// The light's colour and brightness (0..1) this frame. The viewport's realtime flag is taken as set (in game).
	vec3 GlobalLighting(UActor* light, float& brightness)
	{
		MeshLightState& s = State();
		vec3 color = GetHSV(light->LightHue(), light->LightSaturation(), 255);
		double time = light->Level()->TimeSeconds();
		int period = light->LightPeriod();
		int phase = light->LightPhase() << 8;
		switch (light->LightType())
		{
		case LT_None:
			brightness = 0.0f;
			break;
		case LT_Pulse:
			brightness *= SinPhase(PhaseInt(time * 2293760.0 / std::max(period, 1) + phase)) * 0.39f + 0.6f;
			break;
		case LT_Blink:
			if (PhaseInt(time * 2293760.0 / (period + 1) + phase) & 1)
				brightness = 0.0f;
			break;
		case LT_Flicker:
		{
			// The original indexes its per-frame random table with the low byte of the light's address.
			float r = s.Flicker[((uintptr_t)light >> 4) & 0xff];
			brightness = r >= 0.5f ? r * brightness : 0.0f;
			break;
		}
		case LT_Strobe:
			if (s.Strobe)
				brightness = 0.0f;
			break;
		case LT_SubtlePulse:
			brightness *= SinPhase((int)std::round(time * 2293760.0 / std::max(period, 1) + phase)) * 0.09f + 0.9f;
			break;
		case LT_TexturePaletteOnce:
		{
			UTexture* skin = light->Skin();
			if (skin && skin->Palette())
			{
				UActor* defaults = light->Class->GetDefaultObject<UActor>();
				float lifeSpan = defaults ? defaults->LifeSpan() : 0.0f;
				float t = lifeSpan != 0.0f ? std::clamp(1.0f - light->LifeSpan() / lifeSpan, 0.0f, 1.0f) : 0.0f;
				color = PaletteColor(skin, (int)(t * 255.0f), brightness);
			}
			break;
		}
		case LT_TexturePaletteLoop:
		{
			UTexture* skin = light->Skin();
			if (skin && skin->Palette())
			{
				int index = (uint8_t)PhaseInt((time * 35.0 / std::max(period, 1) + light->LightPhase()) * 256.0) % 255;
				color = PaletteColor(skin, index, brightness);
			}
			break;
		}
		default:
			break;
		}
		brightness = std::clamp(brightness, 0.0f, 1.0f);
		return color;
	}

	// IDA Render.dll: not exported: sub_10B06920 [HP1 0x10B06920] (a light's info for this frame; called for every
	// light the light manager's SetupForActor, sub_10B098F0, picked)
	static void SetupLight(MeshLightInfo& info, UActor* light, int fade)
	{
		float radius = light->WorldLightRadius();
		float inner = LightRadiusInner(light) * radius * (1.0f / 256.0f);
		info.Location = light->Location();
		info.Radius = radius;
		info.InvFalloff = 1.0f / std::max(radius - inner, 1.0f);
		info.Source = LightSource(light);
		info.Direction = vec3(0.0f);
		if (info.Source == LD_Plane)
		{
			// NOT the original's space: it compares the world space X axis with camera space normals; we light in
			// world space, so the direction is world space here.
			UPawn* pawn = UObject::TryCast<UPawn>(light);
			info.Direction = Coords::Rotation(pawn ? pawn->ViewRotation() : light->Rotation()).XAxis;
		}

		float brightness = light->LightBrightness() * (1.0f / 255.0f);
		vec3 color = GlobalLighting(light, brightness);
		info.Color = color * (light->Level()->Brightness() * brightness * fade * (1.0f / 255.0f));
		info.Dark = bDarkLight(light);
		if (info.Dark)
			info.Color = -info.Color;
	}

	// The volumetric lights that fog a mesh: the original's sprite collects them from the BSP leaves it is drawn in
	// (each leaf's volumetric light list, sub_10B319A0), SetupForActor gives each its fog terms (sub_10B06920) and
	// sorts them, ambient-source lights last. Here: the leaves' list of the actor's own leaf (Region.iLeaf) only.
	// IDA Render.dll: not exported: sub_10B319A0 [HP1 0x10B319A0] (adds a leaf's volumetric lights to the sprite)
	// IDA Render.dll: not exported: sub_10B06920 [HP1 0x10B06920] (the fog terms at its end: radius, density, colour)
	static void SetupFogLights(MeshLighting& out, UActor* lightActor)
	{
		UModel* model = engine->Level->Model;
		int leaf = lightActor->Region().BspLeaf;
		if (leaf < 0 || leaf >= (int)model->Leaves.size() || model->Leaves[leaf].Volumetric < 0)
			return;

		vec3 view = engine->CameraLocation;
		for (int i = model->Leaves[leaf].Volumetric; i < (int)model->Lights.size() && model->Lights[i]; i++)
		{
			UActor* light = model->Lights[i];
			if (light->bDeleteMe() || out.NumFogLights >= (int)std::size(out.FogLights))
				continue;

			MeshFogLight& f = out.FogLights[out.NumFogLights];
			float brightness = light->LightBrightness() * (1.0f / 255.0f);
			f.Color = GlobalLighting(light, brightness) * (light->Level()->Brightness() * brightness);
			f.Density = light->VolumeBrightness() * brightness * 0.015625f;
			f.Fog = light->VolumeFog() * 0.0039215689f;
			float radius = light->WorldVolumetricRadius();
			f.RadiusSqr = radius * radius;
			f.InvRadius = 1.0f / radius;
			f.InvRadiusSqr = f.InvRadius * f.InvRadius;
			f.Offset = light->Location() - view;
			f.OffsetSqr = dot(f.Offset, f.Offset);
			f.CameraInside = f.OffsetSqr < f.RadiusSqr;
			f.Ambient = LightSource(light) == LD_Ambient;
			out.NumFogLights++;
		}
		std::stable_partition(out.FogLights, out.FogLights + out.NumFogLights, [](const MeshFogLight& f) { return !f.Ambient; });
	}

	// IDA Render.dll: not exported: sub_10B098F0 [HP1 0x10B098F0] (the light manager's SetupForActor: GLightManager's
	// vtable off_10B386D4, slot +8; called by ?DrawLodMesh@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@PAVAActor@@ABVFCoords@@K@Z)
	// IDA Render.dll: not exported: sub_10B097B0 [HP1 0x10B097B0] (adds a light to the frame's list)
	void SetupMeshLighting(MeshLighting& out, UActor* actor, UActor* lightActor, UZoneInfo* zone)
	{
		BeginFrame(actor);
		MeshLightState& s = State();

		float scaleGlow = actor->ScaleGlow();
		out.Diffuse = scaleGlow + scaleGlow;
		float cutoff = 1.0f - SpecularWidth(actor) * 0.0058823531f;
		out.SpecularCutoff = cutoff * cutoff;
		float width = 1.0f - out.SpecularCutoff;
		float specularGlow = SpecularGlow(actor);
		out.Specular = width > 0.0f ? (specularGlow + specularGlow) / (width * width) : 0.0f;
		out.ScaleGlowCurve = out.Diffuse == 1.4f;
		if (out.ScaleGlowCurve)
			out.Specular = 6.0f;
		out.ViewLocation = engine->CameraLocation;

		// AmbientGlow 255 pulses (the original runs it off the viewport clock: sin(8 t)).
		float glow = actor->AmbientGlow() == 255 ? std::sin(actor->Level()->TimeSeconds() * 8.0f) * 0.2f + 0.25f : actor->AmbientGlow() * 0.0039215689f;
		vec3 ambient = zone ? GetHSV(zone->AmbientHue(), zone->AmbientSaturation(), zone->AmbientBrightness()) : vec3(0.0f);
		out.Ambient = ambient * out.Diffuse + glow;
		float unlit = std::clamp(actor->AmbientGlow() * 0.00390625f + scaleGlow * 0.5f, 0.0f, 1.0f);
		out.Unlit = vec3(unlit);

		out.NumLights = 0;
		out.NumFogLights = 0;
		if (actor->bUnlit() || lightActor->Region().BspLeaf == -1)
			return;

		auto [cacheIt, fresh] = s.Cache.try_emplace(lightActor);
		ActorLightCache& cache = cacheIt->second;
		cache.LastTime = s.Time;

		struct Candidate
		{
			UActor* Light;
			int Importance;
			int Fade;
		};
		static std::vector<Candidate> candidates;
		candidates.clear();

		vec3 location = lightActor->Location();
		bool specialLit = lightActor->bSpecialLit();
		for (UActor* light : s.Lights)
		{
			if (light == lightActor || light->bSpecialLit() != specialLit)
				continue;
			float radius = light->WorldLightRadius();
			vec3 L = light->Location() - location;
			float distSqr = dot(L, L);
			if (radius * radius <= distSqr)
				continue;

			// Lights picked before keep their fade; new static lights fade in, new dynamic ones start lit.
			int fade = light->bStatic() ? 0 : 255;
			for (const LightFade& f : cache.Lights)
			{
				if (f.Light == light)
				{
					fade = f.Fade;
					break;
				}
			}
			int importance = (int)((1.0f - std::sqrt(distSqr) / radius) * light->LightBrightness() * 1024.0f);
			candidates.push_back({ light, importance, fade });
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.Importance > b.Importance; });

		// At most 3 static lights, dynamic ones only while fewer than 3 are picked, none under an eighth of the
		// strongest. A light must see the actor's location (rechecked every 16 frames per light).
		int threshold = -1, numStatic = 0, numPicked = 0;
		cache.Lights.clear();
		for (Candidate& c : candidates)
		{
			UActor* light = c.Light;
			bool isStatic = light->bStatic();
			bool picked = false;
			if (c.Importance >= threshold && (isStatic ? numStatic : numPicked) < 3)
			{
				if (!isStatic && light->bMovable())
				{
					picked = true;
				}
				else if (!fresh && ((s.Frame ^ (uint32_t)((uintptr_t)light >> 4)) & 0xF) != 0)
				{
					picked = (c.Fade & 1) != 0;
				}
				else
				{
					// The level's BSP only (UModel::LineCheck through the model's vtable +92, zero extent): movers never block a light.
					CheckResult hit;
					picked = ModelLineCheck(hit, ModelFrame::Level(engine->Level->Model), light->Location(), location, vec3(0.0f), 0);
					if (picked && fresh)
						c.Fade = 255;
				}
			}

			if (picked)
			{
				numPicked++;
				if (isStatic)
					numStatic++;
				if (threshold == -1)
					threshold = c.Importance / 8;
				c.Fade = std::min(c.Fade + s.FadeStep, 255) | 1;
			}
			else
			{
				c.Fade = std::max(c.Fade - s.FadeStep, 0) & ~1;
			}

			if (c.Fade > 0 && out.NumLights < (int)std::size(out.Lights))
				SetupLight(out.Lights[out.NumLights++], light, c.Fade);
			if (c.Fade > 1 && cache.Lights.size() < 16)
				cache.Lights.push_back({ light, c.Fade });
		}

		SetupFogLights(out, lightActor);
	}

	// IDA Render.dll: not exported: sub_10B02B10 [HP1 0x10B02B10] (the light manager's Light: GLightManager's vtable
	// off_10B386D4, slot +24; called per vertex by ?DrawLodMesh@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@PAVAActor@@ABVFCoords@@K@Z)
	// The original works in camera space; distances and angles are the same in world space.
	vec3 MeshLighting::Light(const vec3& location, const vec3& normal) const
	{
		vec3 result = Ambient;
		vec3 view = location - ViewLocation;
		float viewSqr = dot(view, view);
		for (int i = 0; i < NumLights; i++)
		{
			const MeshLightInfo& light = Lights[i];
			vec3 L = light.Location - location;
			float distSqr = dot(L, L);
			float dist = std::sqrt(distSqr);

			float cosine;
			if (light.Source == LD_Ambient)
			{
				cosine = 1.0f;
				L = normal * dist;
			}
			else if (light.Source == LD_Plane)
			{
				cosine = dot(normal, light.Direction);
				L = light.Direction * dist;
			}
			else
			{
				cosine = dist > 0.0f ? dot(L, normal) / dist : 0.0f;
			}
			if (ScaleGlowCurve)
				cosine = (cosine + 1.0f) * (cosine + 1.0f) - 1.5f;

			float amount = std::max(Diffuse * cosine, 0.0f);
			if (Specular > 0.0f)
			{
				vec3 reflected = L - normal * (2.0f * dot(L, normal));
				float rv = dot(reflected, view);
				if (rv > 0.0f && distSqr * viewSqr > 0.0f)
				{
					float highlight = rv * (rv / (distSqr * viewSqr)) - SpecularCutoff;
					if (highlight > 0.0f)
						amount += Specular * highlight;
				}
			}

			if (amount > 0.0f)
			{
				// Linear falloff, full inside LightRadiusInner; negative past the radius (the original doesn't clamp).
				float falloff = std::min((light.Radius - dist) * light.InvFalloff, 1.0f);
				result += light.Color * (falloff * amount);
				if (light.Dark)
					result = vec3(std::max(result.x, 0.0f), std::max(result.y, 0.0f), std::max(result.z, 0.0f));
			}
		}

		// The original clamps with an unsigned compare against 1.0f, so a negative channel also becomes 1.0.
		auto clampChannel = [](float v) {
			uint32_t bits;
			memcpy(&bits, &v, sizeof(bits));
			return bits >= 0x3F800000 ? 1.0f : v;
		};
		return vec3(clampChannel(result.x), clampChannel(result.y), clampChannel(result.z));
	}

	// The fog part of the light manager's Light: for every fog light, the density along the ray from the camera to
	// the vertex inside the light's sphere (the integral of 3 * (1 - r^2 / R^2), times Density, at most 1), doubled;
	// it adds that times the light's colour and replaces that times VolumeFog of the colour so far. An ambient-source
	// light fogs uniformly: distance / R * Density. SurrealEngine's own mesh fog used every fogball between the camera
	// and the mesh, with another density, which turned the fire crabs in Adv3DungeonQuest's green room see-through.
	// IDA Render.dll: not exported: sub_10B02B10 [HP1 0x10B02B10] (the block under flag 0x40000000, vertex +64..+76)
	vec4 MeshLighting::Fog(const vec3& location) const
	{
		vec4 fog(0.0f);
		vec3 V = location - ViewLocation;
		float depthSqr = dot(V, V);
		for (int i = 0; i < NumFogLights; i++)
		{
			const MeshFogLight& f = FogLights[i];
			float amount;
			if (f.Ambient)
			{
				amount = std::min(f.InvRadius * f.Density * std::sqrt(depthSqr), 1.0f);
			}
			else
			{
				float proj = dot(f.Offset, V);
				if ((!f.CameraInside && proj < 0.0f) || depthSqr <= 0.0f)
					continue;
				float distSqr = f.OffsetSqr - proj * proj / depthSqr; // squared distance from the light to the ray
				if (f.RadiusSqr < distSqr)
					continue;
				float depth = std::sqrt(depthSqr);
				float closest = proj / depth;
				float half = std::sqrt(f.RadiusSqr - distSqr);
				float lo = closest - depth, hi;
				bool whole = false;
				if (closest <= half)
				{
					hi = closest;
					lo = std::max(lo, -half);
				}
				else
				{
					hi = half;
					if (lo < -half)
					{
						lo = -half;
						whole = true;
					}
				}
				if (lo >= hi)
					continue;
				float k = 3.0f - distSqr * f.InvRadiusSqr * 3.0f;
				float a = hi * f.InvRadius;
				if (whole)
				{
					amount = 2.0f * a * (k - a * a) * f.Density;
				}
				else
				{
					float b = lo * f.InvRadius;
					amount = (a * (k - a * a) - (k - b * b) * b) * f.Density;
				}
				if (amount <= 0.0f)
					continue;
				amount = std::min(amount, 1.0f);
			}

			amount += amount;
			float alpha = std::min(amount * f.Fog, 1.0f);
			fog.x = std::min(amount * f.Color.x + (1.0f - alpha) * fog.x, 1.0f);
			fog.y = std::min(amount * f.Color.y + (1.0f - alpha) * fog.y, 1.0f);
			fog.z = std::min(amount * f.Color.z + (1.0f - alpha) * fog.z, 1.0f);
			fog.w = std::min(fog.w + alpha, 1.0f);
		}
		return fog;
	}
}
