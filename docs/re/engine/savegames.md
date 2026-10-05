# Save games

How the games save and load. Both keep a level save (the level package written as `Save<N>.usa`, stock UE1) plus
KnowWonder's `GameSaveInfo` file for the slot page; the script side around them differs per game. Implemented in
`kw/KWSave.cpp` plus three `Engine.cpp` hooks ([engine-hooks.md](../../engine-hooks.md)). Addresses are HP1's.

## HP1's flow (HPMenu scripts)

- **Slots.** `FESlotPage` shows 6 slots (indices 0-5, shown as 1-6). Picking an empty one starts a new game in it;
  `nSelectedSlot` stays the current slot for the whole session. With no slot (`-1`, e.g. a game started with
  `--url=` or after the credits) saves go to slot 99, which the page never lists.
- **Saving** is always `FESlotPage.SaveSelectedSlot` → `HPConsole.doLevelSave(slot)`. It's called:
  - on the first tick of every level reached through `HPConsole.ChangeLevel` (`bLoadNewLevel`, "Saving level at start");
  - when Harry touches a `savepoint` (the floating book; it plays `save_game` and destroys itself first).
  `SaveSelectedSlot` also tops LifePotions up to half of MaxLifePotions first.
- **doLevelSave(i)**: clears `Level.Pauser`, runs `ConsoleCommand("SaveGame " $i)`, restores Pauser, then fills a
  `GameSaveInfo` (numBeans, numStars, house points, `currentLevelString` = `LevelEnterText` up to the first ".",
  `savePointID` = `baseHarry.FindNearestSavePointID()`) and writes it with `SaveGameSaveInfo("GameSaveInfo" $i, info)`.
  The screenshot code ("Snap 3", "SaveSnap") is commented out: thumbnails are pre-made BMPs (below).
- **Loading**: `FESlotPage.LoadSelectedSlot` → `ConsoleCommand("open save" $slot $".usa")`. HP1's ini has
  `Paths=../save/*.usa`, so the save is opened like a map.

## HP2's flow (hgame scripts)

Read from HP2's decompiled scripts only; HP2's native side isn't reversed yet.

- **Saving is queued.** HP2's `PlayerPawn.SaveGame()` is a script function that only sets `bQueuedToSaveGame`; its
  comment says the engine saves at the end of the level tick, and that "slots are just directories, the save game
  number that the front end will always look for is zero". HP1 saves synchronously from a console command instead.
- New events on every actor, `PreSaveGame` / `PostSaveGame` ("called right before/after it saves the game in
  UnGame::SaveGame()"); `harry` uses them.
- `HPConsole.doLevelSave(i)` still fills a `GameSaveInfo` and writes it with native 325 (`SaveGameSaveInfo`), as in HP1.
- `SavePoint.OnSaveGame` raises Harry's health to `iMinHealthAfterDeath` for the save and restores it afterwards.
- Loading after death is `ConsoleCommand("LoadGame 0")` (`harry`, `SleepingGoyle`), a console command HP1 doesn't have.

In [hp2_compare.md](../reports/hp2_compare.md) `UViewport::Exec` grew from 4000 to 5271 bytes (probably the new
commands) and `LoadMap` changed by 8 bytes; the four save natives below grew by the DebugInfo check only in size, HP2
code not read yet. The `FString` serializer in Core.dll is identical.

## Engine side

In HP2 `PaintProgress` differs only in offsets; `UGameEngine::SaveGame` changed (1154 -> 1380 bytes, HP2 0x103AB430),
not read yet.


- **`UGameEngine::SaveGame(int)`** [HP1 0x103A2280] is stock UE1: sets `LevelInfo.LevelAction` to saving, paints one
  progress frame (below), saves the
  level package to `"%s\Save%i.usa"` (SavePath, slot) with `UObject::SavePackage`, copies hub files
  `Game%i.usa` → `Save%i_%i.usa` (HP1 never uses hubs), resets the movers' saved positions and LevelAction. It is
  **synchronous**, which is why doLevelSave can clear Pauser around it. SurrealEngine defers `SaveGame` to the end of
  the frame; for HP1 the hook saves at once (`KW::SaveGame`).
- **The save screen**: `UGameEngine::PaintProgress` [HP1 0x10397BA0] draws one frame with the viewport player's
  `FlashFog` forced to (0, 0.1, 0.25, W 0.2): the world at 0.2 brightness plus a dark blue, and on top
  `baseConsole.DrawLevelAction` prints the LEVACT_Saving message, HPDialog `nearly_nick_40` ("Your game will restart
  from this Save Game book."). The frame stays on screen while the synchronous save runs (about 3 s in Flipendo,
  measured 2026-10-05 in Lev_Tut1b). `UGameEngine::Draw` takes the flash from `Viewport->Actor`, not from the camera
  actor the view goes through; SurrealEngine used the view target, so no HP1 screen flash showed at all.
- **`UGameEngine::LoadMap`** [HP1 0x1039C3D0] treats a level whose `LevelInfo.bBegunPlay` is set as a save: no
  InitGame/BeginPlay, `TimeSeconds` restored, the saved player possessed. SurrealEngine's `?load=N` path
  (`LoadFromSaveFile` + `PossessSavedPlayer`) does the same, so the `open` hook turns `open saveN.usa` into `?load=N`
  when `<SavePath>/SaveN.usa` exists.
- **`LevelEnterText`**: no HP1 map sets it and no script writes it. `LoadMap` (at 0x1039CBA6) loads the package's
  `LevelInfo0` first and, if its `LevelEnterText` is empty (`appStricmp(.., "")`), copies `URL.Map` into it (FURL+0x1C).
  So it is the map name the level was travelled to, with `.unr` when the URL had it (`SetStory(3, "Lev_Tut1.unr")`),
  which is why doLevelSave cuts at "." and why one thumbnail is named `SGS lev3_troll.unr.bmp`. A save keeps its text.

## Natives (both games)

| Native | HP1 | Behaviour |
|---|---|---|
| `SaveGameSaveInfo(dir, obj)` (325) | 0x10413D10 | `GFileManager->CreateFileWriter(SavePath * dir)`; writes the 4 ints at obj+0x28..0x34 raw, then `<< FString` at +0x38 |
| `LoadGameSaveInfo(dir, obj)` (326) | 0x10413F70 | same layout read back; false if obj is None or the file can't be opened (= empty slot) |
| `SaveGameExists()` (3972) | 0x1040C420 | `FileSize("%s\Save%i.usa", SavePath, 9) > 0`; only commented-out script calls it |
| `CreateTextureFromBMP(name, file)` (321) | 0x10413630 | imports `file` as texture `name` into package `SavePics`; None if it fails |

`GameSaveInfo` file (`<SavePath>/GameSaveInfoN`, no extension), little endian:

```
int32 numBeans; int32 numStars; int32 numPoints; int32 savePointID;
FString currentLevelString   // compact index length incl. terminator (0 = empty), negative = UTF-16,
                             // else one byte per char (Core 0x10150830 picks UTF-16 only for chars > 0xFF)
```

The `GameSaveInfo.uc` comment explains the raw layout: "all objects in this class are individually saved, as I
couldn't get the object-level stuff to work".

**Thumbnails.** `FESlotPage.UpdateSlots` tries `..\Save\SGS <level><savePointID>.bmp`, then `..\Save\SGS <level>.bmp`,
then `..\Save\DefaultGameSnap.bmp` (paths relative to System). They ship in the retail `Save` folder as 128x128 8-bit
BMPs. `FEFilePage` (unused) would read `SaveGameSnap<i>.bmp`.

## Latent actions in a save

A suspended state saves the state, a byte offset into its bytecode and a latent action ID; SurrealEngine maps the
offset both ways (`FindOffset` / `FindStatementIndex`, `Packages/Core/UObject.cpp`), so the resume point survives.

- **`Sleep`'s remaining time** is `Actor.LatentFloat`, a script property saved with the actor: `AActor::execSleep`
  [HP1 0x104081E0] [HP2 0x10415A30] stores the seconds there, `execPollSleep` [HP1 0x104083E0] [HP2 0x10415CB0]
  subtracts each tick's DeltaTime and is done once it is below half of it. `APawn::execStopWaiting` [HP1 0x103D5DB0]
  [HP2 0x103E32D0] sets it to -1 only when the latent action is Sleep (LatentFloat is also WaitForLanding's timer).
  SurrealEngine kept the time in a C++ field no save holds, so after a load every sleeping actor woke on the first
  tick; fixed for every game (engine fix, [engine-hooks.md](../../engine-hooks.md)). Checked 2026-10-05 in Lev_Tut1:
  `@trigger HelpWithJumping` (Dispatcher6, Sleep 3 s between events), `SaveGame 97` 1 s in, `open save97.usa`:
  LatentFloat comes back at the saved value and the next event fires when it runs out.
- **The IDs** (`FFrame.LatentAction`, written to the save): HP1 writes 384 for Sleep, 385 FinishAnim, 302
  FinishInterpolation, 501 MoveTo, 503 MoveToward, 505 StrafeTo, 507 StrafeFacing, 509 TurnTo, 511 TurnToward, 528
  WaitForLanding (the exec functions; HP2 checked for 384 and 385 only, same). SurrealEngine registers Sleep as 257 and FinishAnim as 262, so `kw/KWSave.cpp`
  registers 384/385 for them: a save from the original game resumes a Sleep / FinishAnim instead of continuing at once,
  and our saves use the original's IDs. Saves made before that (257/262) still load.

## Not ported

- `CreateTextureFromScreenShot`, `SaveObjectAsFile`, `LoadObjectAsFile`: no script calls them.
- `Snap 3` (FEBook.OpenBook, HPConsole): UViewport::Exec copies the frame, shrunk by 2^N, into the viewport's snapshot
  buffer for SaveSnap / CreateTextureFromScreenShot. Nothing in HP1 reads the buffer, so Flipendo accepts the command and
  does nothing (`KW::ViewportCommand`, `kw/KWSave.cpp`).
- SaveGame's mover-position bookkeeping.
- **Regions.** Every actor's `Region` (zone, BSP leaf) is in the save and the original trusts it. Flipendo's saves from
  before the `FindRegion` leaf fix (2026-10-05) hold wrong leaves, so `KW::SaveGameLoaded` recomputes every actor's leaf
  after a load (a no-op for good saves); without it a dropped portcullis stayed black until it moved.
- Native-only state isn't in the save package, so it restarts on load: particles, our side tables. The camera
  (`PotCam`, a BaseCam) is all script state and is saved with everything else. Checked 2026-10-04 in Lev_Tut1
  (`SaveGame 98` then `open save98.usa` while Harry stands in the entrance hall): PotCam0 comes back at the same
  location and in the same state, and screenshots before and after match. The earlier report of the camera
  sitting further back after a load wasn't reproduced; it may need a save taken while the camera is still moving.
