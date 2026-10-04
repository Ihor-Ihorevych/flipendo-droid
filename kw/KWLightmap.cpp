#include "Precomp.h"
#include "KW.h"
#include "KWActor.h"
#include "KWMeshLight.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Math/coords.h"
#include <algorithm>

// HP1's BSP light maps (Render.dll's light manager), on top of SurrealEngine's LightmapBuilder: which lumels a light
// reaches (LightSource, LightRadiusInner) and how the lights add up. docs/re/engine/lighting.md, "Light maps".
namespace KW
{
	namespace
	{
		enum { LD_Point, LD_Plane, LD_Ambient };

		// HP1's light maps are 7 bits per channel (127 = full). D3DDrv uploads them doubled and draws them with
		// MODULATE2X; SurrealEngine's shaders double the light map, so 127 is 254/255 here.
		constexpr float LightmapUnit = 2.0f / 255.0f;
		constexpr float LightmapFull = 127.0f * LightmapUnit;
		// The ambient fill and every light come out half as bright as the terms below say, measured against the
		// original (Lev_Tut1 intro, docs/re/engine/lighting.md "Measured against the original"); the factor isn't
		// found in the code yet. The 127 cap stays: the original's highlights go above the texture's own brightness.
		constexpr float MeasuredScale = 0.5f;

		// The falloff tables (sub_10B023A0): 2t³ - 3t² + 1 at t = sqrt((m + 1) / 4096), and the same divided by t for
		// point lights, whose incidence factor is the light's distance from the surface plane over the radius.
		float Falloff(int m, bool divideByT)
		{
			float t = std::sqrt((m + 1) * (1.0f / 4096.0f));
			float f = 2.0f * t * t * t - 3.0f * t * t + 1.0f;
			return divideByT ? f / t : f;
		}
	}

	// IDA Render.dll: not exported: sub_10B06920 [HP1 0x10B06920] (a light's info: the light map terms; called for
	// every light of a surface by sub_10B077F0, the light manager's light map builder)
	// IDA Render.dll: not exported: loc_10B037C0 [HP1 0x10B037C0] (LE_None; LightEffect table off_10B38110, entry 0;
	// with a LightRadiusInner it runs sub_10B0CA70, the same with a clamp)
	// IDA Render.dll: not exported: sub_10B0CA70 [HP1 0x10B0CA70]
	// IDA Render.dll: not exported: sub_10B05DC0 [HP1 0x10B05DC0] (LE_NonIncidence, table entry 13)
	bool LightmapIllumination(UActor* light, int size, const vec3* locations, const vec3& base, const vec3& normal, const float* shadowmap, float* result)
	{
		uint8_t effect = light->LightEffect();
		// LE_TorchWaver, LE_FireWaver, LE_WateryShimmer are LE_None plus a flicker the original applies when it merges
		// the light (sub_10B03430); the flicker isn't ported. The other effects are left to SurrealEngine.
		bool nonIncidence = effect == LE_NonIncidence;
		if (effect > LE_WateryShimmer && !nonIncidence)
			return false;

		float radius = light->WorldLightRadius();
		float inner = LightRadiusInner(light) * radius * (1.0f / 256.0f);
		float scale = radius / (radius - inner); // the inner radius brightens the light; the clamp at 1 keeps it full inside
		vec3 lightLocation = light->Location();
		uint8_t source = nonIncidence ? LD_Ambient : LightSource(light);

		// The incidence factor: distance of the light from the surface plane / radius (point; times the per lumel
		// falloff / t that's the cosine), the light's X axis against the normal (parallel), or 1 (all directions).
		float incidence = 1.0f;
		if (source == LD_Plane)
		{
			UPawn* pawn = UObject::TryCast<UPawn>(light);
			vec3 direction = Coords::Rotation(pawn ? pawn->ViewRotation() : light->Rotation()).XAxis;
			incidence = std::max(-dot(direction, normal), 0.0f);
		}
		else if (source != LD_Ambient)
		{
			incidence = std::abs(dot(lightLocation - base, normal)) / radius;
		}
		float k = incidence * scale;

		// The table index: LE_None steps (d / radius * 4095)² in 20.12 fixed point, NonIncidence d² * 4093 / radius².
		float indexScale = nonIncidence ? 4093.0f / (radius * radius) : (4095.0f * 4095.0f / 4096.0f) / (radius * radius);
		bool divideByT = source == LD_Point;
		for (int i = 0; i < size; i++)
		{
			vec3 d = locations[i] - lightLocation;
			int m = (int)(dot(d, d) * indexScale);
			if (m >= 4096 || shadowmap[i] <= 0.0f)
			{
				result[i] = 0.0f;
				continue;
			}
			result[i] = shadowmap[i] * std::min(k * Falloff(m, divideByT), 1.0f);
		}
		return true;
	}

	// IDA Render.dll: not exported: sub_10B077F0 [HP1 0x10B077F0] (the light map builder: the zone ambient fill)
	vec3 LightmapAmbient(UZoneInfo* zone)
	{
		// FGetHSV / 4 in 8-bit units, against 127 = full.
		vec3 hsv = GetHSV(zone->AmbientHue(), zone->AmbientSaturation(), zone->AmbientBrightness());
		auto channel = [](float c) { return std::clamp(std::floor(c * 64.0f), 0.0f, 255.0f) * LightmapUnit * MeasuredScale; };
		return vec3(channel(hsv.x), channel(hsv.y), channel(hsv.z));
	}

	// IDA Render.dll: not exported: sub_10B06920 [HP1 0x10B06920] (the light's palette: colour * i, capped at 127)
	// IDA Render.dll: not exported: sub_10B03430 [HP1 0x10B03430] (merges a light into the light map: 7-bit saturating
	// add per channel; bDarkLight subtracts, stopping at 0)
	void AddLightmapLight(UActor* light, const float* illumination, vec3* lightcolors, int size)
	{
		float brightness = light->LightBrightness() * (1.0f / 255.0f);
		vec3 color = GlobalLighting(light, brightness);
		color = color * (brightness * light->Level()->Brightness());
		bool dark = bDarkLight(light);
		// illumination 1 = lumel byte 255.
		vec3 scale = color * (255.0f * LightmapUnit * MeasuredScale);
		for (int i = 0; i < size; i++)
		{
			float s = illumination[i];
			if (s <= 0.0f)
				continue;
			vec3 c(std::min(s * scale.x, LightmapFull), std::min(s * scale.y, LightmapFull), std::min(s * scale.z, LightmapFull));
			vec3& dest = lightcolors[i];
			if (dark)
				dest = vec3(std::max(dest.x - c.x, 0.0f), std::max(dest.y - c.y, 0.0f), std::max(dest.z - c.z, 0.0f));
			else
				dest = vec3(std::min(dest.x + c.x, LightmapFull), std::min(dest.y + c.y, LightmapFull), std::min(dest.z + c.z, LightmapFull));
		}
	}
}
