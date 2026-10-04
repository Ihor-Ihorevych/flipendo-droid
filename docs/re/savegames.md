# Save games

How HP1 saves and loads, from `HPMenu` scripts and Engine.dll. Implemented in `kw/KWSave.cpp` plus three
`Engine.cpp` hooks ([engine-hooks.md](../engine-hooks.md)).

## Flow (scripts)

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

## Engine side

- **`UGameEngine::SaveGame(int)`** [HP1 0x103A2280] is stock UE1: sets `LevelInfo.LevelAction` to saving, saves the
  level package to `"%s\Save%i.usa"` (SavePath, slot) with `UObject::SavePackage`, copies hub files
  `Game%i.usa` → `Save%i_%i.usa` (HP1 never uses hubs), resets the movers' saved positions and LevelAction. It is
  **synchronous**, which is why doLevelSave can clear Pauser around it. SurrealEngine defers `SaveGame` to the end of
  the frame; for HP1 the hook saves at once.
- **`UGameEngine::LoadMap`** [HP1 0x1039C3D0] treats a level whose `LevelInfo.bBegunPlay` is set as a save: no
  InitGame/BeginPlay, `TimeSeconds` restored, the saved player possessed. SurrealEngine's `?load=N` path
  (`LoadFromSaveFile` + `PossessSavedPlayer`) does the same, so the `open` hook turns `open saveN.usa` into `?load=N`
  when `<SavePath>/SaveN.usa` exists.
- **`LevelEnterText`**: no HP1 map sets it and no script writes it. `LoadMap` (at 0x1039CBA6) loads the package's
  `LevelInfo0` first and, if its `LevelEnterText` is empty (`appStricmp(.., "")`), copies `URL.Map` into it (FURL+0x1C).
  So it is the map name the level was travelled to, with `.unr` when the URL had it (`SetStory(3, "Lev_Tut1.unr")`),
  which is why doLevelSave cuts at "." and why one thumbnail is named `SGS lev3_troll.unr.bmp`. A save keeps its text.

## Natives

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

## Not ported

- `CreateTextureFromScreenShot`, `SaveObjectAsFile`, `LoadObjectAsFile`: no script calls them.
- `Snap 3` (FEBook.OpenBook): unknown console command; it only fed the commented-out screenshot saving.
- SaveGame's LevelAction/mover-position bookkeeping (nothing renders during our synchronous save).
- Native-only state isn't in the save package, so it restarts on load: particles, the camera's internal state
  (the camera sits slightly further back right after loading), our side tables.
