#pragma once

#include "Packages/Engine/Actors/UActor.h"
#include <list>

class UTexture;

// HP1's native ParticleFX class (hp1/HP1ParticleFX.cpp simulates, hp1/HP1ParticleRender.cpp draws).
namespace HP1
{
	// UParticle (140 bytes in HP1 Engine.dll; see docs/re/particles.md).
	struct Particle
	{
		vec3 OldPosition = vec3(0.0f);
		vec3 Position = vec3(0.0f);
		vec3 Velocity = vec3(0.0f);
		float Age = 0.0f;
		float Lifetime = 0.0f;
		float Alpha = 0.0f, AlphaStart = 0.0f, AlphaDelta = 0.0f;
		vec4 Color = vec4(0.0f), ColorDelta = vec4(0.0f);
		float Width = 0.0f, WidthStart = 0.0f, WidthDelta = 0.0f;
		float Length = 0.0f, LengthStart = 0.0f, LengthDelta = 0.0f;
		float DripTimer = 0.0f;
		float ChaosTimer = 0.0f;
		float Spin = 0.0f, SpinRate = 0.0f;
		int Id = -1;
		int PriorityTag = 0;
	};

	// UParticleList: particles in emission order (the oldest is dropped first).
	struct ParticleSystemState
	{
		std::list<Particle> Particles;
		bool PendingDestroy = false;
		double AgeRemainder = 0.0; // Age rounding left over from earlier ticks

		Particle* Find(int id)
		{
			for (Particle& p : Particles)
				if (p.Id == id)
					return &p;
			return nullptr;
		}
	};

	struct FloatParams { float Base, Rand; };
	struct ColorParams { Color Base, Rand; };

	struct ParticleFXProps
	{
		PropertyDataOffset ParticlesPerSec, SourceWidth, SourceHeight, SourceDepth, Period;
		PropertyDataOffset AngularSpreadWidth, AngularSpreadHeight, bSteadyState, bPrime;
		PropertyDataOffset Speed, Lifetime, ColorStart, ColorEnd, AlphaStart, AlphaEnd;
		PropertyDataOffset SizeWidth, SizeLength, SizeEndScale, SpinRate, DripTime;
		PropertyDataOffset bUpdate, bVelocityRelative, bSystemRelative, ParentBlend, LOD;
		PropertyDataOffset AlphaDelay, ColorDelay, SizeDelay, AlphaGrowPeriod, SizeGrowPeriod;
		PropertyDataOffset Chaos, ChaosDelay, Elasticity, Attraction, Damping, Distribution, Pattern;
		PropertyDataOffset WindModifier, bWindPerParticle, GravityModifier, Gravity, ParticlesAlive, ParticlesMax;
		PropertyDataOffset Textures, ColorPalette, RenderPrimitive, EmitDelay, LastUpdateLocation, LastEmitLocation;
		PropertyDataOffset LastUpdateRotation, EmissionResidue, Age, ElapsedTime, ParticlesEmitted, LightColor, CurrentPriorityTag;
		PropertyDataOffset bShellOnly, bEmit;
	};
	const ParticleFXProps& GetParticleFXProps();

	// ParticleFX properties, by name (also usable on a class default object).
	namespace PFX
	{
		inline const ParticleFXProps& P() { return GetParticleFXProps(); }
#define HP1_PFX_VALUE(type, name) inline type& name(UObject* o) { return o->Value<type>(P().name); }
#define HP1_PFX_BOOL(name) inline BitfieldBool name(UObject* o) { return o->BoolValue(P().name); }
		HP1_PFX_VALUE(FloatParams, ParticlesPerSec)
		HP1_PFX_VALUE(FloatParams, SourceWidth)
		HP1_PFX_VALUE(FloatParams, SourceHeight)
		HP1_PFX_VALUE(FloatParams, SourceDepth)
		HP1_PFX_VALUE(FloatParams, Period)
		HP1_PFX_VALUE(FloatParams, AngularSpreadWidth)
		HP1_PFX_VALUE(FloatParams, AngularSpreadHeight)
		HP1_PFX_BOOL(bSteadyState)
		HP1_PFX_BOOL(bPrime)
		HP1_PFX_VALUE(FloatParams, Speed)
		HP1_PFX_VALUE(FloatParams, Lifetime)
		HP1_PFX_VALUE(ColorParams, ColorStart)
		HP1_PFX_VALUE(ColorParams, ColorEnd)
		HP1_PFX_VALUE(FloatParams, AlphaStart)
		HP1_PFX_VALUE(FloatParams, AlphaEnd)
		HP1_PFX_VALUE(FloatParams, SizeWidth)
		HP1_PFX_VALUE(FloatParams, SizeLength)
		HP1_PFX_VALUE(FloatParams, SizeEndScale)
		HP1_PFX_VALUE(FloatParams, SpinRate)
		HP1_PFX_VALUE(FloatParams, DripTime)
		HP1_PFX_BOOL(bUpdate)
		HP1_PFX_BOOL(bVelocityRelative)
		HP1_PFX_BOOL(bSystemRelative)
		HP1_PFX_VALUE(float, ParentBlend)
		HP1_PFX_VALUE(float, LOD)
		HP1_PFX_VALUE(float, AlphaDelay)
		HP1_PFX_VALUE(float, ColorDelay)
		HP1_PFX_VALUE(float, SizeDelay)
		HP1_PFX_VALUE(float, AlphaGrowPeriod)
		HP1_PFX_VALUE(float, SizeGrowPeriod)
		HP1_PFX_VALUE(float, Chaos)
		HP1_PFX_VALUE(float, ChaosDelay)
		HP1_PFX_VALUE(float, Elasticity)
		HP1_PFX_VALUE(vec3, Attraction)
		HP1_PFX_VALUE(float, Damping)
		HP1_PFX_VALUE(uint8_t, Distribution)
		HP1_PFX_VALUE(UObject*, Pattern)
		HP1_PFX_VALUE(float, WindModifier)
		HP1_PFX_BOOL(bWindPerParticle)
		HP1_PFX_VALUE(float, GravityModifier)
		HP1_PFX_VALUE(vec3, Gravity)
		HP1_PFX_VALUE(int, ParticlesAlive)
		HP1_PFX_VALUE(int, ParticlesMax)
		HP1_PFX_VALUE(UTexture*, ColorPalette)
		HP1_PFX_VALUE(uint8_t, RenderPrimitive)
		HP1_PFX_VALUE(float, EmitDelay)
		HP1_PFX_VALUE(vec3, LastUpdateLocation)
		HP1_PFX_VALUE(vec3, LastEmitLocation)
		HP1_PFX_VALUE(Rotator, LastUpdateRotation)
		HP1_PFX_VALUE(float, EmissionResidue)
		HP1_PFX_VALUE(float, Age)
		HP1_PFX_VALUE(float, ElapsedTime)
		HP1_PFX_VALUE(int, ParticlesEmitted)
		HP1_PFX_VALUE(vec4, LightColor)
		HP1_PFX_VALUE(int, CurrentPriorityTag)
		HP1_PFX_BOOL(bShellOnly)
		HP1_PFX_BOOL(bEmit)
#undef HP1_PFX_VALUE
#undef HP1_PFX_BOOL
		inline UTexture* Texture0(UObject* o) { return o->Value<UTexture*>(P().Textures); }
	}

	// The particle system of a ParticleFX actor (created on first use), or nullptr for other actors.
	ParticleSystemState* GetParticleSystem(UActor* actor);
	// AParticleFX::Update: simulate up to the system's Age (dt 0) and emit. False if the system is done.
	bool UpdateParticleSystem(UActor* actor, float dt);

	// AParticleFX::Tick (hp1/HP1ParticleFX.cpp) and AWind::Tick (hp1/HP1Wind.cpp).
	void TickParticleFX(UActor* actor, float elapsed);
	void TickWind(UActor* actor, float elapsed);
	// AWind::GetTotalWind: the sum of every Wind in the context actor's level at a location (hp1/HP1Wind.cpp).
	vec3 GetTotalWind(UActor* context, const vec3& location);

	// Gesture.Points (hp1/HP1Gesture.cpp); ParticleFX.Pattern emits along it.
	TypedScriptArray<vec3> GesturePoints(UObject* gesture);
}
