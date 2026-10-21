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
  then overrides any other state when the master list holds it (case-sensitive), and becomes the player's
  `CurrentGameState`, upper-cased. A state not in the master list: "**** Caution: The player pawn has
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
  for maps other than Entry; `--gamestate=<token>` is the `GAMESTATE=` override (from 2026-10-21; it doesn't apply to
  a `--url` hub, whose Harry starts at `NONE`). Checked 2026-10-21 that the event goes to every actor, tokens or not,
  as here.

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
- Engine side, HP2 only (none of these is in HP1's DLLs; read 2026-10-20 in a fresh analysis of the DLLs, the old
  Engine database was damaged): Engine.dll `ULevel::SavePersistentActors` [HP2 0x103B9030],
  `ULevel::LoadPersistentActors` [HP2 0x103B9E40], `AActor::SerializePersistence` [HP2 0x103811D0]; Core.dll
  `UStruct::SerializeTaggedPersistentProperties` [HP2 Core 0x10129300], `SavePersistentActorCache` [HP2 Core
  0x10157790], `LoadPersistentActorCache` [HP2 Core 0x10157E20]. All do nothing in the editor.
- Who calls them: `UGameEngine::Exec`'s `SAVEPACTORS` saves under `LevelInfo.LevelEnterText`; `UGameEngine::LoadMap`
  loads for every map but Entry with `URL.Map`, right after the level package is in and before the GameInfo spawns
  and InitGame / BeginPlay run (a save file's URL names `Save0`, which has no file); `UGameEngine::SaveGame` calls
  `SavePersistentActorCache` after the save; `appInit` (Core, at every start, once the slot folder is known) and
  `LOADGAME` call `LoadPersistentActorCache`.
- Paths: `USystem` +0x40 is the save folder (the slot's, [startup](startup.md#save-slots--saveslot-and--loadgame)),
  +0x4C the Maps folder (appInit: the folder above `System` plus the `Paths` entry for maps). The file is
  `<save folder>\cache\<map without extension>_pa.usa`; the map name is cut after its last `\` and gets `.unr` when it
  has no "unr" in it. The map file's size (FileSize) and time (GetGlobalTime: `_stat`'s modification time in seconds)
  go into the header.
- The cache: `SavePersistentActorCache` deletes the save folder's `*_pa.*` and copies `cache\*_pa.*` into it;
  `LoadPersistentActorCache` empties `cache\` of `*_pa.*` and copies the save folder's in. So the hubs' state travels
  with the slot: starting or loading a game brings them back as they were at the save, travel in between only touches
  the cache.
- Save: the level's actors with `bPersistent` (bit 0 of the dword at +200); none: nothing written (an older file
  stays). Layout, each string as an int32 byte count of its UTF-16 text with the terminator, then the text:
  `"PA0"` (8 bytes, UTF-16 with terminator), map file name, int32 map file size, int64 map file time, int32 actor
  count, then per actor: name, class name (no package), int32 byte size of the rest (patched in after), the tagged
  properties; then the string `EOF`. Logs "*** Starting to Save Persistent Actors for map %s" / "*** Finished Saving
  ...". "*** Could not Save Persistent Actor file %s, CreateFileWriter had an error!!!!." when the file can't be made.
- `SerializePersistence` sets the object flag 0x40000000, preloads the class (writes nothing) and runs
  `SerializeTaggedPersistentProperties` (on a plain loader or saver). Saving writes **every** property of the class
  chain (the class's own first, then its parents') that isn't native (0x1000) or, the archive being persistent,
  transient (0x2000), except `AuxAnims`, whether or not it differs from the defaults, then `None`. So an actor comes
  back with all its script variables, but not its running state code (the state frame isn't written; the scripts
  restore `Characters`' state themselves, above).
- The property tags are UE1's (name; info byte = type | size class << 4 | 0x80 for a true bool or an array index;
  a struct's name; the size when its class doesn't give it; the array index), but through FFileManagerWindows' file
  writer in Game.exe (`sub_10909740`, its `<<FName`): **names are strings** (int32 byte count + UTF-16), and
  `<<UObject*` is the base FArchive's, so **object references write nothing**: they are never saved and an actor keeps
  the map's. The tag's size comes from a counting archive (Core `off_1018373C`) that counts 4 bytes for a name or an
  object, so for those the size doesn't match what follows; it is only used to skip. Values are SerializeItem's: byte
  1, int and float 4, a bool in a tag none (its value is the 0x80 bit), a bool in a struct 1 byte, Str FString
  (compact index of the length with terminator, negative for UTF-16, then the characters), struct its fields'
  values in order, dynamic array a compact count then the elements.
- Load: "*** Could not load Persistent Actor file %s (file is probably missing)."; builds name -> actor of the level's
  persistent actors (none: stop); checks the header: map name (ignoring case), "FinalMapName != Saved map name",
  size, time ("Loading map FileSize that this PA file is for", "Saving map FileTime ..."): a file for a changed map is
  ignored. Per entry: the level's actor of that name (taken out of the table) gets the properties; none: logs "***
  Actor is NOT in level %s but is in pa file %s!", finds the class in any package and spawns it with the saved name
  (`ULevel::SpawnActor`), or skips the entry by its size if the class isn't found. The tag loader: a tag goes to the
  property of that name; a String tag on a Str property is read as bytes; a type mismatch, an index out of bounds, a
  struct of another name, a native or transient property ("Property %s of %s is ..." unless it's XLevel) or an unknown
  name ("Property %s of %s not found") is skipped by its size. Afterwards a flag 0x20000 at +364 is cleared: LoadMap
  skips actors with that flag in its PreBeginPlay / BeginPlay / PostBeginPlay / SetInitialState loops (and the ULevel vtable +176 call between them), so a
  loaded actor gets them like any other (what sets the flag wasn't looked for). After a good `EOF` every persistent
  actor the file didn't name is destroyed (`ULevel::DestroyActor`): a bean Harry picked up stays gone.
- Flipendo: `src/hp2/HP2Persist.cpp` writes and reads the same file, so a save folder carries over from the original
  game and back (as long as the map files keep their size and time). Not written: fixed-size String properties (raw
  bytes of their declared size, which SurrealEngine doesn't keep). Classes are looked up in `[Editor.EditorEngine]
  EditPackages`. Tested 2026-10-20 between Entryhall_hub and Grounds_hub: a changed property, a picked-up bean and
  an actor only the file has come back as they should.
- Also needed for it: actors spawned at runtime had names a map actor already used (SurrealEngine numbered them from
  0 per class: a spawned `Jellybean0` next to the map's), which made two persistent actors share a name. Spawned
  names now skip names in use (KnowWonder games).

## Lumos surfaces

HP2 marks BSP surfaces Lumos acts on with `PF_HighShadowDetail | PF_Modulated` in the surface's own PolyFlags (not the
texture's); with `PF_Gouraud` too the surface is one Lumos brings in instead of fading out. `PF_DirtyShadows` on one of
them means Lumos has reached it. Read in HP2's Render.dll and D3DDrv:

- `URender::OccludeBsp` [HP2 Render 0x10B21DD0]: while the viewport's PlayerPawn has `bLumosOn` (not in the editor),
  every BSP node with both zones the view zone, whose surface has either flag and not `PF_DirtyShadows`, gets
  `PF_DirtyShadows` on its surface when one of its vertices is within `fLumosRadius` (PlayerPawn, 512, never changed by
  the scripts) of the player's Location. The mark stays for good: from then on the surface fades by distance with or
  without Lumos.
- `URender::DrawFrame` [HP2 Render 0x10B24F30], a surface with either flag (not in the editor): not drawn at all
  unless the viewport's actor is a PlayerPawn. Hardware path: without `PF_DirtyShadows`, alpha 0 (plus `0x80000000`,
  depth write) or 255 for an appearing one; with it, `r = min(nearest vertex distance / (fLumosRadius * 0.8), 1)`, alpha
  `255 - 255r` capped at 200 (0 gets the depth write), or for an appearing one `255r`, at least 100. The flags go to the
  device unchanged, the alpha as a new last parameter of `DrawComplexSurface`. Software path (`SpanBased`): a marked
  surface is drawn `PF_Translucent`; an unmarked appearing one only while `bLumosOn`.
- `UD3DRenderDevice::SetBlending` [HP2 D3DDrv 0x10009270]: both flags blend as INVSRCALPHA / SRCALPHA, the alpha in
  the vertex colour (`DrawComplexSurface`). So alpha 0 is solid, 255 invisible. With one flag only the surface draws as
  usual (`PF_Modulated` modulates).

The grounds wall (Grounds_hub surfaces 1566 and 1567, Grounds_Night 1216 and 1217, `HogBrickTrimGrass_GH1`, flags
`0x800048`) is solid until Lumos is lit near it, then fades as the player comes close.

Flipendo: `HP2::MarkLumosSurfaces` (from `VisibleFrame::Process`) and `HP2::LumosSurface` (from `VisibleNode::Draw`).
INVSRCALPHA / SRCALPHA with alpha `a` is the devices' premultiplied `PF_Highlighted` with colour and alpha `1 - a`
(`SurfaceInfo::Opacity`), which scales the texture and lightmap, not the fog map. Alpha 0 draws the surface without
the two flags (solid), 255 skips it.

## Tick order

Checked in HP2's Engine.dll. HP2's `Actor.uc` adds `TickParent` and `TickParent2` (HP1 has neither). `AActor::Tick`
(0x103C0E10) starts by calling `Tick` on the actor's `TickParent` (+0x3C), then its `TickParent2` (+0x40), then its
`Owner` (+0x38) unless the owner's `TickParent` or `TickParent2` is the actor itself; only then comes the "ticked
this frame already" test, so each of them ticks once and before the actor. HP1's ticks only the owner first.
`ULevel::DestroyActor` (0x103B0D20), in its loop over the level's actors that also clears `Owner`, sets
`TickParent` / `TickParent2` to None wherever they point at the destroyed actor.

Users in the scripts: `CutScript` sets the camera's `TickParent2` to itself when it captures `BaseCam`, so the
cutscene ticks before the camera; `BaseCamTarget`, `FlyToController` and HPawn's FlyTo use `TickParent`. Seen
2026-10-21 in PrivetDr's intro: `BaseCam1` has `Owner` BaseCamTarget1, `TickParent` its SplineManager1 and
`TickParent2` CutScriptDisk0; SurrealEngine's owner-only order ticked the camera before the spline that moves it.
Port: `src/hp2/HP2Tick.cpp`.

## Special pause

Checked in HP2's Engine.dll. `UGameEngine::InputEvent` [HP2 0x103AC4C0] first tests Delete (`IK_Delete`) pressed:
when `[HGame.baseConsole] bDebugMode` is True it toggles a flag in the level (ULevel +4380), and the key goes on as
usual. `AActor::Tick` [HP2 0x103C0E10], after the tick parents and the bTicked test: in that special pause a PlayerPawn
with a Player still reads its input and gets `PlayerInput`, and only an actor with `bCanMoveInSpecialPause` (BaseCam)
ticks, with `bInSpecialPause` set; BaseCam's script then flies it on the player's input. Outside it `bInSpecialPause`
is cleared. HP1's Actor has the same two flags and its `AActor::Tick` [HP1 0x103B3840] the same test (without the
input), but nothing in HP1 sets the flag. Port: `HP2::InputEvent` / `SpecialPauseTick` (`src/hp2/HP2Tick.cpp`) from
`Engine::InputEvent` and `ULevel::TickActor`. Checked 2026-10-21: Delete in debug mode sets BaseCam's
`bInSpecialPause` and holds Harry; Delete again lets go.

HP2's debug mode also has `harry.DisplayFirstErrorMessages` compare every pair of cutscene actors in one foreach
inside another. KnowWonder's Core.dll counts only Jump tokens against its runaway limit (`UObject::execJump`
[HP2 Core 0x10133D00], 10,000,000, then a log line); SurrealEngine stopped any function after 500,000 statements and
broke into its script debugger, which without a debugger froze the input. KnowWonder games get 100,000,000
(`0010-engine-fixes`).

## Stair smoothing

Checked in HP2's Engine.dll. `APawn::performPhysics` (0x103F2B80) calls an unexported helper (sub_103F29A0, through the
thunk sub_10302CFC) right after its physics switch. Outside PHYS_Walking it zeroes `Actor.SavedPrePivotZ` (HP2 only,
after `PrePivot`). Walking, `d` = the tick's horizontal movement (Location - OldLocation; physWalking sets OldLocation
at its start) times `StepHeight / StepWidth` (Pawn, HP2 only, 16 / 32 by default), at most StepHeight; a non-zero
SavedPrePivotZ moves `d` towards 0 without passing it. Then, if the pawn went down by StepHeight ±1 this tick,
SavedPrePivotZ = StepHeight - d; up by StepHeight ±1, -(StepHeight - d). `USkeletalMesh::GetMeshCoords` (0x10429A00)
adds SavedPrePivotZ to PrePivot.Z, so the mesh lags a step behind the collision cylinder and catches up as the pawn walks
on. Only horizontal movement gives it back: a pawn that stops right after a step keeps its mesh offset until it moves
again. `BaseCamTarget` adds it to the camera target too (script). Seen 2026-10-21 walking Harry down Grandstaircase_hub's
stairs: 16-unit steps, SavedPrePivotZ up to about 14 after each and back towards 0. Port: `src/hp2/HP2Steps.cpp`,
`KW::GetMeshToWorld`. GetMeshCoords' bAlignBottom test also takes HP2's `bAlignBottomAlways` (BroomHarry,
QuidditchPlayer); not ported.

## Pawn turning

Checked in HP2's Engine.dll: `execTurnTo` (0x103E6B90) sets Focus to the point and, for a pawn in PHYS_Walking, Focus.Z
to its own Location.Z, so the latent turn and its poll aim level; HP1's keeps the point as it is. Ported as an
`IsHP2()` branch in `KW::PawnStartTurnTo` ([engine/physics.md](../engine/physics.md#turning)). `rotateToward` (374 ->
356 bytes) is not compared yet.

## Mover sweep

Checked in HP2's Engine.dll. HP2's `Mover.MoverEncroachType` has four more values, `ME_ReallyStopWhenEncroach` to
`ME_ReallyIgnoreWhenEncroach` (4-7), which the script treats as their plain counterparts. `ULevel::MoveActor`
(0x103B3E80) skips the `MultiLineCheck` sweep for a Mover of type 4 or more, so nothing stops it on the way: it passes
through actors and only its encroachment check (the type's own reaction) applies. The rest of the move, up to the hit
loop, is HP1's. Movers of type 7 (all of them 7, no 4-6 seen) seen 2026-10-21 moving in Adv11aCorridor (8), Adv8Forest,
Adv11bSecrets, Ch4Spongify and Grandstaircase_hub (`ElevatorMover18`, Tag Puddle3). With nothing in their way they end
where they did before. Port: an `IsHP2()` test in `KW::MoveActor`.

## Turning movers push

Checked in HP2's Engine.dll. In `ULevel::CheckEncroachment` (0x103B55C0), when a mover pushes a non-mover out of its
way, HP1 (0x103AB5F0) pushes it by the mover's move alone; HP2 adds ten times how far the mover's turn carries the
actor's location round the mover's: the location is turned by the roll change (in YZ, the other way), then the pitch
change (XZ), then the yaw change (XY), each by an unexported helper (0x103B5210, 0x103B5340, 0x103B50F0). The rest is
HP1's: still encroached after the push, the actor is pushed back from the mover's new place and gets EncroachingOn. The
helpers divide the arm by its length and multiply it back, so an actor right on the turning axis would go NaN; the port
leaves that out. Port: `RotationPush` in `src/knowwonder/KWMove.cpp`. Not seen pushing anything in a playtest yet.

## Trailers touch

Checked in HP2's Engine.dll. `AActor::physTrailer` (0x10402470) works out the place and rotation of a non-sprite
trailer as HP1's (0x103F5F80) does, but moves it there with collision instead of placing it and turning it on the spot:
a trailer with bCollideWorld or bBlockActors is placed by `FarMoveActor` (bNoCheck) and then moved 0.01 along X by
`MoveActor` with the new rotation; any other trailer is moved the whole way by `MoveActor`. Either way the move sweeps,
so the trailer touches what it passes. That is what the Basilisk's `GenericColObj`s on its head and breast bones need to
be hit. Sprite trailers are unchanged. Port: an `IsHP2()` branch in `KW::PhysTrailer`.

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

- Particle colour (checked 2026-10-21): HP2's Render.dll fades it by `Opacity * Alpha` (Actor +440; +416 is
  ScaleGlow, after `SavedPrePivotZ` at +412) in every pass's light slot (billboard/shard/liquid/shell
  `sub_10B162A0` [HP2 Render 0x10B162A0], and `sub_10B18670`, `sub_10B1AA50`); HP1's uses `ScaleGlow * Alpha`
  ([engine/particles.md](../engine/particles.md)). Modulated still fades towards grey. Port: `KW::ParticleLight`.
- Sprites (checked 2026-10-21): `URender::DrawFrame` [HP2 Render 0x10B24F30] draws the frame's sprites in two passes,
  the second for an actor with Style `STY_Translucent` or `Opacity < 1` (ghosts 0.75, `LessonBackground` 0.5,
  `FadeActorController`'s fades); `DrawActorSprite` itself doesn't read Opacity. SurrealEngine draws every sprite
  in its sorted translucent pass already, so nothing changes here.
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
- Sounds (read 2026-10-20). `Actor.PlaySound` gained `optional bool Disable3D, optional bool Loop`.
  `AActor::execPlaySound` [HP2 0x104192B0] is HP1's (Id = object index * 16 + slot * 2 + bNoOverride, radius 1600 when
  0, Volume and Pitch * 100) and hands both to `AActor::CheckHearSound` [HP2 0x104189D0], which is HP1's (0.877
  radius, through BSP 0.35 volume within 0.6 of the range unless the hearer is the Instigator) except that **a Talk
  slot sound skips the BSP test**. `APawn::execClientHearSound` [HP2 0x104156A0] builds ALAudio's flags from the
  sound's own (USound +180): **2 loops** when Loop is set, **0x10 plays the sound without position** when Disable3D is
  set or the sound's actor is the viewport's pawn or owned by it (Harry's own sounds, his wand's). ALAudio's
  `PlaySound` [HP2 ALAudio 0x100040F0]: 0x10 puts the source at 0,0,0 relative to the listener (`AL_SOURCE_RELATIVE`),
  2 sets `AL_LOOPING` (not for a streamed sound), 8 takes the music volume; the Talk slot gets priority 100, the
  pitch is clamped to 0.1..2, the volume (times SoundVolume) to 0.01..1; the reference distance is the radius in
  metres (0.0127 per unit). The scripts that loop: the movers' `MoveAmbientSound` while they move (stopped with
  StopSound when they arrive); dialogue is played Disable3D.
- `Actor.StopSound` gained `optional float FadeOutTime`. `AActor::execStopSound` [HP2 0x1041A550] passes it to ALAudio's
  `StopSound(actor, id, sound, fade)` [HP2 ALAudio 0x100048C0], which takes **every** source of the actor whose id
  matches (ignoring bit 0; slot None: every allocated one) and, if given, that sound (HP1's Galaxy stops the first
  one); with a fade above 0 the source fades out linearly over it (`Update`, a step of at most 1 s) and then stops.
- Flipendo: `KW::PlaySound` / `CheckHearSound` / `ClientHearSound` and `KW::StopSound` (`src/knowwonder/KWSound.cpp`)
  with HP2 branches; the audio device plays a "no position" sound at the camera (where its listener is), loops the
  channel (`AudioDevice::SetLooping`) and fades a stopped sound (`USurrealAudioDevice::StopSoundHP2`). Before
  2026-10-20 a mover's sound played once and stopped, Harry's own sounds came from his position as heard from the
  camera, dialogue was muffled behind walls, and every StopSound cut at once.
- Lip sync (read 2026-10-21). A sound with flag 0x80 carries a second lazy array, `FLipSyncData` (USound +68; Engine.dll
  computes it when the sound is imported, `USound::CalcLipSyncData`): one amplitude byte every 0.02 s
  (`fAmpSampleInterval`), the sound's duration being the float USound +104 (the first of the five values after the
  flags). 2399 of HP2's sounds have it (all the dialogue). `FLipSyncData::GetMouthPos(t)` [HP2 0x10388EB0] is
  `GetInterpAmplitude(t) / GetMaxAmplitude() * 0.9 * 0.7`: the amplitude at t (clamped to the duration) blended
  linearly between the two samples around it [HP2 0x10388F30], over the sound's largest amplitude [HP2 0x10389140],
  so a mouth position from 0 to 0.63. No script and nothing else in Engine.dll uses it: ALAudio does it all.
  `UALAudioSubsystem::PlaySound` [HP2 ALAudio 0x100040F0], when the sound has the flag and is played on an actor,
  gives the source a tracker (`sub_100075D0` [HP2 ALAudio 0x100075D0]): `CreateAnimChannel(AnimChannel, AT_Replace,
  'mouth_root', bTransient false, bNotReplaceable true)` on the actor and `PlayAnim('mouth_anim')` on the channel at
  rate 0. `Update` (`sub_10007670` [HP2 ALAudio 0x10007670]) sets the channel's `AnimFrame` (Actor +0x68) to
  `GetMouthPos` of the time since the sound started (real time; a paused source shifts the start) whenever 0.05 s have
  passed since the last update. When the source stops (`OldStopSound` [HP2 ALAudio 0x10006E30], through
  `StopSoundSource`; also a sound that ended) the channel plays `mouth_rest` (rate 1, tween 1). An actor without a
  `mouth_root` bone (the narrator's hidden pawn) gets no channel. Flipendo: `HP2::UpdateLipSync` / `StopLipSync`
  (`src/hp2/HP2LipSync.cpp`) from the audio device's `UpdateLipSync` / `ClearLipSync` (SurrealEngine's place for Deus
  Ex's lip sync); a paused sound shifts the start (`HP2::SoundPaused`). Checked 2026-10-21 in PrivetDr's intro: Dobby's lines move
  his mouth channel between 0 and about 0.5.
- HP2 has no IpDrv package; SurrealEngine's IpDrv property offsets are skipped for it (`0200-hp2.patch`).
- How far a sound carries, its channel, the ambience and pausing (read 2026-10-21, `UALAudioSubsystem::Init`
  [HP2 ALAudio 0x10002F40], `PlaySound` [0x100040F0], `Update` [0x100052D0], `SoundPriority` [0x10006F10],
  `FindLeastImportantSound` [0x10007180], `PauseSounds` / `UnpauseSounds` [0x10005250, 0x10005290]):
  - Distance: OpenAL's inverse distance clamped model. The reference distance is the sound's radius, the rolloff
    `[ALAudio.ALAudioSubsystem] RollOff` (1), and there is no maximum distance: full volume within the radius, then
    radius / (radius + rolloff * (distance - radius)), half at twice the radius, never silent. HP1's Galaxy fades
    linearly to nothing at the radius. A sound with radius 0 plays at Core.dll's `GAudioDefaultRadius`, 450 (HP1's
    `execClientHearSound`: 1600); the hearing test before it (`CheckHearSound`) still uses 1600.
  - Doppler: `alDopplerFactor(DopplerFactor)` (1); every frame each positioned source gets its actor's velocity (kept
    when it is 300 m/s or more) and the listener the camera's movement over the frame (kept from 50 m/s).
  - Priority: volume times `1 - distance² / (radius * GAudioMaxRadiusMultiplier)²` (from 0.01 to 1;
    `GAudioMaxRadiusMultiplier` is Core.dll's 100), plus 4 for lip sync data (flag 0x80), 2 for a sound at the music
    volume (8), 1 for one without position (0x10); recomputed every frame. A Talk slot sound starts at 100. A new sound
    takes the channel of the same actor and slot (a bNoOverride one doesn't play instead), else the channel with the
    lowest priority below its own, else `FindLeastImportantSound`: the ambient sound playing in the most copies gives
    up its least important copy, and with no ambience playing any sound below the new one does (an ambient sound may
    only push out another ambient sound). `[ALAudio.ALAudioSubsystem] Channels` sources, at most 32.
  - Ambience is the driver's, not the engine's: every 0.2 s while the viewport is realtime and the game isn't paused,
    every actor with an AmbientSound that isn't playing is a candidate, one the player views from (its ViewTarget) or
    owns without position wherever it is, any other within SoundRadius * 100. The radius is the **byte** SoundRadius,
    not `WorldSoundRadius` (25 * (SoundRadius + 1)): a torch with SoundRadius 16 is at full volume within 16 units.
    Grouped by sound with the ambient sounds already playing and ranked by priority, at most three copies of a sound
    play (the rest stop) and the top three not playing start, looped, with their pitch following SoundPitch. Then every
    ambient sound out of SoundRadius * 100, or whose actor changed its AmbientSound, stops. An ambient sound takes its
    actor's SoundVolume / 255 every frame (no AmbientFactor), and any sound of an actor with a LightType is scaled by
    its LightBrightness / 255.
  - The sounds pause (`alSourcePause`) when the viewport stops being realtime or the game is paused, and go on when
    either ends; a paused source isn't updated and its lip sync waits. The console commands `PAUSESOUNDS` and
    `UNPAUSESOUNDS` do it at once, `RESTARTSOUNDS` (sent by the engine after a queued save) at the next update.
  - Not used: `ULevel::IsAudibleAt` and `CalculateRadiusMultiplier` [HP2 0x103BEFA0, 0x103BF120] (zone occlusion,
    `0.85^(zone distance²)`) and Actor's `SoundOcclusion` exist, but nothing in ALAudio calls them. The radius ease
    (a radius changed by ModifySound goes back to the PlaySound one over a second) never runs: no HP2 script calls
    ModifySound. PlaySound's fade-in is always 0 from ClientHearSound.
  Flipendo (`src/hp2/HP2Audio.cpp`) from `USurrealAudioDevice` (`0140-knowwonder-audio`) and an inverse model, source
  velocity and pause in SurrealEngine's OpenAL device (`0010-engine-fixes`). Before 2026-10-21 HP2 used HP1's linear
  Galaxy falloff, SurrealEngine's ambience (WorldSoundRadius, AmbientFactor), 16 channels and no pause.
- `I3DL2Listener` (Engine.u): EAX / I3DL2 reverb settings per zone. ALAudio's `SetI3DL2Listener`
  [HP2 ALAudio 0x100070D0] is a 3-byte stub (`ret`): retail applies no reverb.

## Not needed

- `WalkTexture` / `LevelInfo.bCheckWalkSurfaces` / climbable textures: stock UE1 code, raised only when a level sets
  `bCheckWalkSurfaces`; no HP1 or HP2 script uses it.
