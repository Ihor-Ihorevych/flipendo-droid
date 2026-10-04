# Mesh lighting

How HP1 lights meshes (characters and props), from Render.dll. Ported in `kw/KWMeshLight.cpp`, used by
`KW::DrawSkeletalMesh` (`kw/Anim/KWSkeletal.cpp`). Lightmapped BSP surfaces are SurrealEngine's and not covered here; note that SurrealEngine's surface lighting ignores HP1's
`Actor.LightSource` (`LD_Point`, `LD_Plane` = parallel light, `LD_Ambient` = all directions) and `LightRadiusInner`.
Outdoor maps light with them (Lev2_Quid1: two `LD_Plane` lights at brightness 80, radius 255), which is why the
Quidditch pitch renders too dark.

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
