# Engine hooks

Flipendo uses [SurrealEngine](https://github.com/dpjudas/SurrealEngine) as a dependency: `engine/` is a git
submodule pinned to a SurrealEngine commit, and nothing is ever committed inside it. Our code lives in `kw/` (KnowWonder's
engine, shared by the HP games) and `hp1/` (HP1 only). SurrealEngine's files only get small hooks that call into
them, kept as patch files so a newer SurrealEngine rarely conflicts.

## The patch set

| Patch | What |
|---|---|
| `patches/0001-game-detection.patch` | HP1 `System/HP.exe` hashes (UK 1.1, EN retail SafeDisc, community No-CD); why a folder isn't a game |
| `patches/0002-launcher-flags.patch` | `--autolaunch`, `--logfile`, the flags the HP1 code reads |
| `patches/0003-cursor-focus.patch` | cursor recentering and raw input only while the game window has focus |
| `patches/0010-engine-fixes.patch` | fixes to SurrealEngine bugs that aren't specific to one game (VM, properties, ini, packages, crash reports) |
| `patches/0100-kw-core.patch` | build (`flipendo.cmake`), natives registration, engine loop (input, console, saves, view), missing data messages |
| `patches/0110-kw-physics.patch` | KnowWonder physics, pawn movement, collision |
| `patches/0120-kw-actors.patch` | KnowWonder actor tick, native actors, animation |
| `patches/0130-kw-render.patch` | KnowWonder rendering; HP1's menu canvas |
| `patches/0140-kw-audio.patch` | KnowWonder sound and music |

`patches/routes.txt` maps engine files to these patches (globs, first match wins) and documents the number ranges:
0001-0009 plumbing, 0010-0099 game-independent fixes, 0100-0199 HP1, 0200-0299 free for HP2. Patches split by file,
so a file has one owner.

`tools/build.sh` applies them to the submodule's working tree (`tools/apply_patches.sh`). Every changed line is
marked with a `flipendo:` comment, which SurrealEngine's zlib licence requires for altered source. Hooks into `kw/`
are gated behind `engine->LaunchInfo.IsKnowWonder()` (HP1 for now, HP2 later), HP1-only hooks (mods, menu canvas)
behind `IsHarryPotter1()`, so other UE1 games keep working. Fixes to SurrealEngine bugs that affect any game
(`0010-engine-fixes.patch`) aren't gated.

### Changing a hook

1. Edit the patched file under `engine/` (the patches are already applied after a build).
2. `tools/refresh_patches.sh` regenerates `patches/` from the working tree. `patches/routes.txt` decides which patch
   a file goes to; a newly touched file with no route stops the refresh until you add one. New source files belong in
   `kw/` or `hp1/`, not `engine/`.
3. Rebuild, then commit `patches/` (never `engine/` itself) and add the hook to the table below.

Until you refresh, `tools/build.sh` reports `CONFLICT` for the patches: the working tree has changes they don't
contain yet. Temporary debug edits must be reverted (`tools/apply_patches.sh --reset`) before refreshing.

### Moving to a newer SurrealEngine

```sh
tools/update_engine.sh --check   # new SurrealEngine commits + which patched files they touch
tools/update_engine.sh           # move engine/ to SurrealEngine master and re-apply patches/
tools/update_engine.sh <sha>     # or a specific commit
```

If a patch no longer applies, `apply_patches.sh` reports `CONFLICT`: redo that hook by hand in `engine/`, run
`tools/refresh_patches.sh`, rebuild, and commit `patches/` together with the new `engine` submodule pointer.

## Hooks by file

Paths are relative to `engine/SurrealEngine/` unless they start with `engine/`.

| File | Hook |
|---|---|
| `engine/CMakeLists.txt` | includes `flipendo.cmake` (`kw/kw.cmake`, `hp1/hp1.cmake`) |
| `Package/PackageManager.cpp` | `HP1::RegisterNatives()` after SurrealEngine natives |
| `Packages/Engine/Resources/Mesh/UAnimation.cpp` | `HP1::LoadAnimation` |
| `Packages/Engine/Actors/UActor_Animation.cpp` | `HP1::TickAnimation` |
| `Render/VisibleMesh.cpp` | `HP1::DrawSkeletalMesh` in `DrawSkeletalMesh` |
| `Packages/Engine/Actors/UActor.h`, `UActor.cpp` | `WorldLightRadius` scaled by `max(DrawScale, 1)` for KnowWonder (moved out of line) |
| `Light/LightmapBuilder.cpp` | BSP light maps: `KW::LightmapAmbient` (SetAmbientLight), `KW::LightmapIllumination` instead of `LightEffect::Run` (falloff, LightSource, LightRadiusInner), `KW::AddLightmapLight` (AddLightContribution: colour scale, 7-bit clamps, bDarkLight) |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetRenderBoundingBox` in `UpdateBspInfo` |
| `Packages/Engine/Actors/UActor_PhysMovingBrush.cpp` | `HP1::MoverPhysicsBegin/End` (Mover's shadowed PhysAlpha/PhysRate) |
| `Render/RenderSubsystem.cpp` | `HP1::OnFrameRendered` (`HP1_SHOTS` debug screenshots) |
| `Render/RenderCanvas.cpp`, `RenderSubsystem.h` | `HP1::CanvasUIScale` (float `uiscale`), `HP1::SetCanvasArea` (full-width HUD, 4:3 console/menus); `DrawClippedActor` relative to the canvas area |
| `Engine.cpp` | `HP1::ViewFovAngle` after PlayerCalcView (Hor+ FOV); trim `\|` input subcommands; `SET Input` takes the rest of the line (multi-word aliases; no alias unbinds); `getres` → `HP1::AvailableResolutions`; `HP1::MenuMousePosition` in `OnWindowMouseMove` |
| `Collision/TopLevel/TraceTest.cpp`, `OverlapTest.cpp`, `CollisionSystem.cpp` | CT_Box trace/overlap/hash extents |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnMoveToward`, `HP1::PawnPhysicsTime`, `HP1::PawnPhysicsRotation` |
| `Packages/Engine/Actors/Pawn/UPlayerPawn.cpp` | `HP1::PawnPhysicsRotation` |
| `UE1GameDatabase.h`, `GameApp.cpp` | exe hashes, `--autolaunch` / `--logfile` |
| `SurrealWidgets/.../win32_display_window.cpp` | cursor recentering and raw mouse/keyboard input need foreground focus (0004; raw input is RIDEV_INPUTSINK, so moving the mouse in another app turned the camera) |
| `Packages/Engine/Actors/UActor_Phys.cpp`, `UActor_PhysRolling.cpp` | `HP1::PhysicsChecksLeftWorld` (zone-0 FellOutOfWorld only while walking) |
| `Packages/Engine/Actors/UActor_PhysWalking.cpp` | player slides along actors it hits (no pushable decoration); `HP1::PawnMount` before the step up |
| `Packages/Engine/Actors/UActor_PhysFalling.cpp` | `HP1::PawnMount` on a wall hit |
| `Packages/Engine/Actors/UActor.cpp` | `HP1::TickNativeActor` (ParticleFX, Wind) in Tick, `HP1::ParticleFXDestroyed` in Destroy |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetParticleBoundingBox` for DT_Particles (8) |
| `Render/VisibleActor.cpp` | DT_Particles actors drawn in the translucent pass by `HP1::DrawParticleSystem` |
| `Engine.cpp` | `HP1::DebugCamera` after PlayerCalcView (`HP1_CAMERA`); `KW::ShowWindowInBackground` in OpenWindow (`HP1_BACKGROUND`); `HP1::TickMods` after the console tick; `HP1::ModsKeyDown` in OnWindowKeyDown |
| `Render/RenderCanvas.cpp` (PostRender) | `HP1::PostRenderMods` after the HUD and console/menus |
| `Native/NObject.cpp` | DynamicLoadObject resolves "Package.Group.Name" |
| `Packages/Engine/Actors/UActor.cpp` (Tick end) | `HP1::TickRootMotion` (bAnimMove root motion) |
| `Packages/Engine/Actors/UActor_PhysWalking.cpp`, `UActor_PhysRolling.cpp` | `HP1::PawnWalkOffLedge` / `HP1::StartFalling` (MayFall, ledge rule, auto-jump, Falling) |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnPollMoveToward`, `PawnPollStrafeFacing`, `PawnPollWaitForLanding`; WaitForLanding sets LatentFloat |
| `Packages/Engine/Actors/UActor_PhysMovingBrush.cpp` | KeyFrameReached instead of InterpolateEnd(None) |
| `Engine.cpp` | save games ([re/engine/savegames.md](re/engine/savegames.md)): `SaveGame` saves at once (not at the end of the frame); `open saveN.usa` → `HP1::SaveGameLoadURL` (`?load=N`); `HP1::LevelInfoLoaded` in LoadMap (empty LevelEnterText = URL map); `KW::ViewportCommand` before the exec functions (`Snap`) |
| `Native/NPlayerPawn.cpp` | ClientTravel raises PreClientTravel |
| `Engine.cpp` (after the level tick), `Render/RenderSubsystem.cpp` | ViewFlash event; `HP1::ViewFlashParams` for the screen flash |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp` | music plays despite `UseDigitalMusic=False` (HP1's shipped ini; its mp2 songs play in the original) |
| `Packages/Engine/Actors/UActor_PhysFlying.cpp` | flying keeps Velocity.z (HP1 physFlying 0x103F13A0) |
| `Engine.cpp` | `SET Input` matches the class name ignoring case and saves the ini files at once; input alias names are looked up ignoring case (HP1's options menu writes them upper-cased) |
| `Package/IniFile.cpp` | indexed keys (`Aliases[0]=`) are compared literally when updating a file, like the loader stores them (splitting the index threw, then blanked them) |
| `Packages/Core/UObject.cpp` | `ScriptArray` copy/move constructors keep `Type` (copying an array, e.g. `int(Gesture.Points)`, crashed); `GotoState` clears disabled events (Core.dll's GotoState rebuilds the probe mask, so `Disable('Tick')` only lasts until the next state change; HP1's spell lesson hung on the second judging round) |
| `VM/Bytecode.h` | `FindLabelIndex` on a state with no statements returns -1 |
| `VM/Frame.cpp` | `goto` to a label that doesn't exist logs "GotoLabel (x): Label not found" and stops the state code (UE1's UObject::GotoLabel) instead of a fatal error; HP1's `gargoyle.lookaround` does it (Lev4_Sneak, Lev3_Lumos) |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `TurnToward` on a pawn whose state frame has no code does nothing (UE1 never polls it) |
| `Render/VisibleFrame.cpp` | HP1 translucents are sorted back to front (a modulated sprite behind a particle system darkened it) |
| `Packages/Engine/Resources/Textures/UIceTexture.cpp` | `HP1::UpdateIceTexture` (Fire.dll IceTexture) in `UpdateFrame` |
| `UI/ErrorWindow/ErrorWindow.cpp` | the crash reporter also writes the exception and symbolized call stack to `<dump>.txt` |
| `Packages/Engine/Actors/UActor_Phys.cpp` | an InterpolationManager runs `HP1::InterpolationManagerPhysics` instead of the physics modes |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` | `ModifySoundHP1` / `StopSoundHP1` (Galaxy.dll's slot + sound match) |
| `Packages/Core/Properties/UStructProperty.cpp` | struct members that are fixed arrays load/save every element |
| `Packages/Engine/Actors/UActor_PhysTrailer.cpp` | `HP1::PhysTrailer` (AnimBone attachment, HP1's rotation rules) |
| `Render/VisibleMesh.cpp` | weapon on a skeletal pawn: `HP1::PawnWeaponFrame` (WeaponLoc/WeaponRot) + `Begin/EndWeaponDraw` around the weapon draw |
| `Render/RenderCanvas.cpp` | RenderOverlays only without bBehindView, on the ViewTarget |
| `Package/PackageManager.cpp/.h`, `Package/Package.cpp` | missing data messages: a missing package names who imports it and every Paths folder searched; missing maps list the map folders; Paths folders that don't exist and imports a package lacks are logged ([troubleshooting](troubleshooting.md)) |
| `Native/NObject.cpp` | DynamicLoadObject failures log why (missing package with the searched folders, or missing object) |
| `UE1GameDatabase.cpp/.h`, `GameFolder.cpp/.h`, `GameApp.cpp` | a game folder given on the command line that isn't recognised says why (no such folder, the System folder itself, no known exe, unknown exe SHA-1) |
