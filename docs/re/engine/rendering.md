# Actor rendering and the loading screen

Things Render.dll and `UGameEngine::LoadMap` do around the actors and the level change that SurrealEngine had no
counterpart for. Reversed in HP1's Render.dll and Engine.dll; the HP2 part below says what is known about HP2.
Particles are in [particles.md](particles.md), mesh lighting in [lighting.md](lighting.md).

## Actor shadows (`URender::SetupDynamics` [HP1 Render 0x10B2FB30])

HP1's blob shadows are `ActorShadow` decals (Engine.u; `HarryShadow`, `BroomShadow`, every `baseChar` with a
`ShadowClass`) kept in `Actor.Shadow`. Nothing in the scripts places them: the renderer does.

- `SetupDynamics` walks the whole actor list before drawing anything. For each actor it would draw (not the view
  target unless in a portal or bBehindView, not bHidden, inside VisibilityRadius / VisibilityHeight, the bOnlyOwnerSee
  / bOwnerNoSee rules, the player's IsActorVisible, not a moving brush) that has a `Shadow`, and unless the view is
  one of the editor's ortho modes (RendMap 13-15), it takes a box `CollisionRadius` wide from the actor's location
  down 8192 units. If that box is on screen (BoundVisible), it raises `Shadow.Update(None)`.
- `ActorShadow.Update` puts the decal under the owner's root bone, sizes it from `GetRenderExtent` and
  `ShadowSizeFactor`, and calls `AttachDecal(MaxShadowDist, ...)` again when the owner moved more than `MoveThreshold`.
  Its `Tick` detaches the decal on any frame without an Update, so an actor off screen loses its shadow, and without
  the renderer's call no shadow is ever drawn.
- `Update` is raised with `FindFunctionChecked(ENGINE_Update)` and ProcessEvent, inline in SetupDynamics, which is why
  the event list generated from Engine.dll ([script_events.md](script_events.md)) missed it.

Flipendo: `KW::UpdateActorShadows` (`src/knowwonder/KWActorRender.cpp`), called by `VisibleFrame::Process` before
the BSP walk (Update relinks the decal into the node lists the walk iterates). SurrealEngine's decals draw them
(modulated). The decal ignores the shadow's `Opacity` (set from the owner's); not checked what HP1 does with it.

## Actor.Opacity (`URender::DrawLodMesh` [HP1 Render 0x10B0FF00])

`Actor.Opacity` (Display, default 1): with Opacity < 1, DrawLodMesh adds `PF_Highlighted | PF_Translucent` to every
face, sets each vertex's alpha (`Light.W`) to Opacity (the software renderer scales the light by it instead), and
sorts the faces back to front. The sprite list sorts such an actor with the translucents. KnowWonder's D3DDrv blends
`PF_Highlighted` + `PF_Translucent` as SRCALPHA / INVSRCALPHA, and `PF_Translucent` clears `PF_Masked`, so no alpha test.

HP1 uses it for `InvisibleHarry` (the cloak fades Harry and his wand to `InvisibleValue`), Nearly Headless Nick, the
Bloody Baron and Peeves (0.3 / 0.6 while he hides).

Flipendo: `KW::MeshOpacity` / `ApplyOpacityFlags` / `ApplyOpacityVertices`, used by our skeletal mesh draw and by
SurrealEngine's vertex and LOD mesh paths. SurrealEngine's devices blend `PF_Highlighted` as premultiplied alpha
(ONE / INVSRCALPHA) and pick `PF_Translucent` first, so a face gets `PF_Highlighted` only, with light and fog scaled
by Opacity and vertex alpha Opacity (`GouraudVertex::Alpha`, passed through by the three devices): the same result.
Not ported: the back-to-front face sort inside one mesh, so a translucent character shows its inner faces.

Deliberately unlike HP1: Flipendo keeps `PF_Masked` on a faded mesh. HP1 drops it, so the masked parts draw as solid
texels: Harry's glasses (`skharryMesh` material 2, `skharryTex2`, PF_Masked on the material only) show their lens
texture whenever he is under the cloak (Lev4_Sneak's intro, `Opacity` 0.5). The devices' alpha test (`ALPHATEST` in
the Vulkan, D3D11 and OpenGL scene shaders) tests the texel's own alpha instead of the alpha after the vertex colour,
which would cut a mesh faded below 0.5 entirely.

## The loading screen (`UGameEngine::LoadMap` [HP1 0x1039C3D0])

1. **Fade out.** With a level loaded, LoadMap first clears `LevelInfo.Pauser`, the player's `bShowMenu` and
   `LevelAction`. If `Console.FadeoutTime` > 0 (HPConsole: 0.5) and `Console.bDrewWorld`, it draws frames in a loop
   while it lowers the player's `FlashFog.W` (the brightness, see `KW::ViewFlashParams`) by elapsed seconds /
   FadeoutTime, then sets W = 0 and draws once more. `bDrewWorld` is set by the native `UConsole::PostRender`
   (`!bNoDrawWorld`), which SurrealEngine doesn't run (it raises the script event directly); Flipendo reads
   `bNoDrawWorld`.
2. **Level info.** After loading the new package's `LevelInfo0` (and copying the URL's map into an empty
   `LevelEnterText`, [savegames.md](savegames.md)), it locks the viewport (cleared to black), raises
   `Console.DrawLevelInfo(Canvas, LevelEnterText)` and shows the frame while the rest loads. HPConsole draws the
   parchment with the level's title and objective ([hp1/menus.md](../hp1/menus.md#level-titles)).
   The event's name is lost in the decompiled code (`FName(\`string\`)`), so the generated event list misses it too.

The startup map is loaded by `UGameEngine::Init` before it opens the first viewport, so it shows neither. A save is
loaded through LoadMap like a map, so it gets both.

Flipendo: `KW::LoadMapFadeOut` and `KW::LoadMapLevelInfo` (`src/knowwonder/KWView.cpp`) from `Engine::LoadMap` and
`Engine::LoadFromSaveFile`; `RenderSubsystem::DrawLevelInfo` draws the canvas-only frame.

## Coronas (`URender::DrawFrame` [HP1 Render 0x10B254F0], the block at its end)

Drawn after the scene, for the main view only (FSceneNode +28 == 0), and only when the viewport's actor is in a zone
and a BSP leaf and the render device's `Coronas` option is on. The lights come from the viewport actor's leaf
(`Region.iLeaf`, Actor +164; in a cutscene that's Harry's, not the camera's): the map's `Leaves[leaf].iPermeating`
list and `URender::LeafLights[leaf]` (dynamic lights). `sub_10B26F10` takes a light with `bCorona` (Actor +496 bit 4;
the byte holds bSpecialLit 1, bActorShadows 2, bCorona 4, bLensFlare 8, bDarkLight 0x10), a Skin (+352), and a clear
level line (ULevel vtable +196, TRACE_Movers | TRACE_Level) from the camera to it. A table of 32 (a FMemCache item,
id 39) keeps actor, actor index and fade: every frame each fade drops by 3 × the real seconds passed (appSeconds,
not level time) and is freed below 0; a gathered light adds twice that (a new one starts there), capped at 1. Every
live entry in front of the camera is drawn with Canvas `DrawIcon` (vtable +100): its Skin centred on the projected
light, viewport width × DrawScale (+364) × 0.8 wide, colour = the light's hue and saturation (FGetHSV's colour
without the brightness curve, LightBrightness unused) × fade. SurrealEngine drew every visible bCorona actor at
full colour, so Lev_Tut2's fountain arches and Lev_Tut1's stair lamps had glows the original doesn't show (their
lights aren't in the leaf where Harry stands). `src/knowwonder/KWActorRender.cpp` (`KW::DrawCoronas`); the LeafLights
part isn't ported (SurrealEngine keeps no per-leaf dynamic light list).

## Mirrors on movers (`URender::OccludeBsp` [HP1 Render 0x10B22480])

Checked in HP1's Render.dll. A surface reflects when its PolyFlags have PF_Mirrored (0x08000000), the portal depth
is below 3 and the viewport's show flag 0x800 is set. UE1 adds mover polygons to the level BSP every frame, so a mirror can be a mover face. Lev_Tut3's mirror in the
Alohomora room is one: the `Mirror1` mover (Mover20, a box textured `arch1runner_B`, one face mirrored), which swings
open on Alohomora; the translucent `MirrorBlur` pane (Translucent, NotSolid, TwoSided, SpecialLit) is a separate BSP
surface. SurrealEngine draws movers as actors and only made portals of mirrored BSP surfaces, so the face drew as its
dark texture. Flipendo (render patch, any game): `VisibleBrush::AddMirrorPortals` makes a mirror portal of every
mirrored mover face that faces the view, drawn from the camera reflected in the face's plane and clipped to it (the
room behind the mover isn't reflected); the face then only fills the depth buffer. The reflection also leaves out
actors wholly behind the mirror's plane: the clipper only culls BSP surfaces by it and the GPU doesn't clip, so the
mirror's own mover (its back face nearest the reflected camera) covered the whole reflection with its stone texture.
That cull keeps whatever crosses the plane, drawn whole: with Harry close to the mirror, the walls around it covered
most of the reflection. The reflection's frame now gives the render device the mirror's plane as its clip plane
(`NearClip`, which the D3D11 and Vulkan shaders already clip every vertex by), so only what is in front of the mirror
is drawn. The OpenGL device has that clip commented out.

Mirrors in the level BSP get the same clip plane (the surface's plane). SurrealEngine had none for them, which works
for a mirror with solid behind it but not for a free-standing one: Lev4_Sneak2's Mirror of Erised (glass `MirrorMain2`,
Mirrored + Translucent + Unlit, so its frost is drawn over the reflection) showed its own wooden back panel as the
reflection. James and Lily are `Mover2` (tag `jameslillymover`, raised 192 by the mirror cutscene): one Translucent,
NotSolid, Unlit polygon (`JamesLilli`) standing 130 units in front of the glass and facing it, so the room never sees it
and the reflection does. Two more things hid it there: a mover polygon's one-sided test used the frame's
ViewLocation, which a BSP mirror's reflection then kept at the real camera, so it now takes the
eye from the frame's view transform (`VisibleFrame::EyeLocation`, also used by the skeletal meshes' one-sided test, which had kept Harry's far side in the reflection); and mover polygons weren't reversed in mirror frames as BSP surfaces are
(`VisibleNode`), so the device culled them by their flipped winding.

## Warp zones and mirrors in portals (`URender::OccludeBsp` [HP1 Render 0x10B22480])

Checked in HP1's Render.dll. A portal into a `WarpZoneInfo`'s zone is drawn as a warp only below portal depth 3 and
when the zone has its `OtherSideActor` and `OtherSideLevel`; when either is missing it calls the zone's `Generate`
event and checks again. Without them the portal is drawn as a plain zone portal (the zone behind it, no warp). Both
are `transient`, so a loaded save has none until that `Generate` (`WarpZoneInfo.PreBeginPlay` only runs for a new
level). A mirror (PF_Mirrored) reflects below portal depth 3 too, inside a warp or another reflection.

Lev5_Final's Mirror of Erised scene is built from both. The cutscene camera films a stand-in set at X 4864: the mirror
there is `WarpZoneInfo2`'s portal (`TheRealHarryPotter`, other side `TheRealMirrorWarpZone` = `WarpZoneInfo0` at
6272, 2939), behind which stands a real mirror surface (`MirrorMain2`, Mirrored + Translucent + SpecialLit, 3 units
from that portal), whose reflection looks back into `WarpZoneInfo0`'s portal, whose other side is `WarpZoneInfo1`
(`MirroredRoomForStoner`, no `OtherSideURL` of its own) in the room where the reflected Harry (the cutscene's
`Cast3`) acts. `WarpZoneInfo0`'s `Destinations` switch it between that room and `TheRealHarryPotter` when triggered.

SurrealEngine warped through every warp zone portal and read `OtherSideActor` without checking it: after a loaded
save that crashed (reading 0x48) as soon as the mirror cutscene (`CutScene16`, touched in front of the boss room's
mirror) showed the set. It also never reflected inside a portal (`PortalDepth > 0`, to skip back faces), so the mirror
was black. Flipendo (render patch, any game): a missing other side is generated, a warp zone still without one is a
plain portal, and a mirror reflects below depth 3, inside a portal only when it faces the frame's eye
(`VisibleFrame::EyeLocation`). Inside the reflection the next portal was still lost in three ways, fixed the same way:
a BSP mirror's frame walked the BSP from the real camera (kept as `ViewLocation`), here inside `WarpZoneInfo0`'s small
zone, whose walls were drawn first and filled the clipper before the portal; it now walks from the camera reflected
in the mirror, as mover mirrors did already. The warp portal's far zone and the warped camera are taken from the
frame's eye, and a portal seen from behind (two-sided, from the reflected eye) is checked with its vertices reversed,
since the clipper fills only polygons wound towards the eye. Checked 2026-10-12: the reflected Harry takes the stone;
Lev4_Sneak2's mirror looks the same as before.

The fight's mirror is `Mirror0` (class `Hub5.Mirror`, a rotating mover in the arena at 768, -3264; spells bounce off
it). Its front face is Mirrored + Translucent + Unlit with the `MirrorMain2` frost. A translucent BSP mirror's
surface is drawn over its reflection (it is PF_NoOcclude); a mover's mirror face was only a portal, so the mirror
showed a clear reflection. Flipendo draws a translucent mover mirror face over its reflection too, white-grey as in
the original. Not checked in HP1's code: after a reflection `OccludeBsp` marks the surface invisible unless the
render device's dword at +0x48 is set (`ShinySurfaces`, +0x5C, decides whether a translucent mirror reflects at all;
D3DDrv has it on).

## One-sided BSP surfaces seen from behind

UE1's BSP renderer draws a surface only from its front side unless it is `PF_TwoSided`, and a surface it doesn't draw
doesn't hide what is behind it (stock UE1 behaviour, not reversed in Render.dll). SurrealEngine drew BSP surfaces from
both sides (Vulkan without culling) and let them occlude. HP2's PrivetDr has a lone editor guide plane (`greenguide`,
`PF_NotSolid` but not `PF_Invisible` on one face) between the title shot's camera and its sky backdrop
(`harrymoviewall`, `PF_FakeBackdrop`): seen from behind it took the sky's place as a dark rectangle behind the Harry
Potter logo. `VisibleFrame::ProcessNodeSurface` now drops a one-sided surface the camera is behind
(`0130-knowwonder-render.patch`, any game; HP1's Lev_Tut1 looks the same).

## HP1 and HP2

HP2's DLLs not read yet for these; what to check there:

- The shadow block in SetupDynamics and the Opacity code in DrawLodMesh: expected to be the same. HP2's D3DDrv adds a
  blend for Lumos surfaces ([hp2/engine.md](../hp2/engine.md)).
- Particles: HP1's particle colour fades by `ScaleGlow * Alpha` ([particles.md](particles.md)); HP2 may use
  `ParticleFX.Opacity * Alpha` instead. Check in HP2's Render.dll before porting.
- LoadMap's fade-out and DrawLevelInfo: HP2's `LoadMap` differs from HP1's by only 8 bytes.
