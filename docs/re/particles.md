# ParticleFX (HP1 Engine.dll)

KnowWonder's particle system: `AParticleFX` (Engine.ParticleFX, `DrawType = DT_Particles` = 8) owns a
`UParticleList` (a `UPrimitive`; `ParticleList` property) of `UParticle`s. Source file per asserts:
`C:\hp\Engine\Src\UnParticleFX.cpp`. Rendering is in Render.dll (`UnParticleRn.cpp`, see "Rendering" below; ported in
`kw/KWParticleRender.cpp`). Wind is `kw/KWWind.cpp`.

Addresses are the `..._0` bodies in `../ida/Engine.dll.i64`. `EmitParticles` does not decompile
(Hex-Rays: "inconsistent fpu stack"); it was read from the disassembly.

## Bits

- `0x29C`: bit0 bSteadyState, bit1 bPrime
- `0x2F8`: bit0 bUpdate, bit1 bVelocityRelative, bit2 bSystemRelative
- `0x340`: bit0 bWindPerParticle
- `0x3C8`: bit0 bShellOnly, bit1 bEmit

## UParticleList (concrete class: vtable 0x1047886C, ctor 0x103C7120)

Doubly linked list in insertion order (148-byte nodes = 8 link bytes + 140-byte UParticle), with an
iterator (`Current`, `bAdvanced`) so the current node can be removed while iterating.

`Engine.u` imports `Class Engine.ParticleList` (it's a native class with no script body). SurrealEngine registers
no such class, so the import resolves to null; since the missing-import log was added, every start logs
`Package Engine imports Class Engine.ParticleList, which Engine does not contain`. `kw/KWParticleFX.cpp`
keeps its own particle list per actor in a side table (`ParticleList` stays None, and no HP1 script reads it), so the
missing class has no effect.
(The other import logged on every start, `HPBase` → `Texture HPEdit.Icons.Icons.station`, is an editor icon.)

| vtbl | method |
|---|---|
| +112 | First() |
| +116 | Next() (if the previous call removed the current node, returns the node that took its place) |
| +120 | Add(const UParticle&) — append a copy |
| +124 | Remove(UParticle*) |
| +128 | RemoveCurrent() |
| +132 | Empty() |
| +136 | Num() |
| +140 | RemoveFirst() — oldest particle |
| +144 | Find(Id) — first particle with Id |
| +148 | UpdateBounds(): box of Position ± max(Width, Length)/2 over all particles |

## UParticle (140 bytes)

| off | field |
|---|---|
| +0 | OldPosition (position before the last Update step; used for the collision line check) |
| +12 | Position |
| +24 | Velocity |
| +36 | Age |
| +40 | Lifetime (0 = immortal) |
| +44 | Alpha, +48 AlphaStart (grow target), +52 AlphaDelta (per second) |
| +56 | Color (FPlane, 0..1, W=0), +72 ColorDelta (FPlane per second) |
| +88 | Width, +92 WidthStart, +96 WidthDelta |
| +100 | Length, +104 LengthStart, +108 LengthDelta |
| +112 | unused |
| +116 | DripTimer (>0: particle hangs and grows to full size; when it runs out, the size deltas are zeroed and motion starts) |
| +120 | ChaosTimer |
| +124 | Spin (angle; Shards start at frand*2pi) |
| +128 | SpinRate |
| +132 | Id (-1 = none; script AddParticle ids) |
| +136 | PriorityTag 0..9 (LodParticleDensity draws tags <= LOD level; emission cycles [9,3,1,7,5,0,6,2,4,8], script AddParticle uses 0) |

## FParams (GetParams 0x103C52F0): one random draw per Rand field

`+4 SourceWidth, +8 SourceHeight, +12 SourceDepth, +24 AngularSpreadWidth, +28 AngularSpreadHeight,
+32 Speed, +36 Lifetime, +40 ColorStart, +44 ColorEnd, +48 AlphaStart, +52 AlphaEnd, +56 SizeWidth,
+60 SizeLength, +64 SizeEndScale, +68 SpinRate, +72 DripTime`.
Float: `Base + frand()*Rand` (no draw when Rand == 0). SizeWidth/SizeLength share one draw. Colour: one
draw f, each channel `min(Base + clamp(int(Rand*f), 0, 255), 255)`.

## GetSysParams (0x103C42C0)

ParentBlend <= 0: this; >= 1: the parent class's defaults; otherwise a copy with every FloatParams
(except ParticlesPerSec/Period/Decay) lerped `parent*pb + own*(1-pb)` and colours per channel
`clamp(int(parent*pb)) + clamp(int(own*(1-pb)))` (saturating). Parent = `Class->SuperField` defaults.

## Tick (0x103C3C80) / Update (0x103C3DE0) / UpdateParticles (0x103C1D60)

See kw/KWParticleFX.cpp, which follows these line by line.

## EmitParticles (0x103C2170)

1. `EmitDelay += dt`; `d = LastEmitLocation - Location`.
2. rate = ParticlesPerSec draw (lerped with parent's draw by ParentBlend).
3. count: Uniform distribution: distance / rate (with a Pattern: `(n-1) * DrawScale * |P[i]-P[i+1]| *
   Period.Rand / rate`, i = segment at `(Period.Base + Period.Rand/2) * (n-1)`); else
   `clamp(LOD*3, 0.1, 1) * rate * EmitDelay`. `count += EmissionResidue`.
4. ParticlesMax cap (scales EmitDelay), backdate window `min(EmitDelay, MaxLifetime)`.
5. `n = int(count)`, residue kept; n <= 0 returns.
6. ParticlesAlive: drop the oldest so Num + n <= ParticlesAlive.
7. Per particle i: params (from GetSysParams), t = Uniform ? (i+1)/count : frand(); age =
   window - t*EmitDelay; skip if age >= Lifetime. Position: OwnerMesh = random point on a random owner
   mesh triangle (barycentric r1, r2), direction = triangle normal; Pattern = point on the pattern at
   `(t*Period.Rand + Period.Base)*(n-1)` mapped to local (0, (x-.5)*DrawScale, (.5-y)*DrawScale); else
   Location. Direction (non-mesh): local (cos w cos h, sin w cos h, sin h), w/h = frand(+-spread deg).
   Source box local (frand(+-Depth/2), frand(+-Width/2), frand(+-Height/2)). All local vectors are
   rotated by Rotation. Position += (1-t)*d (spread along the path since the last emission).
   Velocity = dir*Speed (+ Owner.Velocity if bVelocityRelative). Sizes, colour/alpha deltas, spin,
   then the particle is advanced by its age (steps of 1/15 s when Attraction/Elasticity are used).
8. `EmitDelay = 0`, `LastEmitLocation = Location`, `ParticlesEmitted += n`.

## UParticle::Update: Elasticity collisions

A step only collides when the level's BSP blocks it (`UModel::FastLineCheck(Old, New)` fails). The step is then traced
with `SingleLineCheck(TRACE_Movers|TRACE_Level, zero extent)`: a hit on LevelInfo reflects Velocity about the hit normal,
scales it by Elasticity and puts the particle at the hit location; any other result (a mover in front of the wall, or
no hit) zeroes Velocity and puts the particle back at OldPosition. Movers alone (no BSP behind them) are passed through.
Only `WaterDrip` (Lev2_fire1, Lev2_Fire2) and `avifors_react` have an Elasticity.

## LOD

`AParticleFX::Lod`, `LodParticles` and `LodParticleDensity` (0x103C63A0, 0x103C6430, 0x103C64F0) are exported but
never called, neither in Engine.dll nor by Render.dll (whose only ParticleFX imports are `Update` and the class).
`LOD` stays at the 1.0 InitExecution sets, so emission isn't thinned. The only use of PriorityTag at draw time is the
billboard overdraw budget below.

## Rendering (Render.dll, UnParticleRn.cpp)

`URender::DrawParticleSystem` (0x10B15830) calls `AParticleFX::Update(0)` and then runs passes. Each pass is a functor
with a vtable of four slots: light (fill the vertex colours), fill (build the screen-space vertices, return a clip
outcode; 124 = skip the particle), primitives per particle, vertices per primitive. The driver `sub_10B15090` sets the
texture (`Textures[0]`, animated), the poly flags (Style: Masked 0x400002, Translucent 0x400004, Modulated 0x400040,
plus the texture's flags), lights the system, walks the particle list (First/Next, passing each particle and the next
one), clips against the frustum and batches triangle fans to the render device (vtbl +128).

| RenderPrimitive | passes, in order |
|---|---|
| PPRIM_Line | line, shard, liquid, billboard |
| PPRIM_Billboard | billboard |
| PPRIM_Liquid | liquid, billboard |
| PPRIM_Shard | shard, liquid, billboard |
| PPRIM_TriTube | tube (vtable 0x10B388D0, fill 0x10B194C0), then `appFailAssert` (UnParticleRn.cpp line 1152) |

With bShellOnly the shell pass replaces the billboard pass. The shipped content only uses Billboard and Liquid (water:
`WaterDrip`, `WaterShowerFX*`, `Serpent_*`, `WaterBknSpray*`, and placed systems such as the Lev2_HogFront fountain);
no class or map uses Line, Shard, TriTube or bShellOnly (only HPConsole's commented-out mouse particle test).

| pass | vtable | fill | what it draws |
|---|---|---|---|
| line | 0x10B38934 | 0x10B1B7D0 (light 0x10B1B330) | a ribbon through the particles in list order: each joint is ±Width along normalize(toCamera × dirToNext), flipped to agree with the previous joint; UVs (0,0) (0,V) (U,V) (U,0). The first particle only starts the ribbon |
| shard | 0x10B38920 | 0x10B17350 | a world triangle (-L/2,-W/2), (L/2,W/2), (-L/2,W/2) turned by Rotator(Spin*65535, Spin*65535, 0); 3 vertices |
| liquid | 0x10B3890C | 0x10B17CE0 | DripTimer > 0: a diamond hanging under the particle: (0,0), (W/2, 0.75L), (0, L), (-W/2, 0.75L) in screen pixels at the particle's depth. Otherwise a streak (`sub_10B15B50`) |
| billboard | 0x10B388F8 | 0x10B184D0 | a camera-facing W×L quad, turned by Spin if SpinRate != 0; rejected closer than 1 unit |
| shell | 0x10B388E4 | 0x10B16C70 | Position is screen space (X, Y pixels, Z depth); a pixel-snapped W×L quad not scaled by distance, vertices BL, TR, TL, BR (as a fan this misses the right wedge) |

Streak (`sub_10B15B50`): tail = normalize(Velocity) * sqrt(|Velocity|) * Length; the radius keeps the volume of a
sphere of diameter Width: r = sqrt(W³·π/3·3 / ((|tail| + W)·π)). In screen space at the head's depth, with u the unit
direction from the projected head to the projected tail, the kite is head + (-u.y, u.x)·r, head - u·r, head +
(u.y, -u.x)·r and head + u·L', where L' = r if the tail's view-plane length l <= r, else l/|tail_view|·(l - r) + r.

Light slot (`sub_10B16AB0`): colour = LightColor * particle Color; with a = ScaleGlow * Alpha < 1 the colour is scaled
by a and, for STY_Modulated, (1-a)/2 is added. `LightColor` (script property) is (1,1,1,1) for bUnlit systems;
otherwise the light manager lights the actor (`GLightManager` vtbl +8 SetupForActor with `URender::LeafLights`, +28,
+20). No shipped system has bUnlit=False.

Overdraw budget (billboard fill): budget = FX·FY·URender[+0x5C]·0.5 (+0x5C is set to 1.0 in the URender constructor and
is not a config property). Per particle, area = W·L·RZ² (pixels²) and weighted = (PriorityTag+1)·area/4; when weighted >
budget the particle's Alpha is lowered for good to min(Alpha, (2·budget - weighted)/budget) and the particle is skipped
once it reaches 0. So a particle fades when it covers more than 2/(PriorityTag+1) screens (tag 9: 20% of the screen).

## Wind (Engine.dll UnWind.cpp)

`AWind` keeps every Wind in a global array (constructor 0x10430B60 adds, Destroy 0x10430C40 removes).
`GetTotalWind(Level, Loc)` (0x10432180) sums `GetWind` over all of them. Only the Quidditch maps place a Wind, and no
script calls `Wind.GetWind` (native 425). ParticleFX uses the wind only in the damped integration (Damping > 0): the
whole system's wind at Location * WindModifier, or per particle with bWindPerParticle.

- `GetWind(Loc)` (0x10431060): zero if WindSpeed == 0 or the distance > WindRadius² (the world radius), or when not
  bPermeating and the BSP blocks the line from the wind. Direction: WindSource LD_Point (0) = away from the wind
  (directional if exactly at it), LD_Directional (1) = the rotation's X axis, other = none. v = dir + Fluc; with
  WindFluctuation, v += noise(Loc) * WindFluctuation * |v| / 255. Result = v * WindSpeed * min((1 - dist/WindRadius²) /
  (1 - WindRadiusInner/256), 1).
- noise (`sub_104315D0`): value noise on a 32-unit lattice; each lattice point gets a vector from a shuffled 0..255 table
  (`perm[perm[perm[z]+y]+x]`, components /255 - 0.5), blended trilinearly.
- `Tick` (0x10430DB0), only with WindFluctuation: t = dt / (max(WindFlucPeriod,1)/64), k = exp(-t);
  FlucVel = FlucVel·k + randomUnit·(1-k)·WindFluctuation/255; Fluc = Fluc·k + FlucVel·dt.
