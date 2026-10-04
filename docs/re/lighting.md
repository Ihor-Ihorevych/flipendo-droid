# Lighting

How HP1 lights meshes (characters and props) and BSP light maps, from Render.dll. Mesh lighting is ported in
`kw/KWMeshLight.cpp`, used by `KW::DrawSkeletalMesh` (`kw/Anim/KWSkeletal.cpp`); light maps in `kw/KWLightmap.cpp`,
hooked into SurrealEngine's `LightmapBuilder` (see "Light maps" at the end). HP1 adds `Actor.LightSource` (`LD_Point`,
`LD_Plane` = parallel light, `LD_Ambient` = all directions) and `LightRadiusInner` to UE1's light properties; outdoor
maps light with them (Lev2_Quid1: two `LD_Plane` suns, brightness 80, radius 255, LightRadiusInner 255).

# Mesh lighting

## Drawing (URender::DrawLodMesh, Render.dll 0x10B0FF00)

- Vertex normals: the sum of the face normals of every face using the vertex, normalized as `n / sqrt(|n|² + 0.001)`
  (normals that cancel stay near zero; there is no fallback direction). `bMeshCurvy`: the normal points from the
  actor's location to the vertex instead.
- Faces are back-face culled unless their material is `PF_TwoSided`. The test runs in view space: with face vertices
  P0, P1, P2 it computes `n = (P0-P1) x (P2-P0)` and draws the face when `(n . P0) * Mirror < 0` (the scene node's
  Mirror, -1 in mirror reflections, which also swaps the drawing order). View axes (X right, Y down, Z forward) are a
  reflection of world axes (X forward, Y right, Z up), which turns the cross product around: in world space the
  drawn faces are those whose `(P1-P0) x (P2-P0)` points at the viewer, and the vertex normals are sums of that same
  vector. Getting this backwards draws the inside of every closed mesh (faces seen through the backs of heads) and
  lights characters from the wrong side. Thin props are built as two coplanar quads, one
  per side (the classroom `TransBlackboard`: vertices 0-3 front, 4-7 back). Without the cull they z-fight triangle by
  triangle, which looks like a diagonal split between two differently lit halves.
- `PF_Unlit` faces get a flat grey: `clamp(AmbientGlow/256 + ScaleGlow/2, 0, 1)`. Zone 0 is not unlit; an actor
  outside the BSP (`Region.iLeaf == -1`) gets ambient only.
- D3DDrv draws the vertex light as a plain modulation of the texture (light × 255 as the vertex colour), so the
  values below are what ends up on screen. SurrealEngine's own mesh light multiplied by 3 before clamping.

## Picking lights (the light manager's SetupForActor, sub_10B098F0)

GLightManager's vtable is `off_10B386D4` (+8 SetupForActor, +24 Light per vertex).

- Nothing for `bUnlit` actors or actors outside the BSP.
- Candidates: the lights picked for this actor last time (a per-actor cache of up to 16 lights with a fade byte), the
  lights permeating its BSP leaf, the dynamic lights touching it. A candidate needs `LightType != LT_None`,
  `LightBrightness != 0`, the same `bSpecialLit` as the actor, and the actor's location inside its radius.
  Coronas are not excluded.
- Importance `(1 - dist/radius) * LightBrightness * 1024`, sorted strongest first. Picked: at most 3 static lights;
  dynamic lights only while fewer than 3 lights are picked in total; nothing under 1/8 of the first picked light.
- A light must see the actor's location (BSP line check light → actor). The check runs again only every 16 frames per
  light (staggered by the light's object index); in between the last result is kept. Dynamic `bMovable` lights skip it.
- Fade: 768 per second. Picked lights fade in, dropped ones fade out and keep lighting until they reach 0. An actor
  seen for the first time gets its visible lights at full strength at once.

## A light's colour (sub_10B06920, URender::GlobalLighting 0x10B06810)

- Colour `FGetHSV(LightHue, LightSaturation, 255)` (Engine.dll 0x10420DD0; not SurrealEngine's lightmap `hsbtorgb`),
  times `LightBrightness/255` changed by the LightType (table at 0x10B38288: Pulse `0.6 + 0.39 sin`, SubtlePulse
  `0.9 + 0.09 sin`, phase `Level.TimeSeconds * 2293760 / LightPeriod + LightPhase * 256` with 65536 per turn; Blink,
  Flicker from a per-frame random table, Strobe toggling every frame, TexturePalette* from the Skin's palette),
  clamped to 0..1, times `LevelInfo.Brightness` and the fade. `bDarkLight` negates it.
- Falloff is linear: `min((radius - dist) / (radius - radius * LightRadiusInner / 256), 1)`. `LightRadiusInner` is an
  HP1-only property. It isn't clamped at 0, so vertices past the radius lose light.

## A vertex's light (the light manager's Light, sub_10B02B10)

- Ambient: `2 * ScaleGlow * FGetHSV(zone AmbientHue, AmbientSaturation, AmbientBrightness) + AmbientGlow/255`
  (AmbientGlow 255 pulses `0.25 + 0.2 sin(8t)`).
- Per light: `cos` = `L·N / |L|` (LD_Point), `N·direction` (LD_Plane), 1 (LD_Ambient). Diffuse `max(2 * ScaleGlow * cos, 0)`;
  with ScaleGlow 0.7 the cosine is bent to `(cos + 1)² - 1.5`.
- Specular, on by default (SpecularGlow 1, SpecularWidth 170): `cutoff = (1 - SpecularWidth/170)²`,
  `strength = 2 * SpecularGlow / (1 - cutoff)²` (6 with ScaleGlow 0.7). With R = L reflected about the normal and V
  the eye → vertex vector, `strength * ((R·V)² / (|L|²|V|²) - cutoff)` is added when R·V > 0 and the term is positive.
- Added as `colour * falloff * (diffuse + specular)` when that sum is positive. Each channel is then clamped to 1 with
  an unsigned compare, so a negative channel also becomes 1.
- The original lights in camera space but takes LD_Plane's direction in world space; we light in world space.

# Light maps

The light manager's light map builder is `sub_10B077F0` (Render.dll). It fills the map with the zone ambient, sets up
each light with `sub_10B06920` (the same light info as for meshes, 49 dwords), runs the light's LightEffect function
from the table at `off_10B38110` (3 dwords per effect: function + two flags) on the light's shadow bytes, and merges
the result into the map with `sub_10B03430`. SurrealEngine keeps the light picking, shadow maps and caching; HP1's
terms replace its falloff, colour scale and ambient.

- **Light map values** are 7 bits per channel, 127 = full. The ambient fill is `floor(FGetHSV(zone ambient) * 64)`
  (`0.25 * 256`), alpha 127. Each light adds `min(colour * L, 127)` per channel through a 256-entry palette (L = the
  lumel byte, 0..255; colour = `GlobalLighting` colour × brightness after LightType × `LevelInfo.Brightness`), with a
  7-bit saturating add (bDarkLight lights subtract, stopping at 0). So one light can reach full on its own at L ≥ 127.
- **On screen**: D3DDrv's RGBA7 upload doubles every byte (`sub_100022C0`, `*dst = 2 * src`) and the light map stage
  is `D3DTOP_MODULATE2X` (`SetBlending`; the multipass fallback is DESTCOLOR × SRCCOLOR, also 2x). 127 is therefore
  about 2x the texture, the same range as SurrealEngine's shader (light map × 2): HP1 byte b = SurrealEngine `2b/255`.
  SurrealEngine's own builder adds `illum * colour` (half of HP1's) and fills ambient with the full HSV colour.
- **The lumel byte**, LE_None (`loc_10B037C0`; with a LightRadiusInner it runs `sub_10B0CA70`, the same plus a clamp):
  `L = shadow * min(k * table[m], 1)`, 0 outside the radius, where
  - `m = floor((d / R * 4095)² / 4096)` (d = lumel to light, R = `WorldLightRadius`), `table[m]` at
    `t = sqrt((m + 1) / 4096)` is `2t³ - 3t² + 1` (smoothstep falloff), divided by t for `LD_Point`
    (tables `flt_10B5B684` / `flt_10B43320`, built in `sub_10B023A0`);
  - `k = incidence * R / (R - inner)`, inner = `LightRadiusInner * R / 256`. LightRadiusInner doesn't change the
    falloff's shape, it scales it up and the clamp keeps it full near the light (255 makes the light saturate almost to
    its radius);
  - incidence: `LD_Point` `|light - surface plane| / R` (times table/t this is the cosine: `cos * f(t)`; SurrealEngine
    computed `cos * f(t) / t`), `LD_Plane` `max(-X · N, 0)` with X the light's (a pawn's: view) rotation axis,
    `LD_Ambient` 1. Plane and ambient lights still fall off with distance from the light actor.
- **The radius**: HP1's `AActor::WorldLightRadius` (Engine.dll 0x1031BA10) is `max(DrawScale, 1) * (LightRadius + 1) * 25`
  (SurrealEngine leaves out DrawScale). It's R everywhere: light maps, the mesh light picking and falloff. Lev2_Quid1's
  suns have DrawScale 5 (R = 32000); without it they barely reached the pitch. `WorldVolumetricRadius` has no DrawScale.
- **LE_NonIncidence** (`sub_10B05DC0`): `shadow * min(R / (R - inner) * smoothstep[floor(d² * 4093 / R²)], 1)`, no
  incidence and no LightSource.
- **LE_TorchWaver/FireWaver/WateryShimmer** run LE_None; `sub_10B03430` then scales the lumels by `0.95 + 0.05 r`,
  `0.8 + 0.2 r`, `0.6 + 0.4 r` (r from a random table, every frame) as it merges them. The flicker isn't ported.
- Not ported: the other effects (Searchlight, SlowWave, FastWave, StaticSpot, Spotlight, Interference, Cylinder: ~60
  lights in all of HP1's maps) still use SurrealEngine's functions, only the colour and clamps are HP1's. Of the
  ~11,900 lights in HP1's maps, LE_None has ~10,700 and NonIncidence ~1,060; LD_Ambient ~310, LD_Plane ~20.
