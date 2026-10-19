# HP2 engine work for later

What HP2's engine does that HP1's doesn't, or does differently, to port once HP2 is enabled. Unless an item says it
was checked in HP2's retail DLLs (`../ida/hp2/`), it hasn't been: read it there before porting. Script-side
differences are in [gameplay.md](gameplay.md); what both games share is in [`../engine/`](../engine/).

## GameState: actors screened by the story state

The biggest engine feature HP1 doesn't have. HP2's Engine.dll has it (`ScreenActorsByGameState`, the
"GameState ERROR" message), and its scripts use it (`Characters`, `HPawn`, `CutScene`, `CreatureGenerator`,
`Triggers`, `harry`, `HPConsole`, the shortcut browser, ...).

- `PlayerPawn.GameStateMasterList` (const string, the list of GameState tokens), `GameStateTokenLen`,
  `TotalGameStateTokens`, `CurrentGameState` (travel string); script helpers `GetGameStateMasterListToken`,
  `SetGameState`.
- An actor lists the states it belongs to in its `Group` name (`None,GSTATE000`; case doesn't matter) and the ones it
  stays out of in `ExcludeGameStates`; `bInCurrentGameState` (const, default true, "Set by Engine:
  ULevelBase::ScreenActorsByGameState()") and `bPersistent` (saved separately).
- `ULevel::ScreenActorsByGameState` [HP2 0x103B86D0], from `UGameEngine::LoadMap` for every map but Entry, late, once
  the player is in. The player is the first pawn with `bIsPlayer`; its `CurrentGameState`, upper-cased, is the state.
  `NONE` (`PlayerPawn`'s default): nothing is screened and no event raised. A `GAMESTATE=` command line option
  overrides the state when it is in the master list. A state not in the master list: "**** Caution: The player pawn has
  an Invalid CurrentGameState ..." and nothing else. Then for every actor: a non-empty `ExcludeGameStates` holding the
  state puts it out; otherwise each `GSTATE` token in its upper-cased `Group` (up to the next comma) is checked: one of
  `GameStateTokenLen` characters found in the master list and equal to the state puts it in, any other token logs
  "*** GameState ERROR Actor: %s has an invalid GameState Token: %s". An actor with tokens and no match is out; one with
  neither tokens nor exclusion keeps its `bInCurrentGameState`. Every actor (outside the editor) then gets
  `OnResolveGameState()`.
- The state comes with the map: KnowWonder's maps place Harry, and his saved `CurrentGameState` is the level's start
  state (PrivetDr's `Harry1`: `gstate000`). `PlayerPawn.SetGameState` only stores a new state "to be applied when we
  load a level".
- Without it every CutScene stays enabled: PrivetDr ran its intro (`GSTATE000`) and `00001PrivetIntroPart3`
  (`GState010`) at once, and Part3 took over Harry and the camera.
- `CurrentGameState` reaches the next map as a travel property. SurrealEngine wrote travel strings quoted and read the
  quotes back in, so the state arrived as `"GSTATE010"`, failed the master list check and nothing was screened (PrivetDr
  after Diagon Alley ran its intro and Part3 together, black screen). `UStrProperty::SetValueFromString` now drops the
  quotes, as UE1's ImportText does (`0010-engine-fixes.patch`, any game).
- Flipendo: `HP2::ScreenActorsByGameState` (`src/hp2/HP2GameState.cpp`), called at the end of `Engine::LoginPlayer`
  for maps other than Entry. The `GAMESTATE=` override isn't ported.

## Persistent actors across hub travel

HP2 lets Harry walk between hub maps (`Entryhall_hub`, `Grounds_hub`, ...) and back; actors marked `bPersistent`
keep their state in between. HP1 has nothing like it and doesn't need it: every HP1 map links only forward to the next
one in the story (checked 2026-10-05 over every `.unr` in HP1's `Maps`).

- Script side: `Actor.bPersistent` ("If the actor is persistant it will be saved/loaded seperatly when you save/load
  your game"). On by default for `GenericSpawner` (and what it spawns, `bMakeSpawnPersistent`; also `bronzecauldron`,
  `chestbronze`), `Jellybean`, `QArmor`, `WizardCardIcon`, `SecretAreaMarker`, `ChallengeScoreManager`,
  `VendorNimbusBroom`. `harry.LoadLevel` stores each persistent `Characters`' state, `LeadingActor` and `navP` names
  in `PersistentState` / `PersistentLeadingActor` / `PersistentNavPName` (read back in `Characters` on the next visit,
  only for the idle / leading-actor states), then runs `ConsoleCommand("SavePActors")` and travels.
- Engine side, HP2 only (none of these is in HP1's DLLs): Engine.dll `?SavePersistentActors@ULevel@@QAE_NABVFString@@@Z`
  (thunk 0x10303445), `?LoadPersistentActors@ULevel@@QAE_NABVFString@@@Z` (0x103037D3),
  `?SerializePersistence@AActor@@QAEXAAVFArchive@@@Z` (0x10304A43); imported from Core.dll
  `?SerializeTaggedPersistentProperties@UStruct@@QAEXAAVFArchive@@PAEPAVUClass@@@Z`, `?LoadPersistentActorCache@@YAXXZ`,
  `?SavePersistentActorCache@@YAXXZ`. The UTF-16 string `SAVEPACTORS` is referenced at 0x103A2BEF (an Exec).
  `SerializePersistence` writes the class, then only the properties tagged persistent, so the cache may keep results
  (beans spawned, items taken) rather than resume an actor mid-action; not read yet.
- The HP2 Engine database looked misaligned when this was read: the thunks point 0x10 bytes before the bodies
  (bodies at 0x103B9030, 0x103B9E40, 0x103811D0) and the decompiled names didn't fit. Check the database first.
- Flipendo: nothing yet.

## Lumos surfaces

HP2 marks BSP surfaces that Lumos makes disappear or appear, with combinations of existing poly flags
(`PF_HighShadowDetail | PF_Modulated` = affected, disappearing by default; `PF_Gouraud` = appearing instead;
`PF_DirtyShadows` = always on). The renderer reads the player's `bLumosOn` and `fLumosRadius` (PlayerPawn): in
hardware mode the surface's alpha comes from the distance of its nearest vertex to the player, and appearing surfaces
aren't drawn while Lumos is off. D3DDrv blends affected surfaces as INVSRCALPHA / SRCALPHA with the vertex alpha.
Needs SurrealEngine's BSP surface path and the vertex alpha added for `Actor.Opacity`
([engine/rendering.md](../engine/rendering.md)).

## Pawn turning

HP2's `execTurnTo` flattens the focal point for a walking pawn (`Focus.Z = Location.Z`); HP1's doesn't. Retail sizes
fit: execTurnTo is 250 bytes in HP1, 295 in HP2 (`rotateToward` 374 -> 356). Flipendo's port is HP1's
(`KW::PawnTurnStep`, [engine/physics.md](../engine/physics.md#turning)); add the flatten as an `IsHP2()` branch once
read in HP2's Engine.dll.

## Rendering

- Menus: HP2's `HPConsole` lays the book out like HP1's (`Root.SetScale(RealWidth / 640)`, `FEBook` 640x480), so it
  shares HP1's widescreen canvas (`src/knowwonder/KWCanvas.cpp`): HUD full width, console and menus in a centred 4:3
  area. Its resolution list comes from `FESoundVideoPage.IsSupportedResolution` (640x480, 800x600, 1024x768; 1280x1024
  in debug mode), replaced like HP1's. The page also asks `IsFullscreen` (WinDrv's `UWindowsViewport::Exec`, prints
  True or False) and sends `MLOD <factor>` (Render.dll's `URender::Exec` [HP2 Render 0x10B1EB90]: the global mesh LOD
  factor, ignored here as meshes are drawn at full detail; HP1's Render.dll has it too).
- Cutscene text: `HPHud.DrawCutStyleText` draws with `Style = 2` (masked) in `CutSceneManager.colorCutTextBlue`, whose
  alpha is 0. A masked bitmap font ignores the alpha, so English subtitles show (checked 2026-10-17 at 640x480 and
  1920x1080). The languages that use system fonts (`CreateNativeFont`) would have drawn additively; native fonts now
  ignore the alpha as GDI does ([engine/fonts.md](../engine/fonts.md#system-fonts-consolecreatenativefont)).

- Particle colour: HP2 may fade it by `ParticleFX.Opacity * Alpha`; HP1's Render.dll uses `ScaleGlow * Alpha`
  ([engine/particles.md](../engine/particles.md)). Check HP2's Render.dll.
- Actor shadows, `Actor.Opacity` and the loading screen: expected to be the same as HP1's
  ([engine/rendering.md](../engine/rendering.md)).

## Audio

- Music: ALAudio.dll (OpenAL + Ogg Vorbis) plays the music. Scripts pass a file name in `Music/`
  (`PlayMusic("sm_dia_Ambient02_01.ogg", 0.5)` returns a handle; `StopMusic(handle, fade)`, `StopAllMusic(fade)`;
  `PlayerPawn.Song` is a string). Flipendo: `src/hp2/HP2Music.cpp`, written from the scripts and NOT yet checked against
  ALAudio.dll: one song at a time, looping, linear fades. `KW::UpdateMusic` hands over to it for HP2 (HP1's Galaxy
  music properties don't exist in HP2's PlayerPawn).
- Sounds: every sound export has a header of its own between the format name and the data, whatever the format:
  int32 0x104 (0 for wav), float duration, int32 sample count, bits, channels, frequency, then the lazy-array skip
  offset, then the size and data. Format `XA` is 4-bit ADPCM in 15-byte frames (1 byte shift/filter, 14 bytes of 28
  samples); `src/hp2/HP2Sound.cpp` decodes it with the CD-XA filters. Not checked against ALAudio.dll.
- Scripts: `CreateAnimChannel` (265) has a sixth argument `bNotReplaceable` (reversed, [HP2 0x10415ED0]: matched when reusing a channel; `KW::CreateAnimChannel`).

## Cutscenes and fades

- `CutScene` actors start in `auto state disabled` and only leave it through `OnResolveGameState` (`src/hp2/HP2GameState.cpp`,
  after the player's login), so that event is raised for every actor after a map has loaded. `CutScript` threads read the
  scripts with `Localize("thread_N", "line_M", "Cutscenes\<File>")`: a backslash path that must also match the
  lower-case `System/cutscenes/` folder on Android (`ResolveGamePath`, `PackageManager::Localize`).
- A string `switch` ignores case (`CutScript.ParseCommand` switches on a command as written in the `.int` files against
  upper-case cases). Fixed in `Frame::ProcessSwitch`.
- The same scripts log a lot of `CutError` noise in the retail data as well (`*/` is read as a command; `PrivetIntro`
  and `PrivetIntroPart3` both start at level load and fight over `BaseCam` and `Harry`).
- `FlashFog.W` is the scene brightness as in HP1 (1 = normal): `ClientFadeIn` drives `ConstantGlowFog`, `ViewFlash` eases
  it; the engine hook `KW::ViewFlashParams` serves both games.
- Spoken dialogue is `Sounds/AllDialog.USA_uax` (language editions `.<LANG>_uax`); the scripts ask for the package
  `AllDialog`, so the scan maps every `*_uax` to its base name and prefers `USA`.

## Bytecode and loading

- Script tokens: `UStruct::ReadToken` (loading) and `BytecodeStream` (running) both skip DebugInfo records (0x38) and
  map HP2's tokens 0x39..0x46 to stock. The loader keeps the original bytes in the copy, so jump offsets stay valid.
- `IpDrv.u` does not ship with HP2 (only `IpDrv.dll`): SurrealEngine's IpDrv property offsets accept its absence.
- The retail 1.0 install (`Game.exe` SHA-1 `28b0e54a...`, packages version 79) is what runs; the M212 x64 rebuild
  (packages 115) is a different engine and does not load.

  `Intro_Movie_Music_Edit`, `PrivetDrive2_late2school`, `sm_wwa_willowLevel1p_01_part2`); Flipendo then tries `.ogg`.
  Every song loops, so SurrealEngine's Ogg stream restarts at the end (`OggAudioSource::SeekToSample`); that closed
  the Vorbis decoder while the rest of its last frame was still to be copied, and the next read used the freed buffer
  (a crash about two minutes into the Whomping Willow, at once with a 2.5 s song). Fixed in `0010-engine-fixes`.
- Volumes: ALAudio keeps `MusicVolume` / `SoundVolume` as floats 0..1 (`[ALAudio.ALAudioSubsystem]` ships 0.53 and 0.9),
  and the options page (`FESoundVideoPage`) reads and writes them so (`get ini:Engine.Engine.AudioDevice MusicVolume`
  times 100, `set ... MusicVolume <slider / 100>`). `SETVOLUMES SOUND=<f> MUSIC=<f>` (`UALAudioSubsystem::Exec`
  [HP2 ALAudio 0x100067B0]) clamps each to 0..1 and sets every playing source's gain at once: a music stream gets the
  music volume, a sound its own volume times the sound volume. SurrealEngine's audio device keeps HP1's bytes 0..255
  and parsed `1.000000` as 1 of 255 (near silence after touching a slider); for HP2 it reads and writes its ini
  volumes as floats (`HP2::VolumeFromIni`/`VolumeToIni`), `HP2::AudioCommand` handles `SETVOLUMES`, and on the first
  run it takes its settings from `[ALAudio.ALAudioSubsystem]` and `[D3DDrv.D3DRenderDevice]` instead of HP1's Galaxy
  and Direct3D sections.
- HP2 has no IpDrv package; SurrealEngine's IpDrv property offsets are skipped for it (`0200-hp2.patch`).
- `I3DL2Listener` (Engine.u): EAX / I3DL2 reverb settings per zone, used by ALAudio.

## Not needed

- `WalkTexture` / `LevelInfo.bCheckWalkSurfaces` / climbable textures: stock UE1 code, raised only when a level sets
  `bCheckWalkSurfaces`; no HP1 or HP2 script uses it.

## Story state, skipping, saves, volumes and subtitles (2026-10-15, written from the scripts and runs on a phone)

Not reversed (no HP2 IDA database here); what the scripts and the runs show.

- **Story state comes from the actor's `Group`.** The editor's Group field holds the story state an actor belongs to
  (`"None,GSTATE000"`, `"None,GState010"`). After a map loads the engine marks an actor in the current state when it has no
  `GSTATE` token in its Group or lists the current one (`HP2::ScreenActorsByGameState`). Without it PrivetDr's intro
  (`GSTATE000`) and the Diagon Alley scene (`GState010`, played after the reload that Part2 triggers) both started at
  once and fought for BaseCam and Harry.
- **Strings in a level change.** The travel text quotes a string (`PrintValue`), and the import did not unquote it:
  `CurrentGameState` came back as `"GSTATE010"` with the quote characters, one more pair on every travel. Fixed in
  `ObjectTravelInfo.cpp` (0010 patch).
- **Skipping a cutscene is in the game**: `harry.PlayerInput` calls `HPConsole.StartFastForward` while the HUD is in
  cutscene mode and `bSkipCutScene` (Enter) is down; each `CutScene` goes to `FastForwarding`, the HPConsole raises
  `MUTESOUNDS` / `UNMUTESOUNDS` console commands around it. On the phone a Space key-down (SKIP button, pad A) is
  turned into the same call (`HP2::KeyDown`).
- **Saving**: `PlayerPawn.SaveGame()` queues; `HP2::TickSave` clears `bQueuedToSaveGame` at the end of the frame, raises
  `PreSaveGame` / `PostSaveGame` on all actors and writes the level as Save0 (`KW::SaveGame`). `LoadGame N` opens
  SaveN like a map. HP2's own `UGameEngine::SaveGame` (changed from HP1's) is not read; hubs and persistent actors
  (`SavePActors`, `bPersistent`) are not done.
- **Volumes** are floats in `[ALAudio.ALAudioSubsystem]` (`MusicVolume=0.53`, `SoundVolume=0.9`) and the sliders send
  `SETVOLUMES SOUND=x MUSIC=y` (0..1); the stock audio device reads bytes, so "0.9" read as 0 and its sound scale is
  halved for HP1 only.
- **Subtitles** are drawn by script (`CutSceneManager.DrawText` -> `HPHud.DrawCutStyleText`) with
  `colorCutTextBlue = (255,127,127, A=0)`. UE1 ignores the DrawColor alpha for the Masked and Normal styles; ours used
  it, so the text was invisible. `NCanvas` ignores the alpha for KnowWonder.
- Console commands the scripts send and the engine answers: `MUTESOUNDS`, `UNMUTESOUNDS` (accepted, the sounds are not
  muted), `SETVOLUMES`, `MLOD` (accepted), `ISFULLSCREEN` (True), `LoadGame`, `SavePActors` (still unknown).
- **Dev switches** (Android): `/sdcard/FlipendoHP/start_level.txt` (map) and `/sdcard/FlipendoHP/start_state.txt`
  (`GSTATE020`, the story state the first level is loaded in).

## Saved games, new game and controls on the phone

- Retail 1.0 has no slot page: `FEBook.SaveSelectedSlot` / `LoadSelectedSlot` are empty and `FEMainPage` is a developer
  level list. The launcher (`SetupActivity`) has its own front end for HP2: New game or one of six slots, each with the
  picture the game ships for the level (`System/Thumbnails/<map>.bmp`) from `Slot<N>.txt` written beside `Save<N>.rus`.
  Play writes `/sdcard/FlipendoHP/hp2_start.txt` ("new N" or "load N"); `HP2::OpenStartMenu` reads it, picks the slot
  (`HP2::SetActiveSlot`: the queued save writes `Save<N>`, `LoadGame` reads it) and loads the save or travels to PrivetDr.
- HP2 turns with the arrow keys (`RotLeft` / `RotRight`) and strafes with A / D. The phone's stick, arrows and pad send
  the arrow keys, so on Android `Left` / `Right` are rebound to `StrafeLeft` / `StrafeRight` (`HP2::TweakKeybindings`);
  the camera turns by dragging the screen.

## Probe masks without EndState, and challenge stars (2026-10-19, run on a phone)

- `ChallengeStar` and `FinalStar` (hgame) credit the score from `state PickupProp { function EndState() }`: `ChallengeScoreManager.PickedUpStar` adds 200
  to the time, `PickedUpFinalStar` ends the challenge. The state code (the fly to the HUD, `Destroy`) is HProp's `PickupProp`, inherited by the state of
  the same name in the subclass. HP2's compiled `UState.ProbeMask` of the subclass state holds only the inherited bits (BeginState 16, Tick 36), not
  EndState (17), although the state defines it, so SurrealEngine's `IsEventEnabled(EndState)` was false and neither `GotoState` nor `Destroy` ran it:
  no time, no end of the lesson. Fixed in `UObject::IsEventEnabled`: for HP2 a state that has an EndState function and does not ignore the event gets it.
  Not checked how the original decides (no HP2 IDA database here). Test: `pick` in `hp2_debug.txt` picks the first star of the level.
- `Engine::CalcView` hands `PlayerCalcView` three variables; the script's `out` values come back through `Frame::Call` (`args[i].Store(frame variable)`).
  A guard in `ExpressionValue::Store` that skipped struct values of another `ExpressionValueType` dropped the Vector and Rotator and left the camera at Harry's
  location (`BaseCam` was in the right place, `CameraActor` came back right).
