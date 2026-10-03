# Roadmap

Where the HP1 port stands and what's next. Update this when a step lands. Details of what was reversed
live in `docs/re/`. The native-level checklist is `docs/native_audit.md` (`python tools/native_audit.py`).

Legend: [x] done · [~] partly done / in progress · [ ] not started

## 0. Groundwork
- [x] SurrealEngine as a git submodule (`engine/`, upstream SurrealEngine) + our changes as `patches/`;
      `tools/update_engine.sh` to move to newer upstream
- [x] HP1 retail exe detection (SafeDisc + No-CD hashes), `--autolaunch`, `--logfile`
- [x] HP1 code lives in `hp1/`, engine changes are small `hp1_re:` hooks in `patches/`
- [x] Debug env vars: `HP1_SHOTS`/`HP1_SHOT_DIR` screenshots, `HP1_KEYS` scripted key presses,
      `HP1_MOUSE` scripted raw mouse moves, `HP1_TRACE` actor state log (now with zone, pitch, view rotation), `HP1_CAMERA` fixed camera (`hp1/HP1Debug.cpp`)
- [x] `tools/native_audit.py` also reads our `hp1/` overrides (they win for HP1)
- [x] `hp1/mods/`: additions the original doesn't have, kept apart from the port (`--vanilla` turns the
      default-on ones off). Cutscene and storybook skip ("Press Space to skip"), `--skip-splash`, `--skip-intro`
- [x] `DynamicLoadObject("Package.Group.Name")` finds the object (patch 0003, NObject.cpp): the New Game
      storybook pictures (StoryBookTest.utx) were missing, only the subtitles showed
- [x] `// IDA <dll>: <decorated name> [HP1 0x...]` tags on every reimplemented function (for re-checking
      and for HP2)
- [x] Mouse cursor is only recentered while the game window is the foreground window (patch 0004)

## 1. Characters move (skeletal animation) ← current
- [x] `UAnimation` HP1 format loader (`hp1/Anim/HP1Animation.cpp`)
- [x] Anim state natives: PlayAnim, LoopAnim, TweenAnim, IsAnimating, FinishAnim, CreateAnimChannel,
      HasAnim, GetAnimGroup, LinkSkelAnim, BoneNumber, BoneName
- [x] Anim tick: TweenAlpha blend, notifies, AnimEnd, aux channels
- [x] Skeletal pose + skinning: characters render (`hp1/Anim/HP1Skeletal.cpp`: ApplyAnim, GetFrame,
      GetMeshCoords incl. the Y mirror, Wideness, bAlignBottom)
- [~] Channel blending in the pose (AuxAnims per bone subtree) — implemented, not yet verified in game
- [ ] Verify animations visually against the original (walk/run/breathe, tween blends)
- [ ] Root motion (`bAnimMove`: GetRootMovement / AdjustRootMovement / AnimCycleMovement). **Blocks Lev_Tut1**:
      harry.uc's MountFinish subtracts the climb anims' own movement (30 forward + 32/64/96 up) from MountDelta, so
      without root motion a ledge grab plays climb96start/end in place and Harry never gets onto the ledge
- [ ] BonePos, GetBoneCoords, weapon/wand attachment (WeaponBoneIndex, WeaponAdjust), AttachToOwner
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
- [x] Player input: HP1 binds `Up=MoveForward | Button bBroomPitchUp`; upstream didn't trim `|`
      subcommands, so the alias was never found and Harry couldn't move (`Engine::GetSubcommands`)
- [x] `APawn::physicsRotation` port: pawns, including the player, turn towards DesiredRotation every physics
      step (Harry ran sideways in cutscenes). Flying/swimming roll banking not ported yet
- [ ] Some cutscene kids reportedly look like they walk while running (all play `run` at rate 1.5 with
      finished tweens; needs a closer look at which ones)
- [ ] Kids spawned on the same patrol point can overlap (UE1 Spawn fails when the spot is occupied?)
- [ ] Verify CT_Box against `UBox::LineCheck/PointCheck` (Engine.dll 0x103FE620/0x103FE590); CT_Shape
      (decorations/movers: box from mesh/brush) still uses upstream's cylinder/brush collision
- [x] The player slides along actor walls instead of sticking to them (upstream TickWalking left player-vs-actor
      hits as a TODO; HP1's `APawn::stepUp` slides). Harry can now run up the Lev_Tut1 stairs along the
      BlockAll banister and reach the Ron cutscene
- [~] Play through Lev_Tut1 + Lev_Tut1b, fix what breaks (`tools/run_hp1.sh 60 --url=Lev_Tut1`). With `HP1_GOTO`:
      stairs → Ron cutscene → door D1stA → CutScene52 → Fred & George's bookcase room (~126 s) works; stuck at the
      bookcase climb (root motion, phase 1). Route ahead (from `HP1_DUMP`): climbexit door (1032,-3744) → jumping
      help (1700,-3930) → Peeves → wizard cards (2990,-4960) → FGsec2/DADA doors → CUTFLIPBEGIN (956,-6699)
- [x] Ledge grabbing: `APawn::Mount` (`hp1/HP1Pawn.cpp`), called from walking (stepUp) and falling wall hits. Only
      BSP surfaces with PolyFlags 0x1000 (PF_SpecialPoly = HP1's "mountable") qualify. Upstream's cylinder collision
      can report the node of a neighbouring plane, so the face is re-found with a zero-extent ray
- [x] `Actor.SetCollisionSize` has HP1's optional third parameter NewWidth (MountFinish passes three values)
- [ ] Missing Actor natives: ModifySound(567), StopSound(568), SaveGameExists(3972), Wind.GetWind,
      PlayerPawn.ScreenToWorld, Pawn.FindPath, Console.CreateNativeFont
- [ ] Unknown console command `Snap` (FEBook.OpenBook `Snap 3`: screenshot for the save thumbnail, see phase 4)
- [x] `FellOutOfWorld` on the first tick: HP1 only checks zone 0 in physWalking/physFalling, not flying/swimming/rolling
      (`HP1::PhysicsChecksLeftWorld`), so the flying `tut1Peeves0` waiting outside the BSP now survives. `Tut1McGonagall4`
      still dies: she is placed in zone 0 and falling, which kills her in the original too (leftover actor)
- [x] Widescreen / high-res 2D (`hp1/HP1Canvas.cpp`): HUD and cutscene letterbox bars use the full window width,
      menus (FEBook, story book, message boxes) are drawn in a centred 4:3 area, UI scale is fractional (canvas
      768 units tall, like 1024x768), and the options page lists the display's real resolutions
      (`FEOptionsPage.IsSupportedResolution` replaced by a native)
- [x] Camera flew off towards the world origin when looking up (mouse up): `Actor.TraceActors` (309) overridden
      (`hp1/HP1TraceTexture.cpp`, from execTraceActors/MultiLineCheck). Upstream's iterator returned HitLocation
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
- [ ] Save/LoadGameSaveInfo, Save/LoadObjectAsFile, SaveGameExists (`GameSaveInfo` class)
- [ ] CreateTextureFromScreenShot / CreateTextureFromBMP (save thumbnails)
- [ ] FEBook pages that depend on the above

## 5. Remaining native classes and polish
- [ ] Wind, ImpactSoundSet, SoundContainer, InterpolationManager, ClipMarker, LocationID
- [ ] Quidditch / broom levels
- [ ] Full game playthrough; per-level bug list
- [ ] Editor-only natives (BrushBuilders) — low priority

## Engine hooks (upstream files we touch)

These live in `patches/` (0001 exe detection, 0002 launcher flags, 0003 hooks into `hp1/`, 0004 cursor
recentering only while focused) and are kept
minimal so upstream updates rarely conflict.

| File | Hook |
|---|---|
| `engine/CMakeLists.txt` | includes `hp1/hp1.cmake` |
| `Package/PackageManager.cpp` | `HP1::RegisterNatives()` after upstream natives |
| `Packages/Engine/Resources/Mesh/UAnimation.cpp` | `HP1::LoadAnimation` |
| `Packages/Engine/Actors/UActor_Animation.cpp` | `HP1::TickAnimation` |
| `Render/VisibleMesh.cpp` | `HP1::DrawSkeletalMesh` in `DrawSkeletalMesh` |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetRenderBoundingBox` in `UpdateBspInfo` |
| `Packages/Engine/Actors/UActor_PhysMovingBrush.cpp` | `HP1::MoverPhysicsBegin/End` (Mover's shadowed PhysAlpha/PhysRate) |
| `Render/RenderSubsystem.cpp` | `HP1::OnFrameRendered` (`HP1_SHOTS` debug screenshots) |
| `Render/RenderCanvas.cpp`, `RenderSubsystem.h` | `HP1::CanvasUIScale` (float `uiscale`), `HP1::SetCanvasArea` (full-width HUD, 4:3 console/menus); `DrawClippedActor` relative to the canvas area |
| `Engine.cpp` | `HP1::ViewFovAngle` after PlayerCalcView (Hor+ FOV); trim `\|` input subcommands; `getres` → `HP1::AvailableResolutions`; `HP1::MenuMousePosition` in `OnWindowMouseMove` |
| `Collision/TopLevel/TraceTest.cpp`, `OverlapTest.cpp`, `CollisionSystem.cpp` | CT_Box trace/overlap/hash extents |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnMoveToward`, `HP1::PawnPhysicsTime`, `HP1::PawnPhysicsRotation` |
| `Packages/Engine/Actors/Pawn/UPlayerPawn.cpp` | `HP1::PawnPhysicsRotation` |
| `UE1GameDatabase.h`, `GameApp.cpp` | exe hashes, `--autolaunch` / `--logfile` |
| `SurrealWidgets/.../win32_display_window.cpp` | cursor recentering and raw mouse/keyboard input need foreground focus (0004; raw input is RIDEV_INPUTSINK, so moving the mouse in another app turned the camera) |
| `Packages/Engine/Actors/UActor_Phys.cpp`, `UActor_PhysRolling.cpp` | `HP1::PhysicsChecksLeftWorld` (zone-0 FellOutOfWorld only while walking) |
| `Packages/Engine/Actors/UActor_PhysWalking.cpp` | player slides along actors it hits (no pushable decoration); `HP1::PawnMount` before the step up |
| `Packages/Engine/Actors/UActor_PhysFalling.cpp` | `HP1::PawnMount` on a wall hit |
| `Packages/Engine/Actors/UActor.cpp` | `HP1::TickParticleFX` in Tick, `HP1::ParticleFXDestroyed` in Destroy |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetParticleBoundingBox` for DT_Particles (8) |
| `Render/VisibleActor.cpp` | DT_Particles actors drawn in the translucent pass by `HP1::DrawParticleSystem` |
| `Engine.cpp` | `HP1::DebugCamera` after PlayerCalcView (`HP1_CAMERA`); `HP1::TickMods` after the console tick; `HP1::ModsKeyDown` in OnWindowKeyDown |
| `Render/RenderCanvas.cpp` (PostRender) | `HP1::PostRenderMods` after the HUD and console/menus |
| `Native/NObject.cpp` | DynamicLoadObject resolves "Package.Group.Name" |
