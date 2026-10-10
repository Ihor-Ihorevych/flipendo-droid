# Playtest status (HP1)

Every HP1 map in story order, and whether it has been played through in Flipendo. The index and title are the
game's own (`Dobby.int` `n_<map>`, `HPMenu.int` `level_name_<index>`, [re/hp1/menus.md](re/hp1/menus.md)). Titles
without a map of their own (02, 06, 11, 19, 26, 30, 35, 37) are put with the map before them by the order alone,
not checked in the maps.

Start a level with `tools/run_hp1.sh 0 --level=<map>`. It loads the map in the story flow (`HPConsole.bInHubFlow`,
as New Game and Load set it) with what an average player carries in from the levels before
(`src/hp1/HP1LevelStart.cpp`). Don't playtest with `--url`: it only loads the map, with a new game's state and outside
the story, so the flying lesson runs as its menu replay (no beans or wizard card, the hoops loop, ESC to leave) and
the Quidditch matches as League matches.

What `--level` gives Harry (his `travel` properties, which is all a level inherits; HP1 has no list of learned spells,
the wand picks the spell from the target, `baseWand.ChooseSpell`):

- **Wizard cards**: every card of the earlier levels ([re/hp1/collectibles.md](re/hp1/collectibles.md)).
- **Beans**: 80% of the beans placed and in chests in the earlier levels, less 25 for each card Fred sold
  (`WizardCardCut`: Lev_Tut1, Lev2_HogFront, Lev3_Intro, Lev3_PreDungeon) and the 25 `TakeAllBeansTrigger` takes in
  Lev4_Sneak2. Gnome thefts and beans from other sources aren't counted.
- **House points**: 50 for each spell lesson (5 + 10 + 15 + 20 for its four rounds, all needed to go on: Lev_Tut1,
  two in Lev_Tut3, Lev2_Inc_A, Lev3_Intro), 10 for each star challenge (pays 20, 10 or 5: Lev_Tut1b, Lev_Tut3,
  Lev2_Inc_B, Lev3_Lumos) and 10 for the flying lesson (5 to 20). The potions lessons' penalties are left out.
  The other houses follow `baseHarry.AddHousePoints` at the average of its random parts (Slytherin ahead by
  1 + min(G, 58) / 2, Hufflepuff 0.6 G, Ravenclaw 0.8 G).
- **Quest items**: the flute (Lev2_fire1), dittany, moly and wiggentree bark (Lev3_Dungeon), flobberworm mucus
  (Lev3_DungeonB). No script reads them; saves carry them.
- Health and life potions are a new game's (full).
- **Save slot** 0, or `--slot=<0-5>`, selected as New Game does (`FESlotPage.SetSelectedSlot`), so the save books
  write a slot the Load page lists. Without a slot every save goes to slot 99, which the page never shows.

The counts come from the maps' actors (`tools/uelib_dump props`). A save made in the level before is still the way
to test with an exact state.

| # | Map | Title | Played | Notes |
|---|---|---|---|---|
| 01 | `Lev_Tut1` | Hogwarts Main Entrance (also 02 Defence Against the Dark Arts Class) | yes | Loading a save here once logged `Tut1McGonagall4 fell out of the world` (2026-10-05, not looked into yet) |
| 03 | `Lev_Tut1b` | Flipendo Challenge | yes | |
| 04 | `Lev_Tut2` | Flying Lesson | yes | Story version (`--level`) played by hand to the end (2026-10-05), exits to Lev_Tut3. Its intro crashed once Harry's broom loop faded below zero volume (fixed: Galaxy plays a negative volume silent). After Quit from the pause book the main menu seemed to show only Exit; not reproduced (pause book → main page shows all four buttons, 2026-10-05) |
| 05 | `Lev_Tut3` | Wingardium Leviosa Lesson (also 06 Challenge) | yes | Played by hand through the Wingardium challenge to the level change (2026-10-05). Loading Lev_Tut3b then crashed on a UTF-16 string in the map (fixed, [re/engine/packages.md](re/engine/packages.md)). With Harry close to the mirror, walls crossing its plane covered the reflection (fixed: clip plane). Entered from Lev_Tut2 (2026-10-05). The save at the level start and the first save book both write slot 99 (`Save99.usa`, `GameSaveInfo99`; the slot page's Load restores Harry at the book). Savepoint ID -1 is the original's too: `savepoint.Touch` destroys the book before saving, so `FindNearestSavePointID` can't find it. The Alohomora room's mirror (a mover) drew dark (fixed, [re/engine/rendering.md](re/engine/rendering.md); checked by hand 2026-10-05). The students CutScene6 sends to class after the third Alohomora door stopped at a ramp until CutMovingTo's timeout teleported them: an original high-frame-rate bug, fixed by the 60 fps cap ([re/hp1/original_bugs.md](re/hp1/original_bugs.md); checked by hand) |
| 07 | `Lev_Tut3b` | Second Floor Landing | yes | Entered from Lev_Tut3 (2026-10-05); played by hand from Peeves' duel to the level change to Lev2_HogFront (2026-10-05). Harry's beans, house points and wizard cards were lost at every level change until then (fixed, [re/engine/savegames.md](re/engine/savegames.md#level-changes-travel)). After the duel Peeves T-posed and drifted through the walls: the map never links HPath_F3 to his exit station (original bug, fixed by the PathFixes mod, [re/hp1/original_bugs.md](re/hp1/original_bugs.md)). The Wizard Card pickup camera shook left and right (latent turn polled before the Tick event, fixed, [re/engine/physics.md](re/engine/physics.md)). Still open: the camera flutters for a few frames when Harry lands against a wall (BaseCam.PositionCamera) |
| 08 | `Lev2_HogFront` | Hogwarts Grounds | yes | Played by hand from Lev_Tut3b to the level change to Lev2_Inc_A (2026-10-05). Logged `ChocolateFrog0 fell out of the world` (not looked into yet). Loading Lev2_Inc_A then crashed on a spellTrigger with mover physics (fixed, [re/engine/physics.md](re/engine/physics.md#movers)) |
| 09 | `Lev2_Inc_A` | Herbology Class | yes | Played by hand from the start (`--level`) through the Incendio lesson, the greenhouse (doxies, Venomous Tentacula) to the level change to Lev2_Inc_B (2026-10-05). The spell symbol never showed on creatures (doxies, gnomes): the engine didn't call `EndState` when destroying Harry's `Target`, and then called it only after marking the actor deleted, so the event was skipped (fixed, [re/hp1/spells.md](re/hp1/spells.md); the symbol flashes on a gnome at the cast, checked by hand 2026-10-10; as in the original, which shows none while locked on; on a doxie the original seemed to show none at all, not sure ours matches, [re/hp1/spells.md](re/hp1/spells.md#not-checked-yet)). The gnomes ran in place: latent moves timed out at half their time (fixed, [re/engine/physics.md](re/engine/physics.md); checked by hand 2026-10-10). The doxies react only near Harry: they wake from map triggers and HP1's sight is short (~320 units ahead, ported exactly), so as in the original. Tut1Gnome9/10 and jelly beans `fell out of the world`: original quirks ([re/hp1/original_bugs.md](re/hp1/original_bugs.md)) |
| 10 | `Lev2_Inc_B` | Incendio Challenge (also 11) | yes | Entered from Lev2_Inc_A; played by hand to the level change to Lev2_HogFront_2 (2026-10-05). Lev2_HogFront_2 then logged `H2Crabbe1 fell out of the world` one second after loading |
| 12 | `Lev2_HogFront_2` | Hogwarts Grounds | yes | Played by hand (`--level`) to the level change to Lev2_RemChase (2026-10-10). Logs `H2Crabbe1 fell out of the world` at load (not looked into yet) |
| 13 | `Lev2_RemChase` | Remembrall Chase | no | Entered from Lev2_HogFront_2 (2026-10-10): the screen stayed black (HUD and menus drawn, sound playing). The intro cutscene never got its trigger: map actors started with every probe disabled (fixed, [re/engine/scripting.md](re/engine/scripting.md#disable-and-gotostate)); the intro now plays to the chase (automated run). Chase not played yet |
| 14 | `Lev2_HogFront_3` | Hogwarts Grounds | no | |
| 15 | `Lev2_Fire2` | Forest Edge | no | |
| 16 | `Lev2_fire1` | Fire Seed Caves | no | |
| 17 | `Lev2_Quid1` | Quidditch Match: Gryffindor vs. Slytherin | no | |
| 18 | `Lev3_Intro` | Hogwarts Main Entrance (also 19 Lumos Lesson) | no | |
| 20 | `Lev3_Lumos` | Lumos Challenge | no | |
| 21 | `Lev3_PreDungeon` | Second Floor Landing | no | |
| 22 | `Lev3_Dungeon` | Potions Lesson | no | |
| 23 | `Lev3_DungeonB` | Potions Challenge | no | |
| 24 | `Lev3_PreTroll` | Hogwarts Main Entrance | no | |
| 25 | `Lev3_Troll` | Corridor To The Girl's Washroom (also 26 Troll Battle) | no | |
| 27 | `Lev3_Quid2` | Quidditch Match: Gryffindor vs. Ravenclaw | no | |
| 28 | `Lev4_Sneak` | Sneak Up To The Tower | no | |
| 29 | `Lev4_Sneak2` | Sneak Down From The Tower (also 30 Gryffindor Common Room) | no | |
| 31 | `Lev5_fluffy` | The Forbidden Corridor | no | |
| 32 | `Lev5_Snare` | The Devil's Snare | no | |
| 33 | `Lev5_FlyKeys` | The Winged Keys | no | |
| 34 | `Lev5_Chess` | The Chess Game (also 35 The Potions Puzzle) | no | |
| 36 | `Lev5_Final` | The Final Encounter (also 37) | no | |
| 38 | `Snapes_Office` | The End | no | |

Quidditch League (main menu → Quidditch), indices 40-48: `Quid_SlythA`/`B`/`C`, `Quid_RavenA`/`B`/`C`,
`Quid_HuffleA`/`B`/`C`: none played yet.

Other maps: `Entry` and `startup` (the title menu's background), not levels.

## Automated route through Lev_Tut1

`HP1_GOTO` waypoints ([debug-tools.md](debug-tools.md)) that take Harry through Lev_Tut1 (`--url=Lev_Tut1`): stairs →
Ron cutscene → door D1stA → CutScene52 → Fred & George's room (~126 s) → bookcase climb (`HP1_KEYS="137:Up:2.5"`) →
shelves along the jelly-bean trail (-32,-4470; 600,-4470; 768,-4432; 864,-3952; 1056,-3744) → climbexit trigger
(1541,-3749) → jumping-help cutscene → jump room (2080,-3808; 2304,-3824; 2288,-3456; 2640,-3488; 2650,-3392,J;
2650,-3150 = the west balcony) → through the west arch past the candle stand (2656,-3020; 2656,-2944) → corridor
(2656,-2790; 3136,-2790) → east arch (3136,-2960; 3150,-3030) → jump down onto box C (3150,-3068,J; 3150,-3300) →
box D (3150,-3346,J; 3150,-3640) → south ledge (3150,-3748,J; 3150,-3930) → jumpexit doors (3136,-4100).
