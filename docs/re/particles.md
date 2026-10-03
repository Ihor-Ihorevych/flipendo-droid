# ParticleFX (HP1 Engine.dll)

KnowWonder's particle system: `AParticleFX` (Engine.ParticleFX, `DrawType = DT_Particles` = 8) owns a
`UParticleList` (a `UPrimitive`; `ParticleList` property) of `UParticle`s. Source file per asserts:
`C:\hp\Engine\Src\UnParticleFX.cpp`. Rendering is in Render.dll (not yet reversed).

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

See hp1/HP1ParticleFX.cpp, which follows these line by line.

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
