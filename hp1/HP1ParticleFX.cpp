#include "Precomp.h"
#include "HP1.h"
#include "HP1ParticleFX.h"
#include "Anim/HP1Animation.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Collision/TopLevel/CollisionSystem.h"
#include "VM/NativeFunc.h"
#include "Utils/Random.h"
#include "Engine.h"
#include <cmath>
#include <unordered_map>

// ParticleFX (native class in HP1 Engine.dll, UnParticleFX.cpp): simulation, emission and the script natives.
// See docs/re/particles.md for the reversed layouts. Rendering is in hp1/HP1ParticleRender.cpp.
//
// The original keeps the particles in a UParticleList object referenced by the ParticleList property. Here
// they live in a side table keyed by the actor (ParticleList stays None; no HP1 script reads it).
// Like the original, Tick only ages the system; the particles are simulated when the system is drawn.

namespace HP1
{
	void OverrideNative(int index, void (*registerFunc)());

	namespace
	{
		enum EParticlePrimitive : uint8_t { PPRIM_Line, PPRIM_Billboard, PPRIM_Liquid, PPRIM_Shard, PPRIM_TriTube };
		enum EDistributionType : uint8_t { DIST_Random, DIST_Uniform, DIST_OwnerMesh };

		// FParams: one random draw of the system's settings for a new particle.
		struct Params
		{
			float SourceWidth = 0.0f, SourceHeight = 0.0f, SourceDepth = 0.0f;
			float AngularSpreadWidth = 0.0f, AngularSpreadHeight = 0.0f;
			float Speed = 0.0f, Lifetime = 0.0f;
			Color ColorStart = {}, ColorEnd = {};
			float AlphaStart = 0.0f, AlphaEnd = 0.0f;
			float SizeWidth = 0.0f, SizeLength = 0.0f, SizeEndScale = 0.0f;
			float SpinRate = 0.0f, DripTime = 0.0f;
		};

		// The blendable settings (GetSysParams works on a copy of the whole actor; only these are read from it).
		struct SysParams
		{
			FloatParams SourceWidth, SourceHeight, SourceDepth, AngularSpreadWidth, AngularSpreadHeight;
			FloatParams Speed, Lifetime, AlphaStart, AlphaEnd, SizeWidth, SizeLength, SizeEndScale, SpinRate, DripTime;
			ColorParams ColorStart, ColorEnd;
		};

		std::unordered_map<UActor*, ParticleSystemState> States;

		const ParticleFXProps* Props = nullptr;
		UClass* ParticleFXClass = nullptr;

		float MaxLifetime(UActor* a)
		{
			return std::max(PFX::Lifetime(a).Rand, 0.0f) + PFX::Lifetime(a).Base;
		}

		// AActor::InitExecution override: steady state systems start primed (all particles already alive).
		// IDA Engine.dll: ?InitExecution@AParticleFX@@UAEXXZ [HP1 0x103C0F30]
		// IDA Engine.dll: ??0AParticleFX@@QAE@XZ [HP1 0x103C0E90] (LOD = 1, Age = ElapsedTime = 0; Age is then loaded from the map)
		void InitExecution(UActor* a)
		{
			PFX::LOD(a) = 1.0f;
			PFX::bSteadyState(a) = PFX::ParticlesMax(a) == 0 && PFX::Distribution(a) != DIST_Uniform;
			// The original tests Level.TimeSeconds == 0 (actors present when the level starts). This runs lazily on
			// the first tick, so "level start" is approximated as the level's first second.
			if (PFX::bSteadyState(a) && (PFX::bPrime(a) || a->Level()->TimeSeconds() < 1.0f))
			{
				float maxLife = MaxLifetime(a);
				if (maxLife > PFX::Age(a))
					PFX::Age(a) = maxLife;
			}
			PFX::ElapsedTime(a) = 0.0f;
		}

		float RandParam(const FloatParams& p)
		{
			return p.Rand == 0.0f ? p.Base : FRand() * p.Rand + p.Base;
		}

		uint8_t ClampByte(int v)
		{
			return (uint8_t)std::clamp(v, 0, 255);
		}

		Color RandColor(const ColorParams& p)
		{
			if (p.Rand.R == 0 && p.Rand.G == 0 && p.Rand.B == 0 && p.Rand.A == 0)
				return p.Base;
			float f = FRand();
			Color c;
			c.R = (uint8_t)std::min(p.Base.R + ClampByte((int)(p.Rand.R * f)), 255);
			c.G = (uint8_t)std::min(p.Base.G + ClampByte((int)(p.Rand.G * f)), 255);
			c.B = (uint8_t)std::min(p.Base.B + ClampByte((int)(p.Rand.B * f)), 255);
			c.A = (uint8_t)std::min(p.Base.A + ClampByte((int)(p.Rand.A * f)), 255);
			return c;
		}

		// IDA Engine.dll: ?GetParams@AParticleFX@@QBEXAAUFParams@@@Z [HP1 0x103C52F0]
		Params GetParams(const SysParams& s)
		{
			Params p;
			p.AngularSpreadWidth = RandParam(s.AngularSpreadWidth);
			p.AngularSpreadHeight = RandParam(s.AngularSpreadHeight);
			p.SourceWidth = RandParam(s.SourceWidth);
			p.SourceDepth = RandParam(s.SourceDepth);
			p.SourceHeight = RandParam(s.SourceHeight);
			p.Speed = RandParam(s.Speed);
			p.Lifetime = RandParam(s.Lifetime);
			p.AlphaStart = RandParam(s.AlphaStart);
			p.AlphaEnd = RandParam(s.AlphaEnd);
			p.SpinRate = RandParam(s.SpinRate);
			p.DripTime = RandParam(s.DripTime);
			p.ColorStart = RandColor(s.ColorStart);
			p.ColorEnd = RandColor(s.ColorEnd);
			float f = FRand(); // SizeWidth and SizeLength share one draw (and always draw)
			p.SizeWidth = f * s.SizeWidth.Rand + s.SizeWidth.Base;
			p.SizeLength = f * s.SizeLength.Rand + s.SizeLength.Base;
			p.SizeEndScale = RandParam(s.SizeEndScale);
			return p;
		}

		SysParams ReadSysParams(UObject* o)
		{
			SysParams s;
			s.SourceWidth = PFX::SourceWidth(o);
			s.SourceHeight = PFX::SourceHeight(o);
			s.SourceDepth = PFX::SourceDepth(o);
			s.AngularSpreadWidth = PFX::AngularSpreadWidth(o);
			s.AngularSpreadHeight = PFX::AngularSpreadHeight(o);
			s.Speed = PFX::Speed(o);
			s.Lifetime = PFX::Lifetime(o);
			s.AlphaStart = PFX::AlphaStart(o);
			s.AlphaEnd = PFX::AlphaEnd(o);
			s.SizeWidth = PFX::SizeWidth(o);
			s.SizeLength = PFX::SizeLength(o);
			s.SizeEndScale = PFX::SizeEndScale(o);
			s.SpinRate = PFX::SpinRate(o);
			s.DripTime = PFX::DripTime(o);
			s.ColorStart = PFX::ColorStart(o);
			s.ColorEnd = PFX::ColorEnd(o);
			return s;
		}

		// The original reads the parent class's defaults (Class->SuperField). Only meaningful for subclasses of
		// ParticleFX; a direct ParticleFX instance has no ParticleFX parent, so it uses its own settings.
		UObject* ParentDefaults(UActor* a)
		{
			UClass* parent = dynamic_cast<UClass*>(a->Class->BaseStruct);
			if (!parent || !ParticleFXClass)
				return nullptr;
			for (UStruct* s = parent; s; s = s->BaseStruct)
				if (s == ParticleFXClass)
					return parent->GetDefaultObject<UObject>();
			return nullptr;
		}

		FloatParams Lerp(const FloatParams& parent, const FloatParams& own, float pb)
		{
			return { parent.Base * pb + (1.0f - pb) * own.Base, parent.Rand * pb + (1.0f - pb) * own.Rand };
		}

		Color Lerp(const Color& parent, const Color& own, float pb)
		{
			auto ch = [&](uint8_t p, uint8_t o) { return (uint8_t)std::min(ClampByte((int)(p * pb)) + ClampByte((int)(o * (1.0f - pb))), 255); };
			return { ch(parent.R, own.R), ch(parent.G, own.G), ch(parent.B, own.B), ch(parent.A, own.A) };
		}

		// IDA Engine.dll: ?GetSysParams@AParticleFX@@QBEPBV1@PAD@Z [HP1 0x103C42C0]
		SysParams GetSysParams(UActor* a)
		{
			SysParams own = ReadSysParams(a);
			float pb = PFX::ParentBlend(a);
			if (pb <= 0.0f)
				return own;
			UObject* parentObj = ParentDefaults(a);
			if (!parentObj)
				return own;
			SysParams parent = ReadSysParams(parentObj);
			if (pb >= 1.0f)
				return parent;
			SysParams s = own;
			s.AngularSpreadWidth = Lerp(parent.AngularSpreadWidth, own.AngularSpreadWidth, pb);
			s.AngularSpreadHeight = Lerp(parent.AngularSpreadHeight, own.AngularSpreadHeight, pb);
			s.SourceWidth = Lerp(parent.SourceWidth, own.SourceWidth, pb);
			s.SourceDepth = Lerp(parent.SourceDepth, own.SourceDepth, pb);
			s.SourceHeight = Lerp(parent.SourceHeight, own.SourceHeight, pb);
			s.Speed = Lerp(parent.Speed, own.Speed, pb);
			s.Lifetime = Lerp(parent.Lifetime, own.Lifetime, pb);
			s.AlphaStart = Lerp(parent.AlphaStart, own.AlphaStart, pb);
			s.AlphaEnd = Lerp(parent.AlphaEnd, own.AlphaEnd, pb);
			s.SpinRate = Lerp(parent.SpinRate, own.SpinRate, pb);
			s.DripTime = Lerp(parent.DripTime, own.DripTime, pb);
			s.ColorStart = { Lerp(parent.ColorStart.Base, own.ColorStart.Base, pb), Lerp(parent.ColorStart.Rand, own.ColorStart.Rand, pb) };
			s.ColorEnd = { Lerp(parent.ColorEnd.Base, own.ColorEnd.Base, pb), Lerp(parent.ColorEnd.Rand, own.ColorEnd.Rand, pb) };
			s.SizeWidth = Lerp(parent.SizeWidth, own.SizeWidth, pb);
			s.SizeLength = Lerp(parent.SizeLength, own.SizeLength, pb);
			s.SizeEndScale = Lerp(parent.SizeEndScale, own.SizeEndScale, pb);
			return s;
		}

		// Local (X forward, Y right, Z up) to world, by the system's rotation. The original transforms by
		// GMath.UnitCoords *= Rotation (TransformVectorBy), which is the same rotation.
		vec3 LocalToWorld(const Rotator& rot, const vec3& v)
		{
			vec3 x, y, z;
			Coords::Rotation(rot).GetAxes(x, y, z);
			return x * v.x + y * v.y + z * v.z;
		}

		vec3 WorldToLocal(const Rotator& rot, const vec3& v)
		{
			vec3 x, y, z;
			Coords::Rotation(rot).GetAxes(x, y, z);
			return { dot(v, x), dot(v, y), dot(v, z) };
		}

		vec3 PaletteColor(UTexture* palette, int index, float* alpha = nullptr)
		{
			UPalette* pal = palette ? palette->Palette() : nullptr;
			if (!pal || pal->Colors.empty())
				return vec3(0.0f);
			index = std::clamp(index, 0, (int)pal->Colors.size() - 1);
			uint32_t c = pal->Colors[index];
			if (alpha)
				*alpha = ((c >> 24) & 0xff) * (1.0f / 255.0f);
			return vec3((c & 0xff) * (1.0f / 255.0f), ((c >> 8) & 0xff) * (1.0f / 255.0f), ((c >> 16) & 0xff) * (1.0f / 255.0f));
		}

		bool HasPalette(UTexture* palette)
		{
			return palette && palette->Palette() && !palette->Palette()->Colors.empty();
		}

		// IDA Engine.dll: ?Update@UParticle@@QAE_NABVFVector@@M0PAVULevel@@MPAVAParticleFX@@@Z [HP1 0x103BFC30]
		bool UpdateParticle(UActor* a, Particle& p, const vec3& gravity, float damp, const vec3& wind, float dt)
		{
			if (p.Lifetime > 0.0f && dt + p.Age >= p.Lifetime)
				return false;

			if (p.DripTimer > 0.0f)
			{
				p.DripTimer -= dt;
				if (p.DripTimer <= 0.0f)
				{
					// Dripping is over: the rest of the step is spent moving, and the particle stops growing.
					dt = -p.DripTimer;
					p.DripTimer = 0.0f;
					p.WidthDelta = 0.0f;
					p.LengthDelta = 0.0f;
				}
			}

			p.OldPosition = p.Position;
			if (p.DripTimer == 0.0f)
			{
				p.Spin += dt * p.SpinRate;
				if (damp >= 1.0f)
				{
					p.Position += (p.Velocity + gravity * (dt * 0.5f)) * dt;
					p.Velocity += gravity * dt;
				}
				else
				{
					// Damped motion towards the terminal velocity gravity/Damping (+ wind), integrated exactly.
					float damping = PFX::Damping(a);
					vec3 terminal = gravity * (1.0f / damping);
					if (PFX::bWindPerParticle(a))
						terminal += GetTotalWind(a, p.Position) * PFX::WindModifier(a);
					else
						terminal += wind;
					float k = (damp - 1.0f) / damping;
					p.Position += (terminal - p.Velocity) * k + terminal * dt;
					p.Velocity = (p.Velocity - terminal) * damp + terminal;
				}

				if (PFX::bSystemRelative(a))
				{
					// Particles move and turn with the system. (The original applies the translation on every
					// physics sub-step of a frame.)
					p.Position -= PFX::LastUpdateLocation(a) - a->Location();
					vec3 local = WorldToLocal(PFX::LastUpdateRotation(a), p.Position - a->Location());
					p.Position = a->Location() + LocalToWorld(a->Rotation(), local);
				}

				const vec3& attraction = PFX::Attraction(a);
				if (attraction.x != 0.0f || attraction.y != 0.0f || attraction.z != 0.0f)
				{
					vec3 toSystem = a->Location() - p.Position;
					p.Velocity += toSystem * attraction * dt;
				}

				if (p.ChaosTimer != 0.0f)
					p.ChaosTimer = p.ChaosTimer <= dt ? 0.0f : p.ChaosTimer - dt;

				float chaos = PFX::Chaos(a);
				if (chaos != 0.0f && p.ChaosTimer <= 0.0f)
				{
					float rz = FRand() * 2.0f - 1.0f;
					float ry = FRand() * 2.0f - 1.0f;
					float rx = FRand() * 2.0f - 1.0f;
					vec3 r(rx, ry, rz);
					float len2 = dot(r, r);
					if (len2 >= 1e-8f)
						r *= 1.0f / std::sqrt(len2);
					p.Velocity += r * chaos;
					p.ChaosTimer = PFX::ChaosDelay(a);
				}
			}

			if (PFX::bUpdate(a))
			{
				float alphaGrowEnd = PFX::AlphaGrowPeriod(a) * p.Lifetime;
				if (alphaGrowEnd > p.Age)
					p.Alpha = std::min(p.Alpha + dt * p.AlphaStart / alphaGrowEnd, p.AlphaStart);
				else if (p.Age > PFX::AlphaDelay(a))
					p.Alpha += dt * p.AlphaDelta;
				if (p.Alpha < 0.001f)
					p.Alpha = 0.0f;

				UTexture* palette = PFX::ColorPalette(a);
				if (palette)
				{
					if (HasPalette(palette))
					{
						if (p.Lifetime <= 0.0f || p.Age <= PFX::ColorDelay(a))
							p.Color = vec4(PaletteColor(palette, 0), 0.0f);
						else
							p.Color = vec4(PaletteColor(palette, (int)(p.Age / p.Lifetime * 255.0f)), 0.0f);
					}
				}
				else if (length(p.ColorDelta.xyz()) > 0.0f && p.Age > PFX::ColorDelay(a))
				{
					p.Color.x += dt * p.ColorDelta.x;
					p.Color.y += dt * p.ColorDelta.y;
					p.Color.z += dt * p.ColorDelta.z;
				}

				if (!(PFX::RenderPrimitive(a) == PPRIM_Liquid && p.DripTimer <= 0.0f))
				{
					float sizeGrowEnd = PFX::SizeGrowPeriod(a) * p.Lifetime;
					if (sizeGrowEnd > p.Age)
					{
						float r = dt / sizeGrowEnd;
						p.Width = std::min(r * p.WidthStart + p.Width, p.WidthStart);
						p.Length = std::min(r * p.LengthStart + p.Length, p.LengthStart);
					}
					else if (p.Age > PFX::SizeDelay(a))
					{
						p.Width += dt * p.WidthDelta;
						p.Length += dt * p.LengthDelta;
					}
				}
			}

			if (PFX::Elasticity(a) > 0.0f && p.OldPosition != p.Position)
			{
				// Only a step the level's BSP blocks collides (FastLineCheck). The step is then traced against the level
				// and movers (SingleLineCheck, TRACE_Movers|TRACE_Level): a level hit bounces the particle, anything else
				// (a mover in front of the wall) stops it where it was.
				bool blocked = false;
				for (const CollisionHit& h : a->XLevel()->Collision.Trace(p.OldPosition, p.Position, 0.0f, 0.0f, false, true, false))
				{
					if (!h.Actor)
					{
						blocked = true;
						break;
					}
				}
				if (blocked)
				{
					TraceFlags flags;
					flags.movers = true;
					flags.world = true;
					CollisionHit hit = a->XLevel()->Collision.TraceFirstHit(p.OldPosition, p.Position, nullptr, vec3(0.0f), flags);
					if (hit.Fraction < 1.0f && !hit.Actor)
					{
						vec3 hitLocation = p.OldPosition + (p.Position - p.OldPosition) * hit.Fraction;
						p.Velocity = (p.Velocity - hit.Normal * (2.0f * dot(hit.Normal, p.Velocity))) * PFX::Elasticity(a);
						p.Position = hitLocation;
					}
					else
					{
						p.Velocity = vec3(0.0f);
						p.Position = p.OldPosition;
					}
				}
			}

			p.Age += dt;
			return true;
		}

		// IDA Engine.dll: ?UpdateParticles@AParticleFX@@QAEXM@Z [HP1 0x103C1D60]
		void UpdateParticles(UActor* a, ParticleSystemState& state, float dt)
		{
			float maxLife = MaxLifetime(a);
			if (maxLife > 0.0f && dt > maxLife)
			{
				state.Particles.clear();
				return;
			}

			for (auto it = state.Particles.begin(); it != state.Particles.end();)
			{
				if (it->Lifetime > 0.0f && dt + it->Age >= it->Lifetime)
					it = state.Particles.erase(it);
				else
					++it;
			}
			if (state.Particles.empty())
				return;

			UZoneInfo* zone = a->Region().Zone ? a->Region().Zone : a->Level();
			vec3 gravity = zone->ZoneGravity() * PFX::GravityModifier(a) + PFX::Gravity(a);
			vec3 wind(0.0f);
			if (PFX::Damping(a) * PFX::WindModifier(a) > 0.0f)
				wind = GetTotalWind(a, a->Location()) * PFX::WindModifier(a);

			const vec3& attraction = PFX::Attraction(a);
			bool subdivide = attraction.x != 0.0f || attraction.y != 0.0f || attraction.z != 0.0f || PFX::Elasticity(a) > 0.0001f;
			while (dt > 0.0f)
			{
				float step = (dt > 0.06667f && subdivide) ? 0.06667f : dt;
				float damp = PFX::Damping(a) > 0.0f ? std::exp(-(step * PFX::Damping(a))) : 1.0f;
				for (Particle& p : state.Particles)
					UpdateParticle(a, p, gravity, damp, wind, step);
				dt -= step;
			}
		}

		// Shared tail of EmitParticles and AddParticle: sizes, colour and alpha deltas from the drawn params.
		void SetupAppearance(UActor* a, Particle& p, const Params& params, bool emitting)
		{
			p.Lifetime = params.Lifetime;
			p.Width = p.WidthStart = params.SizeWidth;
			p.Length = p.LengthStart = params.SizeLength;
			if (PFX::SizeGrowPeriod(a) > 0.0f)
			{
				p.Width = 0.0f;
				p.Length = 0.0f;
			}

			if (params.DripTime != 0.0f && emitting)
			{
				p.WidthDelta = p.Width / params.DripTime;
				p.Width = 0.0f;
				p.LengthDelta = p.Length / params.DripTime;
				p.Length = 0.0f;
			}
			else if (params.DripTime > 0.0f)
			{
				// AddParticle: grows to full size over the drip time
				p.WidthDelta = p.Width / params.DripTime;
				p.Width = 0.0f;
				p.LengthDelta = p.Length / params.DripTime;
				p.Length = 0.0f;
			}
			else
			{
				// EmitParticles divides by (Lifetime - SizeDelay), AddParticle by Lifetime
				float span = emitting ? p.Lifetime - PFX::SizeDelay(a) : p.Lifetime;
				bool scale = emitting ? p.Lifetime > PFX::SizeDelay(a) : p.Lifetime > 0.01f;
				p.WidthDelta = scale ? (params.SizeEndScale * params.SizeWidth - params.SizeWidth) / span : 0.0f;
				p.LengthDelta = scale ? (params.SizeEndScale * params.SizeLength - params.SizeLength) / span : 0.0f;
			}
			p.DripTimer = params.DripTime;

			p.Color = vec4(params.ColorStart.R * (1.0f / 255.0f), params.ColorStart.G * (1.0f / 255.0f), params.ColorStart.B * (1.0f / 255.0f), 0.0f);
			p.AlphaStart = params.AlphaStart;
			p.Alpha = PFX::AlphaGrowPeriod(a) > 0.0f ? 0.0f : params.AlphaStart;
			p.SpinRate = params.SpinRate;

			UTexture* palette = PFX::ColorPalette(a);
			if (HasPalette(palette))
			{
				// No colour or alpha deltas are set up with a palette (the original leaves them uninitialized).
				p.Color = vec4(PaletteColor(palette, 0), 0.0f);
				p.ColorDelta = vec4(0.0f);
				p.AlphaDelta = 0.0f;
			}
			else
			{
				float colorSpan = p.Lifetime - PFX::ColorDelay(a);
				if (p.Lifetime > PFX::ColorDelay(a))
				{
					float s = (1.0f / 255.0f) / colorSpan;
					p.ColorDelta = vec4((params.ColorEnd.R - params.ColorStart.R) * s, (params.ColorEnd.G - params.ColorStart.G) * s, (params.ColorEnd.B - params.ColorStart.B) * s, 0.0f);
				}
				else
				{
					p.ColorDelta = vec4(0.0f);
				}

				float alphaDelay = emitting ? PFX::AlphaDelay(a) : PFX::AlphaDelay(a) + 0.001f;
				if (p.Lifetime > alphaDelay)
					p.AlphaDelta = (params.AlphaEnd - params.AlphaStart) / (p.Lifetime - PFX::AlphaDelay(a));
				else
					p.AlphaDelta = 0.0f;
			}
		}

		vec3 SpreadDirection(UActor* a, const Params& params)
		{
			float w = FRandRange(-params.AngularSpreadWidth * 0.01745329f, params.AngularSpreadWidth * 0.01745329f);
			float h = FRandRange(-params.AngularSpreadHeight * 0.01745329f, params.AngularSpreadHeight * 0.01745329f);
			return LocalToWorld(a->Rotation(), vec3(std::cos(w) * std::cos(h), std::sin(w) * std::cos(h), std::sin(h)));
		}

		// IDA Engine.dll: ?AddParticle@AParticleFX@@QAE_NHAAVFVector@@PAUFParams@@@Z [HP1 0x103C5B00]
		void AddParticle(UActor* a, ParticleSystemState& state, int id, const vec3& location, const Params& params)
		{
			Particle p;
			p.OldPosition = location;
			p.Position = location;
			p.Id = id;
			p.PriorityTag = 0;
			SetupAppearance(a, p, params, false);
			p.Velocity = SpreadDirection(a, params) * params.Speed;
			state.Particles.push_back(p);
		}

		// DIST_OwnerMesh: world-space vertices and triangles of the owner's mesh.
		bool GetOwnerMesh(UActor* a, Array<vec3>& verts, Array<int>& tris)
		{
			UActor* owner = a->Owner();
			if (!owner || !owner->Mesh())
				return false;
			USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(owner->Mesh());
			if (!mesh || !GetSkeletalFrameVerts(owner, mesh, verts))
				return false;
			tris.clear();
			for (const MeshFace& face : mesh->Faces)
			{
				for (int k = 0; k < 3; k++)
				{
					int wedge = face.Indices[k];
					tris.push_back(wedge < (int)mesh->Wedges.size() ? mesh->Wedges[wedge].Vertex : 0);
				}
			}
			return !tris.empty();
		}

		// IDA Engine.dll: ?EmitParticles@AParticleFX@@QAEHM@Z [HP1 0x103C2170] (read from the disassembly; Hex-Rays fails on it)
		// IDA Engine.dll: ?GetNumTris@ULodMesh@@UBEHXZ [HP1 0x103E9B10] / ?GetTriVerts@ULodMesh@@UBEXHQAH@Z (owner mesh triangles via wedges)
		int EmitParticles(UActor* a, ParticleSystemState& state, float dt)
		{
			PFX::EmitDelay(a) += dt;
			vec3 moved = PFX::LastEmitLocation(a) - a->Location();

			float rate = FRand() * PFX::ParticlesPerSec(a).Rand + PFX::ParticlesPerSec(a).Base;
			if (PFX::ParentBlend(a) != 0.0f)
			{
				if (UObject* parent = ParentDefaults(a))
				{
					float parentRate = FRand() * PFX::ParticlesPerSec(parent).Rand + PFX::ParticlesPerSec(parent).Base;
					rate = (parentRate - rate) * PFX::ParentBlend(a) + rate;
				}
			}

			float count;
			if (PFX::Distribution(a) == DIST_Uniform)
			{
				// Uniform: spaced by distance (ParticlesPerSec is the spacing in units).
				UObject* pattern = PFX::Pattern(a);
				if (pattern)
				{
					TypedScriptArray<vec3> pts = GesturePoints(pattern);
					int n = (int)pts.size();
					int i = std::min((int)((PFX::Period(a).Base + PFX::Period(a).Rand * 0.5f) * (n - 1)), n - 2);
					float segLen = (n >= 2 && i >= 0) ? length(pts[i] - pts[i + 1]) : 0.0f;
					count = (n - 1) * a->DrawScale() * segLen * PFX::Period(a).Rand / rate;
				}
				else
				{
					count = length(moved) / rate;
				}
			}
			else
			{
				float lod = std::clamp(PFX::LOD(a) * 3.0f, 0.1f, 1.0f);
				count = lod * rate * PFX::EmitDelay(a);
			}
			count += PFX::EmissionResidue(a);

			if (PFX::ParticlesMax(a) > 0)
			{
				float remaining = (float)(PFX::ParticlesMax(a) - PFX::ParticlesEmitted(a));
				if (!(count < remaining))
				{
					PFX::EmitDelay(a) = remaining / count * PFX::EmitDelay(a);
					count = remaining;
				}
			}

			// Particles are backdated over at most one lifetime.
			float maxLife = MaxLifetime(a);
			float window = PFX::EmitDelay(a);
			if (window > maxLife)
			{
				count = maxLife / PFX::EmitDelay(a) * count;
				PFX::EmitDelay(a) = maxLife;
				window = maxLife;
			}

			int n = (int)count;
			PFX::EmissionResidue(a) = count - n;
			if (n <= 0)
				return 0;

			int alive = PFX::ParticlesAlive(a);
			if (alive > 0 && (int)state.Particles.size() + n > alive)
			{
				if (n > alive)
					n = alive;
				int kill = (int)state.Particles.size() - alive + n;
				for (int i = 0; i < kill && !state.Particles.empty(); i++)
					state.Particles.pop_front();
			}

			UZoneInfo* zone = a->Region().Zone ? a->Region().Zone : a->Level();
			vec3 gravity = zone->ZoneGravity() * PFX::GravityModifier(a) + PFX::Gravity(a);
			vec3 wind(0.0f);
			if (PFX::Damping(a) * PFX::WindModifier(a) > 0.0f)
				wind = GetTotalWind(a, a->Location()) * PFX::WindModifier(a);

			Array<vec3> meshVerts;
			Array<int> meshTris;
			bool ownerMesh = PFX::Distribution(a) == DIST_OwnerMesh && GetOwnerMesh(a, meshVerts, meshTris);

			SysParams sys = GetSysParams(a);
			const vec3& attraction = PFX::Attraction(a);
			bool subdivide = attraction.x != 0.0f || attraction.y != 0.0f || attraction.z != 0.0f || PFX::Elasticity(a) > 0.0001f;

			for (int i = 0; i < n; i++)
			{
				Params params = GetParams(sys);
				float t;
				if (PFX::Distribution(a) == DIST_Uniform)
					t = count > 0.0f ? (i + 1) / count : 0.0f;
				else
					t = FRand();

				float age = window - t * PFX::EmitDelay(a);
				if (!(age < params.Lifetime))
					continue;

				Particle p;
				p.Lifetime = params.Lifetime;

				vec3 dir;
				if (ownerMesh)
				{
					int tri = RandInt(0, (int)meshTris.size() / 3 - 1);
					const vec3& v0 = meshVerts[std::min((size_t)meshTris[tri * 3], meshVerts.size() - 1)];
					const vec3& v1 = meshVerts[std::min((size_t)meshTris[tri * 3 + 1], meshVerts.size() - 1)];
					const vec3& v2 = meshVerts[std::min((size_t)meshTris[tri * 3 + 2], meshVerts.size() - 1)];
					float r1 = FRand();
					float r2 = FRand();
					p.Position = v0 * (1.0f - r1) + v1 * (r1 * (1.0f - r2)) + v2 * (r1 * r2);
					vec3 normal = cross(v1 - v0, v2 - v0);
					float len = length(normal);
					dir = len > 1e-8f ? normal * (1.0f / len) : vec3(0.0f);
				}
				else
				{
					UObject* pattern = PFX::Pattern(a);
					if (pattern && GesturePoints(pattern).size() >= 2)
					{
						TypedScriptArray<vec3> pts = GesturePoints(pattern);
						int count2 = (int)pts.size();
						float f = (t * PFX::Period(a).Rand + PFX::Period(a).Base) * (count2 - 1);
						int idx = std::min((int)f, count2 - 2);
						float frac = f - idx;
						float x = pts[idx].x * (1.0f - frac) + pts[idx + 1].x * frac;
						float y = pts[idx].y * (1.0f - frac) + pts[idx + 1].y * frac;
						vec3 local(0.0f, (x - 0.5f) * a->DrawScale(), (0.5f - y) * a->DrawScale());
						p.Position = a->Location() + LocalToWorld(a->Rotation(), local);
					}
					else
					{
						p.Position = a->Location();
					}
					dir = SpreadDirection(a, params);
				}

				float h = FRandRange(-params.SourceHeight * 0.5f, params.SourceHeight * 0.5f);
				float w = FRandRange(-params.SourceWidth * 0.5f, params.SourceWidth * 0.5f);
				float d = FRandRange(-params.SourceDepth * 0.5f, params.SourceDepth * 0.5f);
				p.Position += LocalToWorld(a->Rotation(), vec3(d, w, h));
				p.Position += moved * (1.0f - t); // spread along the path since the last emission
				p.OldPosition = vec3(0.0f);

				SetupAppearance(a, p, params, true);

				p.Velocity = dir * params.Speed;
				if (PFX::bVelocityRelative(a) && a->Owner())
					p.Velocity += a->Owner()->Velocity();

				if (PFX::RenderPrimitive(a) == PPRIM_Shard)
					p.Spin = FRand() * 6.2831855f;

				// Advance the particle to where it would be had it been emitted at its backdated time.
				while (age > 0.0f)
				{
					float step = (age > 0.06667f && subdivide) ? 0.06667f : age;
					float damp = PFX::Damping(a) > 0.0f ? std::exp(-(step * PFX::Damping(a))) : 1.0f;
					UpdateParticle(a, p, gravity, damp, wind, step);
					age -= step;
				}

				static const int priorityTags[10] = { 9, 3, 1, 7, 5, 0, 6, 2, 4, 8 };
				int& tag = PFX::CurrentPriorityTag(a);
				if (++tag > 9)
					tag = 0;
				p.PriorityTag = priorityTags[tag];

				state.Particles.push_back(p);
			}

			PFX::EmitDelay(a) = 0.0f;
			PFX::LastEmitLocation(a) = a->Location();
			PFX::ParticlesEmitted(a) += n;
			return n;
		}

		// IDA Engine.dll: ?RecomputeDeltas@AParticleFX@@QAEHH@Z [HP1 0x103C6270]
		bool RecomputeDeltas(UActor* a, ParticleSystemState& state, int id)
		{
			if (PFX::ColorPalette(a))
				return false;
			Particle* p = state.Find(id);
			if (!p)
				return false;
			float left = p->Lifetime - p->Age;
			if (left <= 0.0f)
				return false;
			float inv = 1.0f / left;
			// The original subtracts the particle's 0..1 colour from ColorEnd's 0..255 value before the 1/255 scale.
			const Color& end = PFX::ColorEnd(a).Base;
			p->ColorDelta = vec4((end.R - p->Color.x) * (inv / 255.0f), (end.G - p->Color.y) * (inv / 255.0f), (end.B - p->Color.z) * (inv / 255.0f), 0.0f);
			p->AlphaDelta = (PFX::AlphaEnd(a).Base - p->Alpha) * inv;
			p->WidthDelta = (PFX::SizeEndScale(a).Base * p->Width - p->Width) * inv;
			p->LengthDelta = (PFX::SizeEndScale(a).Base * p->Length - p->Length) * inv;
			return true;
		}

		ParticleSystemState* GetState(UActor* a)
		{
			if (!a || a->bDeleteMe())
				return nullptr;
			auto it = States.find(a);
			if (it != States.end())
				return &it->second;
			ParticleSystemState& state = States[a];
			InitExecution(a);
			return &state;
		}

		bool IsParticleFX(UObject* obj)
		{
			if (!ParticleFXClass)
				return false;
			for (UStruct* s = obj->Class; s; s = s->BaseStruct)
				if (s == ParticleFXClass)
					return true;
			return false;
		}
	}

	ParticleSystemState* GetParticleSystem(UActor* actor)
	{
		return IsParticleFX(actor) ? GetState(actor) : nullptr;
	}

	// IDA Engine.dll: ?Update@AParticleFX@@QAE_NM@Z [HP1 0x103C3DE0]
	bool UpdateParticleSystem(UActor* a, float dt)
	{
		ParticleSystemState* state = GetParticleSystem(a);
		if (!state)
			return false;

		if (PFX::ElapsedTime(a) == 0.0f)
		{
			PFX::LastUpdateLocation(a) = a->Location();
			PFX::LastEmitLocation(a) = a->Location();
			PFX::LastUpdateRotation(a) = a->Rotation();
		}

		if (dt == 0.0f)
			dt = PFX::Age(a) - PFX::ElapsedTime(a);
		PFX::ElapsedTime(a) += dt;

		if (PFX::bSteadyState(a) && a->Location() == PFX::LastUpdateLocation(a) && dt > 0.06667f)
		{
			// A steady system catches up at most one lifetime, and no faster than it can refill.
			float maxLife = MaxLifetime(a);
			float target = (std::max(PFX::ParticlesPerSec(a).Rand, 0.0f) + PFX::ParticlesPerSec(a).Base) * maxLife;
			if (PFX::ParticlesAlive(a) > 0)
				target = std::min(target, (float)PFX::ParticlesAlive(a));
			float minRate = std::min(PFX::ParticlesPerSec(a).Rand, 0.0f) + PFX::ParticlesPerSec(a).Base;
			float refill = std::max((target - (float)state->Particles.size()) / minRate, 0.06667f);
			dt = std::min(dt, maxLife);
			dt = std::min(dt, refill);
		}

		if (PFX::bShellOnly(a))
			dt = 0.1f;
		else if (dt <= 0.0f)
			return true;

		UpdateParticles(a, *state, dt);

		if (PFX::ParticlesMax(a) == 0 || PFX::ParticlesEmitted(a) < PFX::ParticlesMax(a))
		{
			if (PFX::bEmit(a))
				EmitParticles(a, *state, dt);
			else
				PFX::LastEmitLocation(a) = a->Location();
		}
		else if (state->Particles.empty())
		{
			// Finished: destroyed on the next tick (the original destroys it right here, during rendering).
			state->PendingDestroy = true;
			return false;
		}

		PFX::LastUpdateLocation(a) = a->Location();
		PFX::LastUpdateRotation(a) = a->Rotation();
		return true;
	}

	void TickNativeActor(UActor* actor, float elapsed)
	{
		if (IsParticleFX(actor))
			TickParticleFX(actor, elapsed);
		else
			TickWind(actor, elapsed);
	}

	// IDA Engine.dll: ?Tick@AParticleFX@@UAEHMW4ELevelTick@@@Z [HP1 0x103C3C80]
	void TickParticleFX(UActor* a, float elapsed)
	{
		ParticleSystemState* state = GetState(a);
		if (!state)
			return;
		if (state->PendingDestroy)
		{
			a->Destroy();
			return;
		}

		const FloatParams& rate = PFX::ParticlesPerSec(a);
		if (!state->Particles.empty() || std::max(rate.Rand, 0.0f) + rate.Base > 0.0f)
		{
			// Age is a float saved in the maps, often in the 100000s (systems ran in the editor). There a float
			// moves in 1/64 s steps, so at high frame rates `Age += elapsed` would round away every frame and
			// the system would freeze. Keep the rounding error and apply it on later ticks.
			float oldAge = PFX::Age(a);
			double target = (double)oldAge + elapsed + state->AgeRemainder;
			PFX::Age(a) = (float)target;
			state->AgeRemainder = target - (double)PFX::Age(a);
			int maxParticles = PFX::ParticlesMax(a);
			if (maxParticles > 0 && rate.Base > 0.0f && PFX::Age(a) > (float)maxParticles / rate.Base + MaxLifetime(a))
				a->Destroy();
		}
	}

	// IDA Engine.dll: ?Destroy@AParticleFX@@UAEXXZ [HP1 0x103C1CA0]
	void ParticleFXDestroyed(UActor* a)
	{
		States.erase(a);
	}

	// Box around the particles (from the last update) and the emitter's source volume, padded by half the
	// biggest particle size. The original also handles owner mesh/pattern emitters and a system that moved
	// far since the last update; this is the plain emitter box.
	// IDA Engine.dll: ?GetRenderBoundingBox@AParticleFX@@UAE?AVFCoords@@H@Z [HP1 0x103C1060] (simplified)
	BBox GetParticleBoundingBox(UActor* a)
	{
		float size = std::max(std::max(PFX::SizeWidth(a).Rand, 0.0f) + PFX::SizeWidth(a).Base, std::max(PFX::SizeLength(a).Rand, 0.0f) + PFX::SizeLength(a).Base) * 0.5f;
		float source = std::max({ std::max(PFX::SourceWidth(a).Rand, 0.0f) + PFX::SourceWidth(a).Base, std::max(PFX::SourceHeight(a).Rand, 0.0f) + PFX::SourceHeight(a).Base, std::max(PFX::SourceDepth(a).Rand, 0.0f) + PFX::SourceDepth(a).Base }) * 0.5f;
		if (PFX::Pattern(a))
			source = std::max(source, a->DrawScale() * 0.5f);
		vec3 extent(source + size + 1.0f);
		BBox box(a->Location() - extent, a->Location() + extent);

		auto it = States.find(a);
		if (it != States.end())
		{
			for (const Particle& p : it->second.Particles)
			{
				float half = std::max(p.Width, p.Length) * 0.5f;
				box.min = vec3(std::min(box.min.x, p.Position.x - half), std::min(box.min.y, p.Position.y - half), std::min(box.min.z, p.Position.z - half));
				box.max = vec3(std::max(box.max.x, p.Position.x + half), std::max(box.max.y, p.Position.y + half), std::max(box.max.z, p.Position.z + half));
			}
		}
		return box;
	}

	// IDA Engine.dll: ?execNumParticles@AParticleFX@@QAEXAAUFFrame@@QAX@Z [HP1 0x103C6720]
	static void NNumParticles(UObject* Self, int& ReturnValue)
	{
		ParticleSystemState* state = GetParticleSystem(UObject::Cast<UActor>(Self));
		ReturnValue = state ? (int)state->Particles.size() : -1;
	}

	// IDA Engine.dll: ?execAddParticle@AParticleFX@@QAEXAAUFFrame@@QAX@Z [HP1 0x103C6840]
	static void NAddParticle(UObject* Self, int Id, const vec3& Loc, BitfieldBool& ReturnValue)
	{
		UActor* a = UObject::Cast<UActor>(Self);
		ParticleSystemState* state = GetParticleSystem(a);
		if (!state)
		{
			ReturnValue = false;
			return;
		}
		PFX::bSteadyState(a) = false;
		AddParticle(a, *state, Id, Loc, GetParams(GetSysParams(a)));
		ReturnValue = true;
	}

	// ParticleParams (script struct): Position, Velocity, Lifetime, Alpha, Color, Width, Length, DripTimer, SpinRate.
	// The parameter isn't declared `out`; the original writes through GPropAddr into the caller's variable. Here
	// the native gets the struct value; no HP1 script calls Get/SetParticleParams.
	struct ScriptParticleParams
	{
		vec3 Position;
		vec3 Velocity;
		float Lifetime;
		float Alpha;
		Color Color;
		float Width;
		float Length;
		float DripTimer;
		float SpinRate;
	};

	// IDA Engine.dll: ?execGetParticleParams@AParticleFX@@QAEXAAUFFrame@@QAX@Z [HP1 0x103C69B0]
	static void NGetParticleParams(UObject* self, ExpressionValue* args)
	{
		UActor* a = UObject::Cast<UActor>(self);
		ParticleSystemState* state = GetParticleSystem(a);
		Particle* p = state ? state->Find(args[0].ToType<int32_t>()) : nullptr;
		bool found = p && args[1].GetType() == ExpressionValueType::ValueStruct;
		if (found)
		{
			ScriptParticleParams& out = args[1].ToType<ScriptParticleParams&>();
			out.Position = p->Position;
			out.Velocity = p->Velocity;
			out.Lifetime = p->Lifetime;
			out.Alpha = p->Alpha;
			auto toByte = [](float v) { return ClampByte((int)(v * 256.0f - 0.5f)); };
			out.Color = { toByte(p->Color.x), toByte(p->Color.y), toByte(p->Color.z), toByte(p->Color.w) };
			out.Width = p->Width;
			out.Length = p->Length;
			out.DripTimer = p->DripTimer;
			out.SpinRate = p->SpinRate;
		}
		args[2].ToType<BitfieldBool&>() = found;
	}

	// IDA Engine.dll: ?execSetParticleParams@AParticleFX@@QAEXAAUFFrame@@QAX@Z [HP1 0x103C6CB0]
	static void NSetParticleParams(UObject* self, ExpressionValue* args)
	{
		UActor* a = UObject::Cast<UActor>(self);
		ParticleSystemState* state = GetParticleSystem(a);
		if (a)
			PFX::bSteadyState(a) = false;
		Particle* p = state ? state->Find(args[0].ToType<int32_t>()) : nullptr;
		bool found = p && args[1].GetType() == ExpressionValueType::ValueStruct;
		if (found)
		{
			const ScriptParticleParams& in = args[1].ToType<ScriptParticleParams&>();
			p->Position = in.Position;
			p->Velocity = in.Velocity;
			p->Lifetime = in.Lifetime;
			p->Alpha = in.Alpha;
			p->Color = vec4(in.Color.R * (1.0f / 255.0f), in.Color.G * (1.0f / 255.0f), in.Color.B * (1.0f / 255.0f), 0.0f);
			p->Width = in.Width;
			p->Length = in.Length;
			p->DripTimer = in.DripTimer;
			p->SpinRate = in.SpinRate;
		}
		args[2].ToType<BitfieldBool&>() = found;
	}

	// IDA Engine.dll: ?execRecomputeDeltas@AParticleFX@@QAEXAAUFFrame@@QAX@Z [HP1 0x103C6F30]
	static void NRecomputeDeltas(UObject* Self, int Id, BitfieldBool& ReturnValue)
	{
		UActor* a = UObject::Cast<UActor>(Self);
		ParticleSystemState* state = GetParticleSystem(a);
		ReturnValue = state && RecomputeDeltas(a, *state, Id);
	}

	void RegisterParticleNatives()
	{
		OverrideNative(431, [] { RegisterVMNativeFunc_1("ParticleFX", "NumParticles", &NNumParticles, 431); });
		OverrideNative(432, [] { RegisterVMNativeFunc_3("ParticleFX", "AddParticle", &NAddParticle, 432); });
		OverrideNative(433, [] { NativeFunctions::RegisterHandler("ParticleFX", "GetParticleParams", 433, &NGetParticleParams); });
		OverrideNative(434, [] { NativeFunctions::RegisterHandler("ParticleFX", "SetParticleParams", 434, &NSetParticleParams); });
		OverrideNative(435, [] { RegisterVMNativeFunc_2("ParticleFX", "RecomputeDeltas", &NRecomputeDeltas, 435); });
	}

	const ParticleFXProps& GetParticleFXProps()
	{
		static ParticleFXProps props;
		if (!Props)
		{
			UClass* cls = engine->packages->FindClass("Engine.ParticleFX");
			if (!cls)
				Exception::Throw("HP1: Engine.ParticleFX class not found");
			ParticleFXClass = cls;
			auto get = [&](const char* name) { return cls->GetPropertyDataOffset(name); };
			props.ParticlesPerSec = get("ParticlesPerSec");
			props.SourceWidth = get("SourceWidth");
			props.SourceHeight = get("SourceHeight");
			props.SourceDepth = get("SourceDepth");
			props.Period = get("Period");
			props.AngularSpreadWidth = get("AngularSpreadWidth");
			props.AngularSpreadHeight = get("AngularSpreadHeight");
			props.bSteadyState = get("bSteadyState");
			props.bPrime = get("bPrime");
			props.Speed = get("Speed");
			props.Lifetime = get("Lifetime");
			props.ColorStart = get("ColorStart");
			props.ColorEnd = get("ColorEnd");
			props.AlphaStart = get("AlphaStart");
			props.AlphaEnd = get("AlphaEnd");
			props.SizeWidth = get("SizeWidth");
			props.SizeLength = get("SizeLength");
			props.SizeEndScale = get("SizeEndScale");
			props.SpinRate = get("SpinRate");
			props.DripTime = get("DripTime");
			props.bUpdate = get("bUpdate");
			props.bVelocityRelative = get("bVelocityRelative");
			props.bSystemRelative = get("bSystemRelative");
			props.ParentBlend = get("ParentBlend");
			props.LOD = get("LOD");
			props.AlphaDelay = get("AlphaDelay");
			props.ColorDelay = get("ColorDelay");
			props.SizeDelay = get("SizeDelay");
			props.AlphaGrowPeriod = get("AlphaGrowPeriod");
			props.SizeGrowPeriod = get("SizeGrowPeriod");
			props.Chaos = get("Chaos");
			props.ChaosDelay = get("ChaosDelay");
			props.Elasticity = get("Elasticity");
			props.Attraction = get("Attraction");
			props.Damping = get("Damping");
			props.Distribution = get("Distribution");
			props.Pattern = get("Pattern");
			props.WindModifier = get("WindModifier");
			props.bWindPerParticle = get("bWindPerParticle");
			props.GravityModifier = get("GravityModifier");
			props.Gravity = get("Gravity");
			props.ParticlesAlive = get("ParticlesAlive");
			props.ParticlesMax = get("ParticlesMax");
			props.Textures = get("Textures");
			props.ColorPalette = get("ColorPalette");
			props.RenderPrimitive = get("RenderPrimitive");
			props.EmitDelay = get("EmitDelay");
			props.LastUpdateLocation = get("LastUpdateLocation");
			props.LastEmitLocation = get("LastEmitLocation");
			props.LastUpdateRotation = get("LastUpdateRotation");
			props.EmissionResidue = get("EmissionResidue");
			props.Age = get("Age");
			props.ElapsedTime = get("ElapsedTime");
			props.ParticlesEmitted = get("ParticlesEmitted");
			props.LightColor = get("LightColor");
			props.CurrentPriorityTag = get("CurrentPriorityTag");
			props.bShellOnly = get("bShellOnly");
			props.bEmit = get("bEmit");
			Props = &props;
		}
		return props;
	}
}
