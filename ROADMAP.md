# Roadmap

Where the HP1 port stands and what's next. Update this when a step lands.

This file is the checklist only: what works, what doesn't, what's next. How things work goes in `docs/`
([docs/README.md](docs/README.md)): what was reversed in `docs/re/`, the SurrealEngine hooks in
[docs/engine-hooks.md](docs/engine-hooks.md), workflow and tools in [docs/development.md](docs/development.md).
The native-level checklist is [docs/re/native_audit_hp1.md](docs/re/native_audit_hp1.md) (`python tools/native_audit.py`).

Legend: [x] done · [~] partly done / in progress · [ ] not started

## Next up (in order)
1. **Lev_Tut1, the Flipendo lesson**: played by hand (2026-10-04) through the wizard card room, Malfoy, Hermione and
   Quirrell into the Flipendo lesson, which now runs (template, wand, pass mark, timer, IceTexture blackboard). The
   lesson hung on the second judging round (`disable('Tick')` outlived the state, fixed). After it, CutScene60 opens the
   classroom door, Quirrell leads Harry out and the level changes to Lev_Tut1b (tested with `@trigger CUTFLIPBEGIN`:
   inventory travels, first-tick autosave, Lev_Tut1b's intro plays). Next: play the whole lesson by hand to confirm
   scoring over all 4 rounds, then Lev_Tut1b.
2. Broom / Quidditch levels now load and their paths fly (see phase 5). Lev4_Sneak runs: a `goto` to a missing label
   (gargoyle.lookaround's `lcloop`) now stops the state code like UE1 instead of a fatal error.
3. **Dark levels**: HP1's light maps are ported (phase 5); the Quidditch pitch is lit like the original. Still to look
   at: Lev_Tut2 and Lev5_FlyKeys (were nearly black).
4. **First prebuilt release** (see "Releases"): players can't try Flipendo without building it.

Visual parity checks against the original, side by side, wait until the end (after gameplay works): mesh lighting
(brightness, specular highlights, light fades; ported in phase 5), animations (walk/run/breathe, tween blends, aux
channels) and particle effects.

## 0. Groundwork
- [x] SurrealEngine as a git submodule (`engine/`, SurrealEngine) + our changes as `patches/`;
      `tools/update_engine.sh` to move to newer SurrealEngine
- [x] HP1 retail exe detection (SafeDisc + No-CD hashes), `--autolaunch`, `--logfile`
- [x] Native audit vs our disc's scripts (`tools/native_audit.py` → `docs/re/native_audit_hp1.md`)
- [x] Our own scripts: `tools/extract_scripts.sh` extracts the source text embedded in our `.u` files (1247 classes,
      original comments, generated defaultproperties) into `reference/hp1/ScriptSource/` with UELib (`tools/Unreal-Library`
      submodule, our extractor `tools/uelib_dump/`).
      `uelib_dump props <package>` dumps every export of a package (maps: all actors with properties)
- [x] IDA database of `Engine.dll`: `../ida/hp1/Engine.dll.i64`
- [x] HP1 code lives in `hp1/`, engine changes are small `flipendo:` hooks in `patches/`
- [x] Debug env vars: `HP1_HEIGHTMAP` floor heights over a grid (route planning), `HP1_SHOTS`/`HP1_SHOT_DIR` screenshots, `HP1_KEYS` scripted key presses,
      `HP1_MOUSE` scripted raw mouse moves, `HP1_TRACE` actor state log (now with zone, pitch, view rotation), `HP1_CAMERA` fixed camera,
      `HP1_DUMP` actor list (class, state, location, Tag, Event), `HP1_GOTO` waypoint autopilot with jump/wait steps (`kw/KWDebug.cpp`)
- [x] Decompiled dump of Engine/Core/Render.dll (3714/2318/237 functions) in `../ida/hp1/decomp/` with an index per DLL
      (`tools/ida_dump.py`; not in the repo)
- [x] Script events raised by HP1's natives vs SurrealEngine (`docs/re/script_events.md`): 77 raised, 18 never raised
      by SurrealEngine (8 of them net/stat-log only)
- [x] `tools/native_audit.py` also reads our `hp1/` overrides (they win for HP1)
- [x] `hp1/mods/`: additions the original doesn't have, kept apart from the port (`--vanilla` turns the
      default-on ones off). Cutscene and storybook skip ("Press Space to skip"), `--skip-splash`, `--skip-intro`
- [x] `DynamicLoadObject("Package.Group.Name")` finds the object (patch 0003, NObject.cpp): the New Game
      storybook pictures (StoryBookTest.utx) were missing, only the subtitles showed
- [x] `// IDA <dll>: <decorated name> [HP1 0x...]` tags on every reimplemented function (for re-checking
      and for HP2)
- [x] Mouse cursor is only recentered, and raw mouse/keyboard input only used, while the game window is the
      foreground window (patch 0004; raw input arrives in the background, moving the mouse elsewhere turned the camera)
- [x] Missing game data is reported ([docs/troubleshooting.md](docs/troubleshooting.md)): an unrecognised game folder
      says why (no such folder, the System folder itself, no `System/HP.exe`, unknown exe SHA-1); a missing package
      names who needs it and every `Paths=` folder searched; missing maps, missing `Paths=` folders, failed
      DynamicLoadObject calls and imports a package lacks are logged
- [x] Documentation split: README (players: screenshots, status, how to play, extras, Discord), CONTRIBUTING.md
      (ways to help, building, developer flags, ground rules), ROADMAP (status), `docs/` (how things work)
- [x] README screenshot gallery and social preview (`docs/screenshots/`, taken with `HP1_SHOTS`, no official logos)

## 1. Characters move (skeletal animation) ← current
- [x] `UAnimation` HP1 format loader (`kw/Anim/KWAnimation.cpp`)
- [x] Anim state natives: PlayAnim, LoopAnim, TweenAnim, IsAnimating, FinishAnim, CreateAnimChannel,
      HasAnim, GetAnimGroup, LinkSkelAnim, BoneNumber, BoneName
- [x] Anim tick: TweenAlpha blend, notifies, AnimEnd, aux channels
- [x] Skeletal pose + skinning: characters render (`kw/Anim/KWSkeletal.cpp`: ApplyAnim, GetFrame,
      GetMeshCoords incl. the Y mirror, Wideness, bAlignBottom)
- [~] Channel blending in the pose (AuxAnims per bone subtree) — implemented, not yet verified in game
- [ ] Verify animations visually against the original (walk/run/breathe, tween blends)
- [x] Root motion (`bAnimMove`: banked in ApplyAnim, GetRootMovement / AdjustRootMovement, applied at the end of
      AActor::Tick; `kw/Anim/KWSkeletal.cpp`, `docs/re/animation.md`). Harry climbs the bookcase in Fred & George's
      room (climb96start/end) and the 32-unit ledge after it (climb32). ApplyAnim also got the original's "unchanged
      frame" early-out (the tween blend ran once per draw call before). AnimCycleMovement has no caller (not ported)
- [x] BonePos, GetBoneCoords, weapon/wand attachment (WeaponBoneIndex, WeaponAdjust), AttachToOwner
      (`kw/Anim/KWSkeletal.cpp`, `kw/KWAttach.cpp`, `docs/re/animation.md`): Harry holds his wand. The weapon frame is
      GetFrame's (WeaponAdjust in the weapon bone, orthonormalized, Y negated), it sets Pawn.WeaponLoc/WeaponRot and the
      weapon's ThirdPersonMesh is drawn in it. `physTrailer` follows the owner's bone AnimBone-1 (broom trail at
      'BroomTail'). The wand was also invisible because SurrealEngine raised RenderOverlays in third person:
      Weapon.RenderOverlays → Canvas.DrawActor leaves the weapon bHidden (HP1 only raises it without bBehindView)
- [x] GetRenderExtent, GetWorldCollisionBox (`kw/KWCollision.cpp`, `docs/re/spells.md`)
- [x] Transient channel cleanup (done in ApplyAnim, like the original)

## 2. Playable tutorial (Lev_Tut1)
- [x] TraceTexture at HP1's native index 285 (footsteps); decals not traced yet
- [x] Doors (Movers) move: HP1's Mover shadows PhysAlpha/PhysRate (`kw/KWMover.cpp`). This also fixed
      cutscene walks (MoveSmooth + timeout teleport) popping characters through closed doors
- [x] Skeletal render box for culling/BSP placement (`USkeletalMesh::GetRenderBoundingBox`)
- [x] Intro cutscene (CutScene4): doors open, kids run through and up the stairs, Harry runs out,
      Dumbledore walks down and greets him. Fixed by:
  - [x] Hor+ field of view (`kw/KWView.cpp`): shots are framed for 4:3; at 16:9 Dumbledore sat under
        the letterbox bar
  - [x] `CollideType` CT_Box collision (`kw/KWCollision.cpp`): BlockAll walls along the stairs were
        250-radius cylinders that closed the staircase (kids froze; Dumbledore got stuck and was
        teleported by the cutscene timeout = the "pop-in"). Also affects Triggers/CutScene volumes
  - [x] `APawn::moveToward` port (`kw/KWPawn.cpp`): 16-unit arrival, steering damping, speed reduction,
        AvgPhysicsTime; kids no longer circle patrol points or wedge against the open door
- [x] Player input: HP1 binds `Up=MoveForward | Button bBroomPitchUp`; SurrealEngine didn't trim `|`
      subcommands, so the alias was never found and Harry couldn't move (`Engine::GetSubcommands`)
- [x] `APawn::physicsRotation` port: pawns, including the player, turn towards DesiredRotation every physics
      step (Harry ran sideways in cutscenes). Flying/swimming roll banking not ported yet
- [ ] Some cutscene kids reportedly look like they walk while running (all play `run` at rate 1.5 with
      finished tweens; needs a closer look at which ones)
- [ ] Kids spawned on the same patrol point can overlap (UE1 Spawn fails when the spot is occupied?)
- [x] CT_Box ported from `UBox::LineCheck/PointCheck` ([docs/re/collision.md](docs/re/collision.md)): box against box in
      the box's frame, CollisionWidth 0 = CollisionRadius, the start-inside rule. CT_Shape (decorations/movers: box from
      mesh/brush) still uses SurrealEngine's cylinder/brush collision
- [x] The player slides along actor walls instead of sticking to them (SurrealEngine TickWalking left player-vs-actor
      hits as a TODO; HP1's `APawn::stepUp` slides). Harry can now run up the Lev_Tut1 stairs along the
      BlockAll banister and reach the Ron cutscene
- [~] Play through Lev_Tut1 + Lev_Tut1b, fix what breaks (`tools/run_hp1.sh 60 --url=Lev_Tut1`). With `HP1_GOTO`:
      stairs → Ron cutscene → door D1stA → CutScene52 → Fred & George's room (~126 s) → bookcase climb (`137:Up:2.5`)
      → shelves along the jelly-bean trail (-32,-4470; 600,-4470; 768,-4432; 864,-3952; 1056,-3744) → climbexit
      trigger (1541,-3749) → jumping-help cutscene → jump room (2080,-3808; 2304,-3824; 2288,-3456; 2640,-3488;
      2650,-3392,J; 2650,-3150 = the west balcony) → through the west arch past the candle stand (2656,-3020;
      2656,-2944) → corridor (2656,-2790; 3136,-2790) → east arch (3136,-2960; 3150,-3030) → jump down onto box C
      (3150,-3068,J; 3150,-3300) → box D (3150,-3346,J; 3150,-3640) → south ledge (3150,-3748,J; 3150,-3930) →
      jumpexit doors (3136,-4100) works. Peeves patrols (Pawn.FindPath). See "Next up"
- [x] Flying pawns keep their vertical velocity (SurrealEngine TickFlying zeroed it; HP1's physFlying doesn't). Peeves
      pitched up towards his higher nav point but couldn't climb and orbited it forever; now he flies his patrol,
      waitforTrigger2, attackCamera, Taunt and obspatrol
- [x] Ledge grabbing: `APawn::Mount` (`kw/KWPawn.cpp`), called from walking (stepUp) and falling wall hits. Only
      BSP surfaces with PolyFlags 0x1000 (PF_SpecialPoly = HP1's "mountable") qualify. SurrealEngine's cylinder collision
      can report the node of a neighbouring plane, so the face is re-found with a zero-extent ray
- [x] `Actor.SetCollisionSize` has HP1's optional third parameter NewWidth (MountFinish passes three values)
- [x] Gameplay events SurrealEngine never raised (`docs/re/script_events.md`, `kw/KWPawn.cpp`):
  - [x] `Falling` when walking or rolling off a ledge, before PHYS_Falling (Pawn.Falling → PlayInAir: Harry's fall
        animation; the boulder stops its rolling sound). Not from physSpider/findNewFloor (PHYS_Spider is unused)
  - [x] physWalking's ledge rule: MayFall once, then a pawn without bCanJump or with bIsWalking stops at the edge
        (SurrealEngine let them walk off)
  - [x] `DoJump` for PlayerPawn.bAutoJump (options menu "Auto Jump"): edge search + the landing predictor
        (sub_103E6310); jumps when that lands > 10 units higher. Harry clears the jump room's gaps by himself
  - [x] `AlterDestination` (PollMoveToward with HP1's Destination/Focus handling, PollStrafeFacing),
        `LongFall` (WaitForLanding: LatentFloat 2.5 s, latent only while falling)
  - [x] `KeyFrameReached` from mover physics instead of SurrealEngine's InterpolateEnd(None)
- [x] `PreClientTravel` raised by ClientTravel (only PlayerPawn's empty handler in HP1)
- [x] Level change Lev_Tut1 → Lev_Tut1b (CutScene60 `CHANGELEVEL` → ServerTravel with items, first-tick autosave)
- [x] Spell lesson no longer hangs on its second round: `GotoState` re-enables events turned off with `Disable`, like
      Core.dll ([docs/re/spells.md](docs/re/spells.md))
- [x] `Pawn.FindPath` (553, KnowWonder's station pathing; tut1Peeves crashed the game without it,
      `kw/KWNavigation.cpp`)
- [x] ModifySound(567), StopSound(568) (`kw/KWSound.cpp`; Galaxy.dll's match: first sound with the slot's Id and,
      if given, the same Sound). BroomHarry crashed on the first tick without it
- [x] Missing natives ([docs/re/native_classes.md](docs/re/native_classes.md), `kw/KWPlayerNatives.cpp`): ScreenToWorld
      and FindStairRotation ported (no HP1 script reaches either). Console.CreateNativeFont returns None: the Asian
      languages need WinDrv.dll's system-font rasterizer, not reversed yet
- [x] Console command `Snap` (FEBook.OpenBook `Snap 3`) accepted: its snapshot buffer is never read in HP1
      ([docs/re/savegames.md](docs/re/savegames.md))
- [x] `FellOutOfWorld` on the first tick: HP1 only checks zone 0 in physWalking/physFalling, not flying/swimming/rolling
      (`HP1::PhysicsChecksLeftWorld`), so the flying `tut1Peeves0` waiting outside the BSP now survives. `Tut1McGonagall4`
      still dies: she is placed in zone 0 and falling, which kills her in the original too (leftover actor)
- [x] Widescreen / high-res 2D (`hp1/HP1Canvas.cpp`): HUD and cutscene letterbox bars use the full window width,
      menus (FEBook, story book, message boxes) are drawn in a centred 4:3 area, UI scale is fractional (canvas
      768 units tall, like 1024x768), and the options page lists the display's real resolutions
      (`FEOptionsPage.IsSupportedResolution` replaced by a native)
- [x] Camera flew off towards the world origin when looking up (mouse up): `Actor.TraceActors` (309) overridden
      (`kw/KWTraceTexture.cpp`, from execTraceActors/MultiLineCheck). SurrealEngine's iterator returned HitLocation
      (0,0,0), traced End->Start and never reported BSP hits as LevelInfo, which `BaseCam.CheckPosition` relies on
- [ ] Windowed mode: menu mouse mapping (`WindowsMouseX/Y` into the 4:3 area) not yet tested in game

## 3. Spells
- [x] `Gesture` spell-drawing recognition (CompareGesture, CompareGesturePoint) (`kw/KWGesture.cpp`)
- [x] `ParticleFX` simulation, emission and natives (`kw/KWParticleFX.cpp`, `docs/re/particles.md`): Tick/Update,
      UParticle::Update (gravity, damping, attraction, chaos, drip, colour palettes, elasticity bounce), all
      distributions incl. owner mesh and gesture patterns, ParentBlend. Lev_Tut1's torch fires burn
- [x] ParticleFX billboard rendering (`kw/KWParticleRender.cpp`, from Render.dll `URender::DrawParticleSystem`)
- [x] ParticleFX render passes (`kw/KWParticleRender.cpp`, `docs/re/particles.md` "Rendering"): Line ribbon, Shard
      triangle, Liquid (hanging drop / falling streak: water drips, fountains), Billboard, bShellOnly screen-space pass,
      stacked per RenderPrimitive like the original. TriTube isn't drawn (the original fails an assert after it).
      The shipped content only uses Billboard and Liquid
- [x] ParticleFX lighting for bUnlit=False systems (LightColor from SurrealEngine's vertex light; no shipped system is lit),
      the billboard overdraw budget (PriorityTag-weighted screen area fades particles), Mover hits for Elasticity (the
      original's FastLineCheck + SingleLineCheck rule). LodParticleDensity/Lod/LodParticles are dead code in HP1
- [x] Wind (`kw/KWWind.cpp`): AWind::Tick fluctuation, GetWind (point/directional, noise, falloff), GetTotalWind for
      ParticleFX, Wind.GetWind (425)
- [ ] Verify particle effects against the original side by side (spell trails, fires). Checked in ours only: Lev_Tut1 torch
      fires, the Lev2_HogFront fountain (liquid streaks), Lev2_fire1 water drips (hanging drops), a spell cast's puff
- [~] Spell targeting (`docs/re/spells.md`): `Actor.GetWorldCollisionBox` (286) ported, the stub's empty box made lock-on
      FX and spell homing aim at the world origin; `GetRenderExtent` (274) too. A cast (AltFire → playeraiming → Target →
      CastSpell) works in Lev_Tut1. Still to test: locking onto a real victim (eVulnerableToSpell picks the spell) and
      hit reactions, which need the Flipendo lesson

## 4. Save games and front end
- [x] Saving and loading (`kw/KWSave.cpp`, [docs/re/savegames.md](docs/re/savegames.md)): `SaveGame N` writes
      `Save/SaveN.usa` at once (SurrealEngine deferred it to the end of the frame, after doLevelSave had restored
      Level.Pauser), `open saveN.usa` (FESlotPage) loads it through SurrealEngine's `?load=N`. Tested in Lev_Tut1:
      Harry, level time and scripts come back where they were saved, the next cutscene triggers
- [x] Save/LoadGameSaveInfo (`Save/GameSaveInfoN`, the original's raw layout), SaveGameExists
- [x] LevelEnterText = the travel URL's map when the level has none (LoadMap), so slots get their level name
- [x] CreateTextureFromBMP: the slot page shows each slot's thumbnail (`Save/SGS <level>.bmp`)
- [ ] Play the whole New Game → save point → quit → Load Game loop by hand through the menus (only driven with
      `HP1_EXEC` so far), and a save made after a level change (autosave on the first tick)
- [~] After loading, the camera sits a little further back than when saved: not reproduced with a standstill save in
      Lev_Tut1 (camera and screenshots identical, [docs/re/savegames.md](docs/re/savegames.md)); try a save taken
      while the camera moves
- [ ] Save/LoadObjectAsFile, CreateTextureFromScreenShot, `Snap 3`: no script uses them (low priority)

## 5. Remaining native classes and polish
- [x] InterpolationManager (`kw/KWInterpolation.cpp`): performPhysics flies the Owner along InterpolationPoint Bezier
      segments (DesiredSpeed/IPSpeed, bConstantSpeed correction, pauses, view targets, bFaceMoveDirection, rotation
      smoothing, Catmull-Rom bNewRotationSmoothing) and raises UpdateCamera, InterpolateEnd(manager, bForward) and
      FinishedInterpolation. Quidditch Bludgers/Snitch/Quaffle fly their paths at ~300 u/s
- [x] Struct defaults with fixed array members (QuidCommentator's `CommentInfo Variant[8]`) loaded only element 0, which
      desynced every Quidditch/broom map on load ("Property value does not match property type!")
- [x] Mesh lighting (`kw/KWMeshLight.cpp`, [docs/re/lighting.md](docs/re/lighting.md)): HP1's light picking (3 static
      lights, line of sight, fades), light colours and LightType effects, linear falloff with LightRadiusInner, diffuse +
      specular per vertex, and back-face culling of skeletal meshes. Fixes the classroom blackboards' diagonal split
      (front and back quads z-fought). Not yet compared side by side with the original
- [x] Skeletal mesh back-face cull and vertex normals had the winding backwards (the original tests in view space, a
      reflection of world space): every character was drawn inside out and lit from behind ([docs/re/lighting.md](docs/re/lighting.md))
- [x] BSP light maps (`kw/KWLightmap.cpp`, [docs/re/lighting.md](docs/re/lighting.md) "Light maps"): HP1's `LightSource`
      (`LD_Plane` parallel, `LD_Ambient`), `LightRadiusInner`, its smoothstep falloff, the 2x colour scale with 7-bit
      clamps, bDarkLight, the zone ambient, and `WorldLightRadius` scaled by DrawScale. The Quidditch pitch is lit like the original.
      Waver flicker and the rarer light effects (~60 lights) still use SurrealEngine's
- [ ] Bugs of the original to fix, not reproduce: [docs/re/original_bugs.md](docs/re/original_bugs.md)
- [x] ImpactSoundSet, SoundContainer, ClipMarker, LocationID: Engine.dll has no native code for them, only boilerplate;
      their script classes are enough ([docs/re/native_classes.md](docs/re/native_classes.md))
- [ ] Console.CreateNativeFont for the Asian languages (WinDrv.dll's GDI font rasterizer)
- [x] `ViewFlash` (UGameEngine::Tick) and the screen flash: HP1 has no FlashScale, FlashFog.W is the brightness
      (`kw/KWView.cpp`). Cutscene FadeIn/FadeOut, damage flashes and the level fade-in now show
- [~] Quidditch / broom levels: Lev_Tut2, Lev2_Quid1, Lev2_RemChase, Lev5_FlyKeys load and run their intros
- [ ] Full game playthrough; per-level bug list
- [ ] Editor-only natives (BrushBuilders) — low priority

## 6. Modding (`hp1/mods/` and beyond)
- [x] `hp1/mods/` with `--vanilla`: cutscene skip, storybook skip, `--skip-splash`, `--skip-intro`
- [ ] Per-mod on/off and settings in an ini section (`[Flipendo.Mods]`), not only command-line flags; an *Extras*
      page in the options book (FEBook) to toggle them in game
- [ ] Drop-in content mods: `Mods/<name>/` folders added to the package search path ahead of the originals
      (`PackageManager::ScanPaths` already reads `Core.System` Paths from the ini, and `ScanFolder` keeps the first
      folder that has a package of that name, so mod folders must be scanned before the originals). Define load
      order between mods, and test a texture pack and a custom map
- [ ] Script mods: replace a game class with a mod subclass at spawn time (e.g. `HarryPotter.harry` → `MyMod.MyHarry`),
      configured in the ini, without touching the original packages
- [ ] Run custom levels made by the HP1 modding community (collect a few test maps, list what breaks)
- [ ] More built-in extras: FOV slider, frame limiter / uncapped framerate, controller support, speedrun timer,
      free camera
- [ ] In-game modding tools: Dear ImGui overlay with actor list, live property inspector, console and mod manager,
      built from the `HP1_DUMP`/`HP1_TRACE`/`HP1_EXEC` debug tools ([docs/modding.md](docs/modding.md))
- [~] Modder docs: [docs/modding.md](docs/modding.md) (writing a mod, hooks, tools); still missing a worked
      "first mod" walkthrough

## 7. Editor (grows out of the §6 ImGui overlay)
The editor runs inside the game (no separate UnrealEd-style program): play-in-editor for free, edits visible while
the game runs. Steps 1–2 can go alongside the first playable release (they double as debugging tools); the rest
after it.

Ground rules:
- Read and write the original formats (`.unr`, `.u`), so existing community maps open in Flipendo and Flipendo maps
  are ordinary UE1 packages. No private map format.
- UnrealScript stays the game's language. The new scripting layers sit on top and never replace it, so existing
  mods keep working.
- Same editor for HP1 and HP2, gated like the rest (`IsKnowWonder()`), and it runs on Linux too.
- First check what SurrealEngine's own `SurrealEditor` (builds alongside the game) already has: package loading,
  viewports, anything reusable. Building on it means less of our code in `engine/`.

1. Core
   - [ ] Object tree: every actor in the level, filterable (class, tag, event, name, gamestate) and sortable, click to
         select and focus the camera
   - [ ] 3D previews of meshes, textures and prefabs in the browsers
   - [ ] Property inspector with edit (the §6 overlay one), showing which values differ from the defaults
   - [ ] ImGuizmo for move / rotate / scale, grid and angle snapping, multi-select
   - [ ] Load maps and packages from the editor (file picker, recent list, `Mods/` folders included)
   - [ ] Save to `.unr`: package writer (name / import / export tables). **Biggest risk:** BSP / CSG rebuild after
         moving brushes; check early whether anything usable exists (SurrealEngine has no builder) before relying on it
   - [ ] Undo / redo, copy / paste (also across maps), autosave and crash recovery
2. Clarity
   - [ ] Trigger graph: Event → Tag links drawn as lines in the viewport, highlight what a trigger fires
   - [ ] Validator: broken references, missing textures, triggers that fire nothing, unreachable path nodes; click a
         result to jump to it
   - [ ] Gamestate preview: switch GState000/GState010/… and see which actors exist
   - [ ] Search across all maps and packages ("where is this class used?")
   - [ ] Prefab browser
3. Scripting
   - [ ] Sequence editor (timeline + nodes) for cutscenes and scripted moments; it outputs the game's existing cutscene
         commands, so the result runs like an original cutscene
   - [ ] Lua (or similar) for mod logic, with hot reload; bindings to actors, events and the cutscene system
4. Assets and external tools
   - [ ] Blender addon: import / export of the KnowWonder skeletal mesh and animation format (custom characters)
   - [ ] "Edit in external app" for textures (Photoshop / Krita / GIMP): watch the file, reimport live on save. Small
         built-in editor only for quick fixes (alpha, resize, palette)
   - [ ] Text map format (T3D-like) for diffs and teamwork in git
   - [ ] Sound browser with preview, dialogue and lipsync editor, localization strings
5. HP tools
   - [ ] Spell editor: draw new gesture shapes for the recognizer (`Gesture`) and attach effects
   - [ ] ParticleFX editor with live preview (`kw/KWParticleFX.cpp`)
   - [ ] Lighting: rebuild lightmaps, per-surface light scale preview
   - [ ] Path-node building and visualization for AI navigation
6. Sharing
   - [ ] "Package mod": writes `Mods/<name>/` with a manifest (version, dependencies, game)
   - [ ] In-game mod browser: enable / disable, load order; later a community mod index

## Releases
- [ ] Prebuilt Windows download on the release page (zip with `SurrealEngine.exe` and its DLLs; no game data), with a
      changelog and a "point it at your game folder" first-run
- [ ] Game folder picker in the launcher remembered between runs, so players never touch the command line
- [ ] Version number in the window title and the log, for bug reports
- [ ] Linux / Steam Deck build tested with Flipendo

## Later: the other KnowWonder games
- [~] HP2 (Chamber of Secrets, UE1 build 433): groundwork done, port after HP1.
  - [x] Shared engine layer: `kw/` (KnowWonder engine) vs `hp1/` (HP1 only), hooks gated by `IsKnowWonder()`
  - [x] `hp2/` (HP2 only) and the no-duplication rules ([hp2/README.md](hp2/README.md)): HP2-only natives `BoneRot`,
        `IsSoftwareRendering`, `GetCurrentKeyState`, the `StopSound` adapter (HP2 added `FadeOutTime`), `KW::IsHP2()`
  - [x] Comparison with HP1 ([docs/re/hp2_compare.md](docs/re/hp2_compare.md)): Fire.dll identical, Core 82%, Engine 62%
        identical (+14% offsets only); of our 120 tagged ports 23 identical, 17 offsets only, 74 changed
  - [x] Scripts extracted (`tools/extract_scripts.sh hp2`, most source stripped, decompiled), native audit
        ([docs/re/native_audit_hp2.md](docs/re/native_audit_hp2.md)): 151 OK, 22 covered by HP1 ports, 21 missing, 28 stubs
  - [x] The ~29 extra bytes in most "changed" exec* wrappers: HP2 checks for a new DebugInfo bytecode token after
        every native call; HP2's token table shifts GlobalFunction..FloatToBool by one
        ([docs/re/hp2_bytecode.md](docs/re/hp2_bytecode.md), `hp2/HP2Bytecode.cpp`)
  - [ ] Hook HP2's token table into SurrealEngine's `BytecodeStream` (skip DebugInfo, `HP2::ToStockToken`)
  - [ ] Check the remaining "changed" ports for real behaviour differences (beyond the DebugInfo check)
  - [ ] HP2 music natives: `PlayMusic` / `StopMusic` / `StopAllMusic` (ALAudio.dll), `StopSound` fade-out
  - [ ] Enable `IsKnowWonder()` for HP2, OpenAL/Ogg audio (ALAudio.dll), first level
- [ ] HP3 (Prisoner of Azkaban, UE2 build 2226, packages 129): no UE2 counterpart of SurrealEngine exists. First check,
      with UELib: how many native classes/functions its gameplay packages have (HP1's have none). Mostly script =
      extending SurrealEngine towards UE2 is worth a look; otherwise fixes for the original exe are the better route
