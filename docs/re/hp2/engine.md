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
  `PlayerPawn.Song` is a string). From ALAudio.dll (2026-10-20):
  - `UALAudioSubsystem::PlayMusic` [HP2 ALAudio 0x10004C70]: an empty song or `None` plays nothing and returns 0. Else
    the path is `..\Music\<song>`, with `.ogg` added when the name's extension isn't `ogg` (FEBook's
    `Intro_Movie_Music_Edit`, `sm_wwa_skurgeExploreEdit_01_ogg`); a transient USound with flags 12 (streamed, music)
    is played through `PlaySound` with flags 28 (+ no position), radius 1000, the fade-in time, and the handle is that
    sound source's index + 1. **Each song is a sound source of its own: a new song doesn't stop the others.** A level's
    music goes on under a jingle a MusicTrigger starts and stops (Adv1Willow's `willow1hit`), and a NewMusicTrigger's
    song starts next to what a cutscene left playing, until a trigger with FadeOutAllSongs stops them all.
  - `StopMusic(handle, fade)` [0x100050C0]: no fade stops the source; else it fades out (from full volume) and stops.
    `StopAllMusic(fade)` [0x100051B0] does that to every source with the music flag (8).
  - Fades (`Update` [0x100052D0]): linear in time, a step of at most 1 s; a music source's volume is MusicVolume.
  - Music always loops: `RegisterSound` [0x10003400] opens a music Ogg as `FFileStream` type 2, which `FFileStream::Read`
    [HP2 Core 0x10178390] rewinds at the end (type 1, other Ogg sounds, ends).
  Flipendo (`src/hp2/HP2Music.cpp`) mixes the songs into the audio device's one music stream; handles are new numbers,
  not reused source indices. `KW::UpdateMusic` hands over to it for HP2 (HP1's Galaxy music properties don't exist in
  HP2's PlayerPawn). Before 2026-10-20 it played one song at a time, so each new song cut off the one before.
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
