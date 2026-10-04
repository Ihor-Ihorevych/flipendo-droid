#include "Precomp.h"
#include "KW.h"
#include "KWParticleFX.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "VM/NativeFunc.h"
#include "Utils/Random.h"
#include "Engine.h"
#include <cmath>

// Wind (native class in HP1 Engine.dll, UnWind.cpp): a light-like source of air movement. ParticleFX systems with a
// WindModifier drift in the sum of all winds (AWind::GetTotalWind). Only the Quidditch maps place Wind actors.
// See docs/re/particles.md ("Wind").

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	namespace
	{
		struct WindProps
		{
			PropertyDataOffset WindSpeed, WindRadius, WindRadiusInner, WindFluctuation, WindFlucPeriod, WindSource;
			PropertyDataOffset bPermeating, Fluc, FlucVel;
		};

		UClass* WindClass = nullptr;

		const WindProps& Props()
		{
			static WindProps props;
			static bool initialized = false;
			if (!initialized)
			{
				WindClass = engine->packages->FindClass("Engine.Wind");
				if (!WindClass)
					Exception::Throw("HP1: Engine.Wind class not found");
				auto get = [](const char* name) { return WindClass->GetPropertyDataOffset(name); };
				props.WindSpeed = get("WindSpeed");
				props.WindRadius = get("WindRadius");
				props.WindRadiusInner = get("WindRadiusInner");
				props.WindFluctuation = get("WindFluctuation");
				props.WindFlucPeriod = get("WindFlucPeriod");
				props.WindSource = get("WindSource");
				props.bPermeating = get("bPermeating");
				props.Fluc = get("Fluc");
				props.FlucVel = get("FlucVel");
				initialized = true;
			}
			return props;
		}

		float& WindSpeed(UActor* w) { return w->Value<float>(Props().WindSpeed); }
		uint8_t& WindRadius(UActor* w) { return w->Value<uint8_t>(Props().WindRadius); }
		uint8_t& WindRadiusInner(UActor* w) { return w->Value<uint8_t>(Props().WindRadiusInner); }
		uint8_t& WindFluctuation(UActor* w) { return w->Value<uint8_t>(Props().WindFluctuation); }
		uint8_t& WindFlucPeriod(UActor* w) { return w->Value<uint8_t>(Props().WindFlucPeriod); }
		uint8_t& WindSource(UActor* w) { return w->Value<uint8_t>(Props().WindSource); }
		BitfieldBool bPermeating(UActor* w) { return w->BoolValue(Props().bPermeating); }
		vec3& Fluc(UActor* w) { return w->Value<vec3>(Props().Fluc); }
		vec3& FlucVel(UActor* w) { return w->Value<vec3>(Props().FlucVel); }

		enum ELightSource : uint8_t { LD_Point, LD_Directional };

		bool IsWind(UObject* obj)
		{
			Props();
			for (UStruct* s = obj->Class; s; s = s->BaseStruct)
				if (s == WindClass)
					return true;
			return false;
		}

		// 3D value noise: a random vector per lattice point (from a shuffled 0..255 table), trilinearly blended.
		// The original indexes its table with `% 256` of possibly negative ints; here the index wraps.
		// IDA Engine.dll: not exported: sub_104315D0 [HP1 0x104315D0] (called from AWind::GetWind; table at 0x105F7C18, scale 1/32)
		vec3 WindNoise(const vec3& location)
		{
			static uint8_t perm[256];
			static bool initialized = false;
			if (!initialized)
			{
				for (int i = 0; i < 256; i++)
					perm[i] = (uint8_t)i;
				for (int i = 0; i < 256; i++)
					std::swap(perm[i], perm[RandInt(0, 255)]);
				initialized = true;
			}
			auto wrap = [](int v) { return ((v % 256) + 256) % 256; };
			auto lattice = [&](int x, int y, int z) {
				int h = perm[wrap(perm[wrap(perm[wrap(z)] + y)] + x)];
				int a = perm[h];
				int b = perm[a];
				return vec3(h * (1.0f / 255.0f) - 0.5f, a * (1.0f / 255.0f) - 0.5f, b * (1.0f / 255.0f) - 0.5f);
			};

			vec3 p = location * (1.0f / 32.0f);
			int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
			vec3 f(p.x - x, p.y - y, p.z - z);
			vec3 g = vec3(1.0f) - f;
			return lattice(x + 1, y + 1, z + 1) * (f.x * f.y * f.z) +
				lattice(x + 1, y + 1, z) * (f.x * f.y * g.z) +
				lattice(x + 1, y, z + 1) * (f.x * g.y * f.z) +
				lattice(x + 1, y, z) * (f.x * g.y * g.z) +
				lattice(x, y + 1, z + 1) * (g.x * f.y * f.z) +
				lattice(x, y + 1, z) * (g.x * f.y * g.z) +
				lattice(x, y, z + 1) * (g.x * g.y * f.z) +
				lattice(x, y, z) * (g.x * g.y * g.z);
		}

		// IDA Engine.dll: ?GetWind@AWind@@QAE?AVFVector@@ABV2@@Z [HP1 0x10431060]
		// IDA Engine.dll: ?Radius@AWind@@QBEMXZ [HP1 0x1031EE80] / ?InnerRadiusFrac@AWind@@QBEMXZ [HP1 0x1031EEB0]
		vec3 GetWind(UActor* w, const vec3& location)
		{
			vec3 d = location - w->Location();
			float distSquared = dot(d, d);
			float radius = (float)(WindRadius(w) * WindRadius(w)); // world radius = WindRadius²
			if (WindSpeed(w) == 0.0f || distSquared > radius * radius)
				return vec3(0.0f);

			// Blocked by the level's BSP unless bPermeating (the original's FastLineCheck ignores actors).
			if (!bPermeating(w))
			{
				for (const CollisionHit& hit : w->XLevel()->Collision.Trace(w->Location(), location, 0.0f, 0.0f, false, true, false))
				{
					if (!hit.Actor)
						return vec3(0.0f);
				}
			}

			float dist = std::sqrt(distSquared);
			vec3 dir(0.0f);
			if (WindSource(w) == LD_Point && dist > 0.0f)
			{
				dir = d * (1.0f / dist);
			}
			else if (WindSource(w) == LD_Point || WindSource(w) == LD_Directional)
			{
				vec3 x, y, z;
				Coords::Rotation(w->Rotation()).GetAxes(x, y, z);
				dir = x;
			}

			vec3 v = dir + Fluc(w);
			if (WindFluctuation(w) != 0)
				v += WindNoise(location) * (WindFluctuation(w) * length(v) * (1.0f / 255.0f));

			float falloff = std::min((1.0f - dist / radius) / (1.0f - WindRadiusInner(w) * (1.0f / 256.0f)), 1.0f);
			return v * (falloff * WindSpeed(w));
		}

		// The original keeps every Wind in a global array (added in the constructor, removed in Destroy). Here the
		// level's actors are scanned once per level time step.
		const Array<UActor*>& WindsOf(UActor* context)
		{
			static Array<UActor*> winds;
			static ULevel* cachedLevel = nullptr;
			static float cachedTime = -1.0f;
			ULevel* level = context->XLevel();
			float time = context->Level()->TimeSeconds();
			if (cachedLevel != level || cachedTime != time)
			{
				winds.clear();
				for (UActor* a : level->Actors)
					if (a && !a->bDeleteMe() && IsWind(a))
						winds.push_back(a);
				cachedLevel = level;
				cachedTime = time;
			}
			return winds;
		}
	}

	// IDA Engine.dll: ?GetTotalWind@AWind@@SA?AVFVector@@PAVULevel@@ABV2@@Z [HP1 0x10432180]
	vec3 GetTotalWind(UActor* context, const vec3& location)
	{
		vec3 total(0.0f);
		for (UActor* w : WindsOf(context))
			if (!w->bDeleteMe())
				total += GetWind(w, location);
		return total;
	}

	// Fluc drifts by a random walk: FlucVel relaxes towards random unit vectors scaled by WindFluctuation/255 over
	// FlucPeriod (WindFlucPeriod/64 s), and Fluc decays towards zero while integrating FlucVel.
	// IDA Engine.dll: ?Tick@AWind@@UAEHMW4ELevelTick@@@Z [HP1 0x10430DB0]
	// IDA Engine.dll: ?FlucPeriod@AWind@@QBEMXZ [HP1 0x1031EF10]
	void TickWind(UActor* w, float elapsed)
	{
		if (!IsWind(w) || WindFluctuation(w) == 0)
			return;
		float period = (WindFlucPeriod(w) ? WindFlucPeriod(w) : 1) * (1.0f / 64.0f);
		float t = elapsed / period;
		float keep = std::exp(-t);

		vec3 r;
		do
		{
			r = vec3(FRand() * 2.0f - 1.0f, FRand() * 2.0f - 1.0f, FRand() * 2.0f - 1.0f);
		} while (dot(r, r) > 1.0f);
		float len = length(r);
		if (len > 0.0f)
			r *= 1.0f / len;

		FlucVel(w) = FlucVel(w) * keep + r * ((1.0f - keep) * WindFluctuation(w) * (1.0f / 255.0f));
		Fluc(w) = Fluc(w) * keep + FlucVel(w) * elapsed;
	}

	// IDA Engine.dll: ?execGetWind@AWind@@QAEXAAUFFrame@@QAX@Z [HP1 0x10432240]
	static void NGetWind(UObject* self, const vec3& loc, vec3& ReturnValue)
	{
		UActor* w = UObject::Cast<UActor>(self);
		ReturnValue = w && IsWind(w) ? GetWind(w, loc) : vec3(0.0f);
	}

	void RegisterWindNatives()
	{
		OverrideNative(425, [] { RegisterVMNativeFunc_2("Wind", "GetWind", &NGetWind, 425); });
	}
}
