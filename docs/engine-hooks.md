# Engine hooks

Flipendo uses [SurrealEngine](https://github.com/dpjudas/SurrealEngine) as a dependency: `src/engine/` is a git
submodule pinned to a SurrealEngine commit, and nothing is ever committed inside it. Our code lives in `src/knowwonder/` (KnowWonder's
engine, shared by the HP games) and `src/hp1/` (HP1 only). SurrealEngine's files only get small hooks that call into
them, kept as patch files so a newer SurrealEngine rarely conflicts.

## The patch set

| Patch | What |
|---|---|
| `src/surreal-patches/0001-game-detection.patch` | HP1 `System/HP.exe` hashes (UK 1.1, EN retail SafeDisc, community No-CD, Russian retail and No-CD), also under the long exe names some releases use; why a folder isn't a game |
| `src/surreal-patches/0002-launcher-flags.patch` | `--autolaunch`, `--logfile`, the flags the HP1 code reads; `--list-mods` prints `HP1::ModsList()` and exits |
| `src/surreal-patches/0003-cursor-focus.patch` | cursor recentering and raw input only while the game window has focus; window icons the right way up; macOS: `ShowFullscreen` shows the window first and only toggles when it isn't fullscreen yet, and mouse buttons go into the key state `GetKeyState` reads (HP2's `GetCurrentKeyState`; the middle button gets its own handlers) |
| `src/surreal-patches/0010-engine-fixes.patch` | fixes to SurrealEngine bugs that aren't specific to one game (VM, properties, ini (UTF-16 files), packages, GetLanguage, fonts, crash reports, audio device) |
| `src/surreal-patches/0100-knowwonder-core.patch` | build (`src/flipendo.cmake`), natives registration, engine loop (input, console, saves, view), missing data messages |
| `src/surreal-patches/0110-knowwonder-physics.patch` | KnowWonder physics, pawn movement, collision |
| `src/surreal-patches/0120-knowwonder-actors.patch` | KnowWonder actor tick, native actors, animation |
| `src/surreal-patches/0130-knowwonder-render.patch` | KnowWonder rendering; HP1's menu canvas |
| `src/surreal-patches/0140-knowwonder-audio.patch` | KnowWonder sound and music |
| `src/surreal-patches/0200-hp2.patch` | Harry Potter 2: its sound objects' extra fields and XA sounds, no IpDrv package, actors' tick parents |

`src/surreal-patches/routes.txt` maps engine files to these patches (globs, first match wins) and documents the number ranges:
0001-0009 plumbing, 0010-0099 game-independent fixes, 0100-0199 HP1, 0200-0299 free for HP2. Patches split by file,
so a file has one owner.

`tools/build.sh` applies them to the submodule's working tree (`tools/apply_patches.sh`). Each patch starts with a
`flipendo: ...` header line (from `src/surreal-patches/routes.txt`), which marks the altered source as SurrealEngine's zlib licence
requires; the changed lines themselves carry no marker. Hooks into `src/knowwonder/`
are gated behind `engine->LaunchInfo.IsKnowWonder()` (HP1 and HP2), HP1-only hooks (mods, menu canvas)
behind `IsHarryPotter1()`, HP2-only ones behind `IsHarryPotter2()`, so other UE1 games keep working. Fixes to SurrealEngine bugs that affect any game
(`0010-engine-fixes.patch`) aren't gated.

### Changing a hook

1. Edit the patched file under `src/engine/` (the patches are already applied after a build).
2. Regenerate `src/surreal-patches/` from the working tree's diff. `src/surreal-patches/routes.txt` decides which patch
   a file goes to; a newly touched file needs a route there first. New source files belong in
   `src/knowwonder/` or `src/hp1/`, not `src/engine/`.
3. Rebuild, then commit `src/surreal-patches/` (never `src/engine/` itself) and add the hook to the table below.

Until you refresh, `tools/build.sh` reports `CONFLICT` for the patches: the working tree has changes they don't
contain yet. Temporary debug edits must be reverted (`tools/apply_patches.sh --reset`) before refreshing.

### Moving to a newer SurrealEngine

Fetch SurrealEngine in `src/engine/`, check out the new commit and run `tools/apply_patches.sh`. If a patch no longer
applies, it reports `CONFLICT`: redo that hook by hand in `src/engine/`, regenerate `src/surreal-patches/`, rebuild,
and commit `src/surreal-patches/` together with the new `engine` submodule pointer.

## Hooks by file

Paths are relative to `src/engine/SurrealEngine/` unless they start with `src/engine/`.

| File | Hook |
|---|---|
| `src/engine/CMakeLists.txt` | includes `src/flipendo.cmake` (`src/knowwonder/knowwonder.cmake`, `src/hp1/hp1.cmake`) |
| `Engine.cpp` (OpenWindow) | window icon `KW::GameIcons`: the game's own `System/<exe>.ico` from the player's folder (HP1 `hp.ico`, grey background made transparent), else SurrealEngine's |
| `Package/PackageManager.cpp` | `HP1::RegisterNatives()` after SurrealEngine natives |
| `Package/PackageManager.cpp` (ScanFolder) | `IsKnowWonder()`: the plain package file wins over a language file next to it (`AllDialog.uax` over `AllDialog.int_uax`, [packages](re/engine/packages.md#language-files-appfindpackagefile-hp1-core-0x10153f40)); also fixes the doubled folder in a language file's path (any game) |
| `Packages/Engine/Resources/Mesh/UAnimation.cpp` | `HP1::LoadAnimation` |
| `Packages/Engine/Actors/UActor_Animation.cpp` | `HP1::TickAnimation` |
| `Render/VisibleMesh.cpp` | `HP1::DrawSkeletalMesh` in `DrawSkeletalMesh` |
| `Packages/Engine/Actors/UActor.h`, `UActor.cpp` | `WorldLightRadius` scaled by `max(DrawScale, 1)` for KnowWonder (moved out of line) |
| `Packages/Engine/Actors/UActor_Base.cpp` | `InitBase` applies AttachTag to the child, stopping at the first matching parent; with KnowWonder physics, initial support uses `KW::FindBase` rather than the first overlap ([physics](re/engine/physics.md#initial-support-must-block-the-actor)) |
| `Light/LightmapBuilder.cpp` | BSP light maps: `KW::LightmapAmbient` (SetAmbientLight), `KW::LightmapIllumination` instead of `LightEffect::Run` (falloff, LightSource, LightRadiusInner), `KW::AddLightmapLight` (AddLightContribution: colour scale, 7-bit clamps, bDarkLight); ungated fix: `CalcWorldLocations` puts lumels on the surface plane (inverse of the U/V/normal frame) |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetRenderBoundingBox` in `UpdateBspInfo` |
| `Packages/Engine/Actors/UActor_PhysMovingBrush.cpp` | `KW::PhysMovingBrush` in place of upstream's mover loop: the Mover's own (shadowing) PhysAlpha/PhysRate, KeyFrameReached instead of InterpolateEnd(None), a blocked move stops the interpolation, bCollideWorld movers (GridMover) collide with the world by their bounding box and fall with the zone's gravity |
| `Packages/Engine/Actors/UActor_Phys.cpp` (TickPhysics, CheckLocation) | KnowWonder's own physics: `KW::PhysicsTick` (one `performPhysics` per tick, `src/knowwonder/KWPhysics.cpp`) in place of the 0.02 s steps; spawn placement with `KW::SpawnFindSpot` (FindSpot); off with `FLIPENDO_SE_PHYSICS=1` |
| `Packages/Engine/Actors/UActor.cpp` (Tick) | with KnowWonder's physics the Timer runs before the physics (HP1's AActor::Tick order) |
| `Packages/Engine/Actors/UActor.cpp`, `UActor.h` (Tick), `Pawn/UPawn_Tick.cpp`, `Pawn/UPawn.h` | KnowWonder polls a pawn's latent action (`UPawn::PollLatentAction`, split out of `UPawn::Tick`) after the script Tick event, as HP1's ProcessState does |
| `Collision/TopLevel/CollisionSystem.cpp/.h` | the hash files KnowWonder actors by `KW::ActorWorldCollisionBox` (CollisionWidth offset, CT_Box); `ActorsInBox` for KnowWonder's actor checks |
| `Engine.cpp` (Run, Exec) | HP2: `HP2::TickQueuedSave` after the level ticks (a save the player queued with `bQueuedToSaveGame`; it ends the frame), `HP2::SaveCommand` for `LOADGAME <n>` (`src/hp2/HP2Save.cpp`) |
| `Render/RenderSubsystem.cpp` | `KW::OnFrameRendered` after `Unlock` (`HP1_SHOTS` debug screenshots; `ReadPixels` needs the finished frame) |
| `Render/RenderCanvas.cpp`, `RenderSubsystem.h` | `KW::CanvasUIScale` (float `uiscale`), `KW::SetCanvasArea` (full-width HUD, 4:3 console/menus; HP1 and HP2); `DrawClippedActor` relative to the canvas area |
| `Render/RenderCanvas.cpp` (DrawText) | centred text (Canvas.bCenter) is centred on CurX with `KW::CenteredTextLineX`, as KnowWonder's `UCanvas::WrappedPrint` (HP1 and HP2; [re/engine/fonts.md](re/engine/fonts.md#centred-text-canvasbcenter)) |
| `Render/RenderCanvas.cpp` (DrawText, DrawTextClipped), `Packages/Engine/Resources/UFont.*` | fonts made by `KW::CreateNativeFont` (`UFont::SetNativeGlyphs`, `IsNative`, `src/knowwonder/KWNativeFont.cpp`) draw premultiplied in the DrawColor whatever the style, its alpha taken as 1, like HP1's GDI text |
| `Engine.cpp` | `HP1::ViewFovAngle` after PlayerCalcView (Hor+ FOV); trim `\|` input subcommands; `SET Input` takes the rest of the line (multi-word aliases; no alias unbinds); `getres` → `KW::AvailableResolutions`; `isfullscreen` (WinDrv's viewport command, any game: True / False); `KW::MenuMousePosition` in `OnWindowMouseMove` |
| `Collision/TopLevel/TraceTest.cpp`, `OverlapTest.cpp`, `CollisionSystem.cpp` | CT_Box trace/overlap/hash extents |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnMoveToward`, `HP1::PawnPhysicsTime`, `HP1::PawnPhysicsRotation` |
| `Packages/Engine/Actors/Pawn/UPlayerPawn.cpp` | `HP1::PawnPhysicsRotation` |
| `UE1GameDatabase.h`, `GameApp.cpp` | exe hashes, `--autolaunch` / `--logfile`; `--level=<map>` loads the map as `--url` does; `--list-mods` (`HP1::ModsList`) |
| `SurrealWidgets/.../win32_display_window.cpp` | cursor recentering and raw mouse/keyboard input need foreground focus (0004; raw input is RIDEV_INPUTSINK, so moving the mouse in another app turned the camera); the icon bitmap is top-down (it was drawn upside down) |
| `Packages/Engine/Actors/UActor_Phys.cpp`, `UActor_PhysRolling.cpp` | `HP1::PhysicsChecksLeftWorld` (zone-0 FellOutOfWorld only while walking) |
| `Packages/Engine/Actors/UActor_PhysWalking.cpp` | player slides along actors it hits (no pushable decoration); `HP1::PawnMount` before the step up |
| `Packages/Engine/Actors/UActor_PhysFalling.cpp` | `HP1::PawnMount` on a wall hit |
| `Packages/Engine/Actors/UActor.cpp` | `HP1::TickNativeActor` (ParticleFX, Wind) in Tick, `HP1::ParticleFXDestroyed` in Destroy, `KW::ActorConstructed` in Spawn (what AParticleFX's constructor resets, after the class defaults are copied) |
| `Packages/Engine/Actors/UActor.cpp` (Destroy) | KnowWonder: an actor in a state gets `EndState` when it is destroyed (HP1's ULevel::DestroyActor; `Target` draws the spell symbol on a creature from it) |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` (Tick) | with KnowWonder's physics `UPawn::Tick` leaves MoveTimer and AvgPhysicsTime to `KW::PerformPhysics` (counted twice before) |
| `Packages/Engine/Actors/UActor_Render.cpp` | `HP1::GetParticleBoundingBox` for DT_Particles (8) |
| `Render/VisibleActor.cpp` | DT_Particles actors drawn in the translucent pass by `HP1::DrawParticleSystem` |
| `Engine.cpp` | `HP1::DebugCamera` after PlayerCalcView (`HP1_CAMERA`); `KW::ShowWindowInBackground` in OpenWindow (`HP1_BACKGROUND`); `HP1::TickMods` after the console tick (`IsKnowWonder()`: HP2 runs only the frame limit); `HP1::ModsKeyDown` in OnWindowKeyDown |
| `Engine.cpp` (Run) | `HP1::ModsTimeScale` multiplies the frame's level time (`src/hp1/mods/FastForward.cpp`: Shift held = 2.5x; `IsKnowWonder()`, HP1 and HP2) |
| `Render/RenderCanvas.cpp` (PostRender) | `HP1::PostRenderMods` after the HUD and console/menus |
| `RenderDevice/Vulkan/UploadManager.cpp` (UploadTexture) | `HP1::LoadTextureOverride` (HP1 and HP2, gated `IsKnowWonder()`): a `Textures/<package>/<group>/<name>.dds` replacement image is uploaded instead of the texture's mipmaps (`src/hp1/mods/TextureOverrides.cpp`); UVs keep the texture's own USize/VSize. Vulkan only |
| `Native/NObject.cpp` | DynamicLoadObject resolves "Package.Group.Name" |
| `Packages/Engine/Actors/UActor.cpp` (Tick end) | `HP1::TickRootMotion` (bAnimMove root motion) |
| `Packages/Engine/Actors/UActor_PhysWalking.cpp`, `UActor_PhysRolling.cpp` | `HP1::PawnWalkOffLedge` / `HP1::StartFalling` (MayFall, ledge rule, auto-jump, Falling) |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `HP1::PawnPollMoveToward`, `PawnPollStrafeFacing`, `PawnPollWaitForLanding`; WaitForLanding sets LatentFloat |
| `Engine.cpp` | save games ([re/engine/savegames.md](re/engine/savegames.md)): `SaveGame` → `KW::SaveGame` at once (not at the end of the frame): LevelAction saving, the blue progress frame (`PaintProgress`), the save; `open saveN.usa` → `HP1::SaveGameLoadURL` (`?load=N`); `KW::SaveGameLoaded` after LoadFromSaveFile (BSP leaves found again); `HP1::LevelInfoLoaded` in LoadMap (empty LevelEnterText = URL map); `KW::ViewportCommand` before the exec functions (`Snap`) |
| `Engine.cpp` (ConsoleCommand), `Packages/Engine/Subsystems/USurrealAudioDevice.cpp` (Update) | `IsHarryPotter2()`: ALAudio's `MUTESOUNDS` / `UNMUTESOUNDS` (`HP2::AudioCommand`) hold the sound volume at 0 while a skipped cutscene fast-forwards; `SETVOLUMES SOUND=` / `MUSIC=` set the volumes |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` (`VolumeToString`, `VolumeFromString`), `Engine.cpp` (LoadEngineSettings) | `IsHarryPotter2()`: ini volumes as ALAudio's floats 0..1 (`HP2::VolumeFromIni` / `VolumeToIni`); the first run reads `HP2::AudioIniSection` / `RenderIniSection` |
| `Engine.cpp` (LoginPlayer) | `HP1::LevelStartPlayer` before Possess: `--level` sets `HPConsole.bInHubFlow` and Harry's travel state (`src/hp1/HP1LevelStart.cpp`) |
| `Engine.cpp` (LoadMap) | `IsKnowWonder()`: PreBeginPlay..SetInitialState run on the actors the level was loaded with only (SurrealEngine runs them on spawned actors too, for Deus Ex); `KW::SetStartupZones` after BeginPlay: every loaded actor's zone and BSP leaf, as HP1's LoadMap does (a map saves movers with leaf -1, which left bDynamicLightMover movers unlit) |
| `Native/NPlayerPawn.cpp` | ClientTravel raises PreClientTravel |
| `Engine.cpp` (after the level tick), `Render/RenderSubsystem.cpp` | ViewFlash event; `HP1::ViewFlashParams` for the screen flash, from the viewport's player (not the camera actor the view goes through) |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp` | `Update`: `KW::UpdateMusic` in place of `UpdateMusic` (Galaxy's song changes: fades, no restart of the song already playing, looping unless bDontLoopSong, bSongFinished, PercentMusicVolume; music plays despite HP1's `UseDigitalMusic=False`) and its result as the music volume; `StopSounds` (level change, save load) leaves the music and the viewport alone, as Galaxy keeps the music playing |
| `Packages/Engine/Actors/UActor_PhysFlying.cpp` | flying keeps Velocity.z (HP1 physFlying 0x103F13A0) |
| `Engine.cpp/.h` | the main loop's PlayerCalcView moved into `Engine::CalcView`, which `KW::SaveGame`'s progress frame also calls (UGameEngine::Draw [HP1 0x1039FA40] calls PlayerCalcView for every frame; the level-start save, run from HPConsole.Tick before the loop's view of the new level, was drawn from the last level's camera spot) |
| `Engine.cpp` | `SET Input` matches the class name ignoring case and saves the ini files at once; input alias names are looked up ignoring case (HP1's options menu writes them upper-cased) |
| `Package/IniFile.cpp` | indexed keys (`Aliases[0]=`) are compared literally when updating a file, like the loader stores them (splitting the index threw, then blanked them) |
| `Packages/Core/UObject.cpp` | `ScriptArray` copy/move constructors keep `Type` (copying an array, e.g. `int(Gesture.Points)`, crashed); `GotoState` clears disabled events (Core.dll's GotoState rebuilds the probe mask, so `Disable('Tick')` only lasts until the next state change; HP1's spell lesson hung on the second judging round); `Load` reads a set bit of the saved probe mask as enabled and `Save` writes it so (a mask of 0, from older saves, disables nothing) (it disabled every probe of a map actor until its first GotoState; Lev2_RemChase's intro cutscene ignored its trigger) |
| `VM/Bytecode.h` | `FindLabelIndex` on a state with no statements returns -1 |
| `VM/Frame.cpp` (Call) | Any game: a singular function is skipped while a singular function runs on the same object, as UE1's UObject::ProcessInternal; HP2's Aragog fight ([re/engine/scripting.md](re/engine/scripting.md#script-recursion-limit)) |
| `VM/Frame.cpp` (Run) | Any game: more than 250 nested script calls is the script error "Infinite script recursion (250 calls) detected", as UE1's UObject::ProcessInternal, instead of overflowing the native stack; HP2's `harry.Summon` calls itself |
| `VM/Frame.cpp` | `goto` to a label that doesn't exist logs "GotoLabel (x): Label not found" and stops the state code (UE1's UObject::GotoLabel) instead of a fatal error; HP1's `gargoyle.lookaround` does it (Lev4_Sneak, Lev3_Lumos) |
| `VM/Frame.cpp` | Any game: "Accessed None" (also "when calling") is logged with its callstack once per script expression, not on every hit: since SurrealEngine runs statements on `Frame::RunExpr` it reports None inside sub-expressions too, and HP1's `BaseCam.CheckPosition` reads `PossibleVictim.Name` every frame when its trace hits nothing |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `TurnToward` on a pawn whose state frame has no code does nothing (UE1 never polls it) |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `KW::PawnStartMove` when MoveTo / MoveToward / StrafeTo / StrafeFacing start (their first step); MoveToward on a pawn: MoveTimer 1.2; StrafeFacing's DesiredSpeed and Focus |
| `Packages/Engine/Resources/Level/UModel.cpp` | ungated: `FindRegion` takes the leaf on the point's side of the node, like the zone (it took the other side's, often -1) |
| `Render/VisibleBrush.cpp` | ungated: mover polygons are one-sided like BSP surfaces (drawn only from in front, unless PF_TwoSided); the portcullis's plank back face filled its masked gate. Their texture's PolyFlags count like the polygon's, as for BSP surfaces (a masked bar drew its holes black) |
| `Light/LightSystem.h`, `LightSystem_Light.cpp` | `GetMoverLightmap`: a bDynamicLightMover is lit by `KW::DynamicMoverLights` (the lights permeating its leaf, in front of the surface) plus nearby dynamic lights, instead of the lights that see its pivot ([re/engine/lighting.md](re/engine/lighting.md)) |
| `Native/NActor.cpp`, `Native/NPawn.cpp`, `Packages/Engine/Actors/UActor.h`, `UActor.cpp` | ungated: `Sleep` keeps its remaining time in `Actor.LatentFloat` (saved with the actor) instead of a C++ field, done once under half a tick; `StopWaiting` ends only a Sleep (LatentFloat -1). A Sleep now survives save/load ([re/engine/savegames.md](re/engine/savegames.md)) |
| `Native/NActor.cpp`, `VM/Iterator.cpp` | ungated: `VisibleActors` without a radius (or with 0) has no distance limit, as HP1's execVisibleActors (0x1040E610); SurrealEngine took the caller's CollisionRadius, so `baseHarry.ExtendTarget` found nothing and spells locked on only when the 512-unit trace hit (doxies). [re/hp1/spells.md](re/hp1/spells.md) |
| `Render/VisibleFrame.cpp` | `KW::DrawCoronas` instead of SurrealEngine's corona list: HP1 gathers coronas from the lights permeating the viewport actor's leaf, with a fade ([re/engine/rendering.md](re/engine/rendering.md)) |
| `Render/VisibleActor.cpp` | HP1: visible bCorona actors aren't collected (KW::DrawCoronas gathers them) |
| `Render/VisibleFrame.cpp` | Any game: a BSP surface's `Front` flag says whether the camera is in front of the node's own plane, so it takes the front zone's ambient light and texture panning (SurrealEngine had it inverted: walls took zone 0's) ([re/engine/lighting.md](re/engine/lighting.md)) |
| `Render/VisibleFrame.cpp` | HP1 translucents are sorted back to front (a modulated sprite behind a particle system darkened it) |
| `Packages/Engine/Resources/Textures/UIceTexture.cpp` | `HP1::UpdateIceTexture` (Fire.dll IceTexture) in `UpdateFrame` |
| `UI/ErrorWindow/ErrorWindow.cpp` | the crash reporter also writes the exception and symbolized call stack to `<dump>.txt` |
| `Packages/Engine/Actors/UActor_Phys.cpp` | an InterpolationManager runs `HP1::InterpolationManagerPhysics` instead of the physics modes |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` | HP2 (ALAudio.dll): `PlaySound`'s `no3D` / `loop` (a sound played at the camera, the channel looped with `SetLooping` for KnowWonder games) and `StopSoundHP2` (every matching sound of the actor, faded out linearly over FadeOutTime in `UpdateSounds`) ([re/hp2/engine.md](re/hp2/engine.md#audio)) |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` | `IsHarryPotter2()`: `InitDevice` creates at least `HP2::AudioChannels` device channels and calls `HP2::InitAudio` (inverse falloff) instead of the linear falloff, `SetViewport` takes `HP2::AudioChannels`, `Update` calls `HP2::UpdateAudio` (pause, ambience, listener velocity) instead of `StartAmbience` / `UpdateAmbience`, `UpdateSounds` skips paused sounds and takes each one's volume, pitch and velocity from `HP2::UpdateSound` and its priority from `HP2::SoundPriority`, `PlaySound` clamps volume and pitch and picks the channel with `HP2::ChooseSoundChannel`; `PlayingSound` keeps the flags and the pause; `GetCurrentViewport`, `GetPlayingSounds`, `StopSoundAt`, `SetSoundPaused`, `SetSoundVelocity`, `SetListenerVelocity` for `src/hp2/HP2Audio.cpp` and the `Get/SetMusicVolume`, `Get/SetSoundVolume` accessors for its front end (the device and the volumes are private) ([re/hp2/engine.md](re/hp2/engine.md#audio)) |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` | `IsHarryPotter2()`: `UpdateLipSync` / `ClearLipSync` call `HP2::UpdateLipSync` / `StopLipSync` (the speaker's mouth_root channel driven by the sound's amplitude); `PlayingSound` keeps the channel and the start time ([re/hp2/engine.md](re/hp2/engine.md#audio)) |
| `Packages/Engine/Subsystems/USurrealAudioDevice.cpp/.h` | `ModifySoundHP1` / `StopSoundHP1` (Galaxy.dll's slot + sound match, volume stored as given); `UpdateSounds` turns on the device's linear falloff (HP1) with the sound volume as its maximum and `PlaySound` keeps volume and radius as given (Galaxy's; `KW::PlaySound` already made a zero radius 1600) |
| `Packages/Core/Properties/UStructProperty.cpp` | struct members that are fixed arrays load/save every element |
| `Packages/Engine/Actors/UActor_PhysTrailer.cpp` | `HP1::PhysTrailer` (AnimBone attachment, HP1's rotation rules) |
| `Packages/Engine/Actors/UActor_Phys.cpp` (TryMove) | `KW::IsBlockedBy` decides blocked (Bump) vs touched (movers stop bCollideWorld actors: a spell bumps a GridMover) |
| `Packages/Engine/Actors/UActor_PhysProjectile.cpp` | HitWall also for blocking actors (HP1 physProjectile), so a spell explodes on the mover it bumped |
| `Audio/AudioDevice.h`, `Audio/OpenAL/*` | `SetLooping(channel, loop)` (default nothing): `AL_LOOPING` on the whole sound, or between its loop points as before |
| `Audio/AudioDevice.h`, `Audio/OpenAL/*` | `SetLinearFalloff(enable, maxVolume)` (off unless a game asks): volume `min(Volume * (1 - d/R), maxVolume)`, OpenAL reference distance 0, rolloff 1, max gain `maxVolume` (the sound volume) |
| `VM/Frame.cpp` (Run) | `IsKnowWonder()`: a function may run 100,000,000 statements before the runaway break (500,000 otherwise); KnowWonder's Core.dll only counts Jump tokens, 10,000,000, and logs ([re/hp2/engine.md](re/hp2/engine.md#special-pause)) |
| `Engine.cpp` (InputEvent), `Packages/Engine/Resources/Level/ULevel.cpp` (TickActor) | `IsHarryPotter2()`: `HP2::InputEvent` (Delete in debug mode toggles the special pause) and `HP2::SpecialPauseTick` before an actor ticks ([re/hp2/engine.md](re/hp2/engine.md#special-pause)) |
| `Audio/AudioDevice.h`, `Audio/OpenAL/*` | `SetInverseFalloff(rolloff, doppler, speedOfSound)`, `SetSoundVelocity`, `SetListenerVelocity`, `SetPaused` (default nothing): OpenAL's inverse distance clamped model with the radius as reference distance and no maximum, doppler from the given velocities, a paused source still counts as playing (HP2's ALAudio) |
| `Audio/OpenAL/OpenALAudioDevice.cpp` | ungated fix: the music thread fills the rest of a buffer with silence when the stream ends (it replayed a stale chunk, a stuck ~1 s loop) |
| `Render/VisibleMesh.cpp` | ungated fix: the mesh's bounding sphere handed to `InitVertexLight` (which fogballs fog it) is its centre through `ObjectToWorld` and its radius `.w` (SurrealEngine's `(xyz, 1.0f)` was a comma expression, the radius `.z`) |
| `Packages/Engine/Subsystems/USurrealAudioDevice.h` | ungated fix: includes `UActor.h` for `ESoundSlot` (an enum without a fixed type can't be declared ahead; GCC and Clang refuse it) |
| `Packages/Engine/Actors/UActor_Touch.cpp` | `KW::BeginTouch` in `Touch` (one side at a time, the trigger is told even when the spell destroyed itself) |
| `Render/VisibleMesh.cpp` | weapon on a skeletal pawn: `HP1::PawnWeaponFrame` (WeaponLoc/WeaponRot) + `Begin/EndWeaponDraw` around the weapon draw |
| `Render/RenderCanvas.cpp` | RenderOverlays only without bBehindView, on the ViewTarget |
| `Package/PackageManager.cpp/.h`, `Package/Package.cpp` | missing data messages: a missing package names who imports it and every Paths folder searched; missing maps list the map folders; Paths folders that don't exist and imports a package lacks are logged ([troubleshooting](troubleshooting.md)) |
| `Native/NObject.cpp` | DynamicLoadObject failures log why (missing package with the searched folders, or missing object) |
| `UE1GameDatabase.cpp/.h`, `GameFolder.cpp/.h`, `GameApp.cpp` | a game folder given on the command line that isn't recognised says why (no such folder, the System folder itself, no known exe, unknown exe SHA-1) |
| `Render/VisibleFrame.cpp` | `KW::UpdateActorShadows` before the BSP walk: Shadow.Update for actors whose shadow area is on screen (URender::SetupDynamics, [re/engine/rendering.md](re/engine/rendering.md)) |
| `Render/VisibleMesh.cpp` | `KW::MeshOpacity` / `ApplyOpacityFlags` / `ApplyOpacityVertices` in `DrawMesh` and `DrawLodMeshFace`: Actor.Opacity < 1 alpha blends the mesh |
| `RenderDevice/RenderDevice.h`, `Vulkan/`, `D3D11/`, `OpenGL/` | `GouraudVertex::Alpha` (default 1), passed to the vertex colour's alpha by `DrawGouraudPolygon` (premultiplied with PF_Highlighted) |
| `Engine.cpp` (LoadMap, LoadFromSaveFile), `Render/RenderSubsystem.h`, `RenderCanvas.cpp` | the loading screen: `KW::LoadMapFadeOut` at the start (fade to black over Console.FadeoutTime), `KW::LoadMapLevelInfo` once LevelInfo is loaded; `RenderSubsystem::DrawLevelInfo` draws Console.DrawLevelInfo on a black frame |
| `Packages/Engine/Actors/Pawn/UPawn_Tick.cpp` | `KW::PawnRotateToward` in `TickRotateTo` (100-unit yaw tolerance); `KW::PawnStartTurnTo` (HP2: Focus flattened while walking) and `KW::PawnTurnStep` when TurnTo / TurnToward start, `KW::PawnTurnStep` in their polls |
| `GameFolder.h` | `IsKnowWonder()` is HP1 or HP2 |
| `Render/VisibleFrame.cpp` (ProcessNodeSurface) | a one-sided BSP surface seen from behind is neither drawn nor an occluder, as in UE1 (any game; [rendering](re/engine/rendering.md#one-sided-bsp-surfaces-seen-from-behind)) |
| `Packages/Engine/Resources/Level/ULevel.cpp` (TickActor), `Packages/Engine/Actors/UActor.cpp` (Destroy) | `IsHarryPotter2()`: an actor's `TickParent`, `TickParent2` and then its owner (`HP2::TickBefore`) tick before it, instead of only the owner; a destroyed actor is cleared from every `TickParent` / `TickParent2` (`HP2::ClearTickParents`) ([tick order](re/hp2/engine.md#tick-order)) |
| `Engine.cpp` (LoginPlayer) | `IsHarryPotter2()`: `HP2::ScreenActorsByGameState` once the player is in, except on Entry ([GameState](re/hp2/engine.md#gamestate-actors-screened-by-the-story-state)) |
| `Render/VisibleFrame.cpp` (Process), `Render/VisibleNode.cpp` (Draw), `RenderDevice/RenderDevice.h` (`SurfaceInfo::Opacity`), the three devices' `DrawComplexSurface` | `IsHarryPotter2()`: `HP2::MarkLumosSurfaces(ViewZone)` per frame, `HP2::LumosSurface` per BSP surface (skip it, or the flags and opacity to draw it with); the devices take `Opacity` as the surface's vertex colour and alpha ([Lumos surfaces](re/hp2/engine.md#lumos-surfaces)) |
| `Engine.cpp` (Run, LoadMap) | `IsHarryPotter2()`: `HP2::LoadPersistentActorCache` once the save folder is known (after the front end); `HP2::LoadPersistentActors(url.Map)` in LoadMap after the actors are linked, before the GameInfo spawns, except on Entry ([hub persistence](re/hp2/engine.md#persistent-actors-across-hub-travel)) |
| `Packages/Engine/Actors/UActor.cpp` (Spawn) | KnowWonder: a spawned actor's name skips names the level's actors already have (HP2's persistent actors are matched by name) |
| `Engine.cpp` (Run), `Package/PackageManager.h` (`SetSaveFolderPath`) | `IsHarryPotter2()`: without a URL, `HP2::RunFrontEnd` (Game.exe's splashes and menu) before the window opens: it sets the URL and the slot's save folder, or ends the run on Exit; after the first map `HP2::LoadGameSave` travels to the slot's save (Load Game, -LOADGAME) ([startup](re/hp2/startup.md)) |
| `VM/Bytecode.h`, `VM/Bytecode.cpp`, `Packages/Core/UStruct.cpp` | `IsHarryPotter2()`: HP2's bytecode, `HP2::ToStockToken` and DebugInfo records skipped ([scripting](re/engine/scripting.md#how-flipendo-reads-it)); HP1's DynArrayCount token for HP2 too |
| `Packages/Engine/Resources/USound.cpp`, `USound.h` | `IsHarryPotter2()`: HP2's extra sound fields in Load/Save, XA sounds through `HP2::DecodeXA` ([sound](re/engine/sound.md#hp2s-sound-objects)) |
| `PropertyOffsets.cpp`, `Package/PackageManager.h` | `IsHarryPotter2()`: no IpDrv offsets (HP2 ships without IpDrv) |
| `Native/NObject.cpp`, `Native/NActor.cpp` | Object.GetLanguage and Actor's HP1 natives (PlayAnim, LoopAnim, GetWorldCollisionBox, GetRenderExtent) also for HP2 |
| `Utils/File.cpp/.h`, `Package/PackageManager.cpp`, `UE1GameDatabase.cpp` | `File::find_ignoring_case` for `System/`, the ini files, the `Paths=` folders and `Localize` files (whose name may hold a `\\` folder, HP2's cutscenes) (any game; HP2 ships `system/default.ini`) |

### HP1 cutscene skip transition hooks

`Engine.cpp` calls `HP1::BeginModsFrame` before HP1 time scaling, uses `ModsSimulationSteps` for bounded world and
console ticks, and checks `ModsSimulationTick` after each step. `ModsWorldPaused` protects the lesson reveal;
`ModsLevelChanging` clears transient skip state before UnloadMap destroys actors. Non-HP1 games keep one world tick
and their existing clock-update timing; HP2 queued saves still end the frame after that tick.

`Packages/Engine/Actors/Pawn/UPlayerPawn.cpp` calls `HP1::ModsPlayerTick` between PlayerInput and PlayerTick to consume
released gameplay input during the transition while preserving scripted CutMovingTo. `Engine.cpp` consumes key
presses when `HP1::ModsKeyDown` returns true. `Packages/Engine/Subsystems/USurrealAudioDevice.cpp` checks
`HP1::ModsMuteCutsceneSpeech` for the speech slots. Every new hook is gated by IsHarryPotter1; the mod is disabled by
its existing switch, `--no-mod=cutscene-skip` / `--vanilla`.
