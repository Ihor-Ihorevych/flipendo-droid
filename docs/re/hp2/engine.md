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
- Each actor has GameState tokens and `ExcludeGameStates`; `bInCurrentGameState` (const, "Set by Engine:
  ULevelBase::ScreenActorsByGameState()") and `bPersistent` (saved separately).
- After a map loads, `ScreenActorsByGameState` takes the player's `CurrentGameState` (upper-cased) and walks every actor
  that has tokens: it is in the current state unless the state is in its `ExcludeGameStates`; each token must have
  `GameStateTokenLen` characters and be in the master list (else "*** GameState ERROR Actor: %s has an invalid
  GameState Token: %s!!!"). It sets `bInCurrentGameState` and, outside the editor, raises `OnResolveGameState()`.
- Flipendo: nothing yet. Needs a hook after LoadMap's BeginPlay calls (and save loading), gated `KW::IsHP2()`.

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

- Particle colour: HP2 may fade it by `ParticleFX.Opacity * Alpha`; HP1's Render.dll uses `ScaleGlow * Alpha`
  ([engine/particles.md](../engine/particles.md)). Check HP2's Render.dll.
- Actor shadows, `Actor.Opacity` and the loading screen: expected to be the same as HP1's
  ([engine/rendering.md](../engine/rendering.md)).

## Audio

- Music: ALAudio.dll (OpenAL + Ogg Vorbis) plays the music; `PlayMusic` / `StopMusic` / `StopAllMusic` are still
  MISSING in [native_audit_hp2.md](../reports/native_audit_hp2.md).
- `I3DL2Listener` (Engine.u): EAX / I3DL2 reverb settings per zone, used by ALAudio.

## Not needed

- `WalkTexture` / `LevelInfo.bCheckWalkSurfaces` / climbable textures: stock UE1 code, raised only when a level sets
  `bCheckWalkSurfaces`; no HP1 or HP2 script uses it.
