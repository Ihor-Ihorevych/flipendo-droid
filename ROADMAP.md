# Roadmap

Where the HP1 port stands and what's next. Update this when a step lands.

This file is the checklist only: what works, what doesn't, what's next. How things work goes in `docs/`
([docs/README.md](docs/README.md)): what was reversed in `docs/re/`, the SurrealEngine hooks in
[docs/engine-hooks.md](docs/engine-hooks.md), workflow and tools in [docs/development.md](docs/development.md).
The native-level checklist is [docs/native_audit.md](docs/native_audit.md) (`python tools/native_audit.py`).

Legend: [x] done · [~] partly done / in progress · [ ] not started

## Next up (in order)
1. **Lev_Tut1 after the jump room**: the autopilot now crosses the jump room and leaves through the jumpexit doors
   (3140,-4061, ~205 s). Next: wizard cards (2990,-4960) → FGsec2/DADA doors → CUTFLIPBEGIN (956,-6699), and the
   level change to Lev_Tut1b.
2. Verify animations visually against the original (walk/run/breathe, tween blends, aux channels): needs the original
   game running next to ours, side by side.
3. Broom / Quidditch levels now load and their paths fly (see phase 5). Next there: Lev4_Sneak stops on
   `goto lcloop` (gargoyle.lookaround: "Could not find label").

## 0. Groundwork
- [x] SurrealEngine as a git submodule (`engine/`, SurrealEngine) + our changes as `patches/`;
      `tools/update_engine.sh` to move to newer SurrealEngine
- [x] HP1 retail exe detection (SafeDisc + No-CD hashes), `--autolaunch`, `--logfile`
- [x] Native audit vs our disc's scripts (`tools/native_audit.py` → `docs/native_audit.md`)
- [x] Our own scripts: `tools/extract_scripts.sh` extracts the source text embedded in our `.u` files (1247 classes,
      original comments, generated defaultproperties) into `reference/hp1/ScriptSource/` with UELib (`tools/Unreal-Library`
      submodule, our extractor `tools/uelib_dump/`).
      `uelib_dump props <package>` dumps every export of a package (maps: all actors with properties)
- [x] IDA database of `Engine.dll`: `../ida/Engine.dll.i64`
- [x] HP1 code lives in `hp1/`, engine changes are small `flipendo:` hooks in `patches/`
- [x] Debug env vars: `HP1_HEIGHTMAP` floor heights over a grid (route planning), `HP1_SHOTS`/`HP1_SHOT_DIR` screenshots, `HP1_KEYS` scripted key presses,
      `HP1_MOUSE` scripted raw mouse moves, `HP1_TRACE` actor state log (now with zone, pitch, view rotation), `HP1_CAMERA` fixed camera,
      `HP1_DUMP` actor list (class, state, location, Tag, Event), `HP1_GOTO` waypoint autopilot with jump/wait steps (`hp1/HP1Debug.cpp`)
- [x] Decompiled dump of Engine/Core/Render.dll (3714/2318/237 functions) in `../ida/decomp/` with an index per DLL
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
- [x] Documentation split: README (users), ROADMAP (status), `docs/` (how things work)

## 1. Characters move (skeletal animation) ← current
- [x] `UAnimation` HP1 format loader (`hp1/Anim/HP1Animation.cpp`)
- [x] Anim state natives: PlayAnim, LoopAnim, TweenAnim, IsAnimating, FinishAnim, CreateAnimChannel,
      HasAnim, GetAnimGroup, LinkSkelAnim, BoneNumber, BoneName
- [x] Anim tick: TweenAlpha blend, notifies, AnimEnd, aux channels
- [x] Skeletal pose + skinning: characters render (`hp1/Anim/HP1Skeletal.cpp`: ApplyAnim, GetFrame,
      GetMeshCoords incl. the Y mirror, Wideness, bAlignBottom)
- [~] Channel blending in the pose (AuxAnims per bone subtree) — implemented, not yet verified in game
- [ ] Verify animations visually against the original (walk/run/breathe, tween blends)
- [x] Root motion (`bAnimMove`: banked in ApplyAnim, GetRootMovement / AdjustRootMovement, applied at the end of
      AActor::Tick; `hp1/Anim/HP1Skeletal.cpp`, `docs/re/animation.md`). Harry climbs the bookcase in Fred & George's
      room (climb96start/end) and the 32-unit ledge after it (climb32). ApplyAnim also got the original's "unchanged
      frame" early-out (the tween blend ran once per draw call before). AnimCycleMovement has no caller (not ported)
- [x] BonePos, GetBoneCoords, weapon/wand attachment (WeaponBoneIndex, WeaponAdjust), AttachToOwner
      (`hp1/Anim/HP1Skeletal.cpp`, `hp1/HP1Attach.cpp`, `docs/re/animation.md`): Harry holds his wand. The weapon frame is
      GetFrame's (WeaponAdjust in the weapon bone, orthonormalized, Y negated), it sets Pawn.WeaponLoc/WeaponRot and the
      weapon's ThirdPersonMesh is drawn in it. `physTrailer` follows the owner's bone AnimBone-1 (broom trail at
      'BroomTail'). The wand was also invisible because SurrealEngine raised RenderOverlays in third person:
      Weapon.RenderOverlays → Canvas.DrawActor leaves the weapon bHidden (HP1 only raises it without bBehindView)
- [ ] GetRenderExtent, GetWorldCollisionBox (skeletal bounds)
- [x] Transient channel cleanup (done in ApplyAnim, like the original)

## 2. Playable tutorial (Lev_Tut1)
- [x] TraceTexture at HP1's native index 285 (footsteps); decals not traced yet
- [x] Doors (Movers) move: HP1's Mover shadows PhysAlpha/PhysRate (`hp1/HP1Mover.cpp`). This also fixed
      cutscene walks (MoveSmooth + timeout teleport) popping characters through closed doors
- [x] Skeletal render box for culling/BSP placement (`USkeletalMesh::GetRenderBoundingBox`)
- [x] Intro cutscene (CutScene4): doors open, kids run through and up the stairs, Harry runs out,
      Dumbledore walks down and greets him. Fixed by:
  - [x] Hor+ field of view (`hp1/HP1View.cpp`): shots are framed for 4:3; at 16:9 Dumbledore sat under
        the letterbox bar
  - [x] `CollideType` CT_Box collision (`hp1/HP1Collision.cpp`): BlockAll walls along the stairs were
        250-radius cylinders that closed the staircase (kids froze; Dumbledore got stuck and was
        teleported by the cutscene timeout = the "pop-in"). Also affects Triggers/CutScene volumes
  - [x] `APawn::moveToward` port (`hp1/HP1Pawn.cpp`): 16-unit arrival, steering damping, speed reduction,
        AvgPhysicsTime; kids no longer circle patrol points or wedge against the open door
- [x] Player input: HP1 binds `Up=MoveForward | Button bBroomPitchUp`; SurrealEngine didn't trim `|`
      subcommands, so the alias was never found and Harry couldn't move (`Engine::GetSubcommands`)
- [x] `APawn::physicsRotation` port: pawns, including the player, turn towards DesiredRotation every physics
      step (Harry ran sideways in cutscenes). Flying/swimming roll banking not ported yet
- [ ] Some cutscene kids reportedly look like they walk while running (all play `run` at rate 1.5 with
      finished tweens; needs a closer look at which ones)
- [ ] Kids spawned on the same patrol point can overlap (UE1 Spawn fails when the spot is occupied?)
- [ ] Verify CT_Box against `UBox::LineCheck/PointCheck` (Engine.dll 0x103FE620/0x103FE590); CT_Shape
      (decorations/movers: box from mesh/brush) still uses SurrealEngine's cylinder/brush collision
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
- [x] Ledge grabbing: `APawn::Mount` (`hp1/HP1Pawn.cpp`), called from walking (stepUp) and falling wall hits. Only
      BSP surfaces with PolyFlags 0x1000 (PF_SpecialPoly = HP1's "mountable") qualify. SurrealEngine's cylinder collision
      can report the node of a neighbouring plane, so the face is re-found with a zero-extent ray
- [x] `Actor.SetCollisionSize` has HP1's optional third parameter NewWidth (MountFinish passes three values)
- [x] Gameplay events SurrealEngine never raised (`docs/re/script_events.md`, `hp1/HP1Pawn.cpp`):
  - [x] `Falling` when walking or rolling off a ledge, before PHYS_Falling (Pawn.Falling → PlayInAir: Harry's fall
        animation; the boulder stops its rolling sound). Not from physSpider/findNewFloor (PHYS_Spider is unused)
  - [x] physWalking's ledge rule: MayFall once, then a pawn without bCanJump or with bIsWalking stops at the edge
        (SurrealEngine let them walk off)
  - [x] `DoJump` for PlayerPawn.bAutoJump (options menu "Auto Jump"): edge search + the landing predictor
        (sub_103E6310); jumps when that lands > 10 units higher. Harry clears the jump room's gaps by himself
  - [x] `AlterDestination` (PollMoveToward with HP1's Destination/Focus handling, PollStrafeFacing),
        `LongFall` (WaitForLanding: LatentFloat 2.5 s, latent only while falling)
  - [x] `KeyFrameReached` from mover physics instead of SurrealEngine's InterpolateEnd(None)
- [x] `PreClientTravel` raised by ClientTravel (only PlayerPawn's empty handler in HP1; the level change itself is
      untested, the playthrough doesn't get there yet)
- [x] `Pawn.FindPath` (553, KnowWonder's station pathing; tut1Peeves crashed the game without it,
      `hp1/HP1Navigation.cpp`)
- [x] ModifySound(567), StopSound(568) (`hp1/HP1Sound.cpp`; Galaxy.dll's match: first sound with the slot's Id and,
      if given, the same Sound). BroomHarry crashed on the first tick without it
- [ ] Missing Actor natives: Wind.GetWind,
      PlayerPawn.ScreenToWorld, Console.CreateNativeFont
- [ ] Unknown console command `Snap` (FEBook.OpenBook `Snap 3`: screenshot for the save thumbnail, see phase 4)
- [x] `FellOutOfWorld` on the first tick: HP1 only checks zone 0 in physWalking/physFalling, not flying/swimming/rolling
      (`HP1::PhysicsChecksLeftWorld`), so the flying `tut1Peeves0` waiting outside the BSP now survives. `Tut1McGonagall4`
      still dies: she is placed in zone 0 and falling, which kills her in the original too (leftover actor)
- [x] Widescreen / high-res 2D (`hp1/HP1Canvas.cpp`): HUD and cutscene letterbox bars use the full window width,
      menus (FEBook, story book, message boxes) are drawn in a centred 4:3 area, UI scale is fractional (canvas
      768 units tall, like 1024x768), and the options page lists the display's real resolutions
      (`FEOptionsPage.IsSupportedResolution` replaced by a native)
- [x] Camera flew off towards the world origin when looking up (mouse up): `Actor.TraceActors` (309) overridden
      (`hp1/HP1TraceTexture.cpp`, from execTraceActors/MultiLineCheck). SurrealEngine's iterator returned HitLocation
      (0,0,0), traced End->Start and never reported BSP hits as LevelInfo, which `BaseCam.CheckPosition` relies on
- [ ] Windowed mode: menu mouse mapping (`WindowsMouseX/Y` into the 4:3 area) not yet tested in game

## 3. Spells
- [x] `Gesture` spell-drawing recognition (CompareGesture, CompareGesturePoint) (`hp1/HP1Gesture.cpp`)
- [x] `ParticleFX` simulation, emission and natives (`hp1/HP1ParticleFX.cpp`, `docs/re/particles.md`): Tick/Update,
      UParticle::Update (gravity, damping, attraction, chaos, drip, colour palettes, elasticity bounce), all
      distributions incl. owner mesh and gesture patterns, ParentBlend. Lev_Tut1's torch fires burn
- [x] ParticleFX billboard rendering (`hp1/HP1ParticleRender.cpp`, from Render.dll `URender::DrawParticleSystem`)
- [ ] ParticleFX: Line, Liquid, Shard, TriTube and bShellOnly passes (drawn as billboards for now; Render.dll
      functors at 0x10B1B7D0, 0x10B17CE0, 0x10B17350, 0x10B194C0, 0x10B16C70)
- [ ] ParticleFX: lighting for bUnlit=False systems, LodParticleDensity thinning, the billboard overdraw budget,
      Wind (`AWind::GetTotalWind`), Mover hits for Elasticity
- [ ] Verify particle effects against the original (spell trails, fires)
- [ ] Spell targeting / eVulnerableToSpell paths

## 4. Save games and front end
- [x] Saving and loading (`hp1/HP1Save.cpp`, [docs/re/savegames.md](docs/re/savegames.md)): `SaveGame N` writes
      `Save/SaveN.usa` at once (SurrealEngine deferred it to the end of the frame, after doLevelSave had restored
      Level.Pauser), `open saveN.usa` (FESlotPage) loads it through SurrealEngine's `?load=N`. Tested in Lev_Tut1:
      Harry, level time and scripts come back where they were saved, the next cutscene triggers
- [x] Save/LoadGameSaveInfo (`Save/GameSaveInfoN`, the original's raw layout), SaveGameExists
- [x] LevelEnterText = the travel URL's map when the level has none (LoadMap), so slots get their level name
- [x] CreateTextureFromBMP: the slot page shows each slot's thumbnail (`Save/SGS <level>.bmp`)
- [ ] Play the whole New Game → save point → quit → Load Game loop by hand through the menus (only driven with
      `HP1_EXEC` so far), and a save made after a level change (autosave on the first tick)
- [ ] After loading, the camera sits a little further back than when saved (BaseCam native state not in the save?)
- [ ] Save/LoadObjectAsFile, CreateTextureFromScreenShot, `Snap 3`: no script uses them (low priority)

## 5. Remaining native classes and polish
- [x] InterpolationManager (`hp1/HP1Interpolation.cpp`): performPhysics flies the Owner along InterpolationPoint Bezier
      segments (DesiredSpeed/IPSpeed, bConstantSpeed correction, pauses, view targets, bFaceMoveDirection, rotation
      smoothing, Catmull-Rom bNewRotationSmoothing) and raises UpdateCamera, InterpolateEnd(manager, bForward) and
      FinishedInterpolation. Quidditch Bludgers/Snitch/Quaffle fly their paths at ~300 u/s
- [x] Struct defaults with fixed array members (QuidCommentator's `CommentInfo Variant[8]`) loaded only element 0, which
      desynced every Quidditch/broom map on load ("Property value does not match property type!")
- [ ] Wind, ImpactSoundSet, SoundContainer, ClipMarker, LocationID
- [x] `ViewFlash` (UGameEngine::Tick) and the screen flash: HP1 has no FlashScale, FlashFog.W is the brightness
      (`hp1/HP1View.cpp`). Cutscene FadeIn/FadeOut, damage flashes and the level fade-in now show
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
- [~] Modder docs: [docs/modding.md](docs/modding.md) (writing a mod, hooks, tools); still missing a worked
      "first mod" walkthrough

## Later: the other KnowWonder games
- [ ] HP2 (Chamber of Secrets, UE1 build 433, packages 79): same pipeline. The `// IDA` tags use decorated names so
      the same functions can be found in HP2's DLLs; UELib reads its packages
- [ ] HP3 (Prisoner of Azkaban, UE2 build 2226, packages 129): no UE2 counterpart of SurrealEngine exists. First check,
      with UELib: how many native classes/functions its gameplay packages have (HP1's have none). Mostly script =
      extending SurrealEngine towards UE2 is worth a look; otherwise fixes for the original exe are the better route
