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

## HP1 and HP2

HP2's DLLs not read yet for these; what to check there:

- The shadow block in SetupDynamics and the Opacity code in DrawLodMesh: expected to be the same. HP2's D3DDrv adds a
  blend for Lumos surfaces ([hp2/engine.md](../hp2/engine.md)).
- Particles: HP1's particle colour fades by `ScaleGlow * Alpha` ([particles.md](particles.md)); HP2 may use
  `ParticleFX.Opacity * Alpha` instead. Check in HP2's Render.dll before porting.
- LoadMap's fade-out and DrawLevelInfo: HP2's `LoadMap` differs from HP1's by only 8 bytes.
