# Roadmap

Where the HP1 port stands and what's next. Update this when a step lands. Details of what was reversed
live in `docs/re/`. The native-level checklist is `docs/native_audit.md` (`python tools/native_audit.py`).

Legend: [x] done · [~] partly done / in progress · [ ] not started

## 0. Groundwork
- [x] SurrealEngine as a git submodule (`engine/`, our mirror) + our changes as `patches/`;
      `tools/update_engine.sh` to move to newer upstream
- [x] HP1 retail exe detection (SafeDisc + No-CD hashes), `--autolaunch`, `--logfile`
- [x] HP1 code lives in `hp1/`, engine changes are small `hp1_re:` hooks in `patches/`
- [x] Debug env vars: `HP1_SHOTS`/`HP1_SHOT_DIR` screenshots, `HP1_KEYS` scripted key presses,
      `HP1_TRACE` actor state log (`hp1/HP1Debug.cpp`)
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
- [ ] Root motion (`bAnimMove`: GetRootMovement / AdjustRootMovement / AnimCycleMovement)
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
- [ ] Play through Lev_Tut1 + Lev_Tut1b, fix what breaks (`tools/run_hp1.sh 60 --url=Lev_Tut1`)
- [ ] Missing Actor natives: ModifySound(567), StopSound(568), SaveGameExists(3972), Wind.GetWind,
      PlayerPawn.ScreenToWorld, Pawn.FindPath, Console.CreateNativeFont
- [ ] Unknown console command `Snap`
- [ ] Peeves and a McGonagall get `FellOutOfWorld` on the first tick (zone/physics difference vs the original)
- [ ] Cutscene letterbox bars only cover part of the screen width at 2560x1440 (canvas scaling)

## 3. Spells
- [ ] `ParticleFX` native class (AddParticle, NumParticles, Get/SetParticleParams, RecomputeDeltas) + rendering
- [ ] `Gesture` spell-drawing recognition (CompareGesture, CompareGesturePoint)
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
| `Engine.cpp` | `HP1::ViewFovAngle` after PlayerCalcView (Hor+ FOV); trim `\|` input subcommands |
| `Collision/TopLevel/TraceTest.cpp`, `OverlapTest.cpp`, `CollisionSystem.cpp` | CT_Box trace/overlap/hash extents |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnMoveToward`, `HP1::PawnPhysicsTime`, `HP1::PawnPhysicsRotation` |
| `Packages/Engine/Actors/Pawn/UPlayerPawn.cpp` | `HP1::PawnPhysicsRotation` |
| `UE1GameDatabase.h`, `GameApp.cpp` | exe hashes, `--autolaunch` / `--logfile` |
| `SurrealWidgets/.../win32_display_window.cpp` | cursor recentering needs foreground focus (0004) |
