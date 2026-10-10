# Playtest status (HP1)

Every HP1 map in story order, and whether it has been played through in Flipendo. The index and title are the
game's own (`Dobby.int` `n_<map>`, `HPMenu.int` `level_name_<index>`, [re/hp1/menus.md](re/hp1/menus.md)). Titles
without a map of their own (02, 06, 11, 19, 26, 30, 35, 37) are put with the map before them by the order alone,
not checked in the maps.

This file is the one place for per-level status. Notes list only what is still open in a level, and what looks wrong
but is the original's behaviour (so it isn't reported again). A fixed bug leaves the table: how the fix works goes in
`docs/re/`, when it landed is in the git history.

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
| 01 | `Lev_Tut1` | Hogwarts Main Entrance (also 02 Defence Against the Dark Arts Class) | yes | Some cutscene kids look like they walk while running (all play `run` at rate 1.5 with finished tweens; which ones not checked). Kids spawned on the same patrol point can overlap (UE1 Spawn fails when the spot is occupied?). Loading a save here once logged `Tut1McGonagall4 fell out of the world` (not looked into) |
| 03 | `Lev_Tut1b` | Flipendo Challenge | yes | |
| 04 | `Lev_Tut2` | Flying Lesson | yes | Once after Quit from the pause book the main menu seemed to show only Exit (not reproduced) |
| 05 | `Lev_Tut3` | Wingardium Leviosa Lesson (also 06 Challenge) | yes | |
| 07 | `Lev_Tut3b` | Second Floor Landing | yes | The camera flutters for a few frames when Harry lands against a wall (`BaseCam.PositionCamera`). Peeves' missing exit path is the original's, fixed by the PathFixes mod ([re/hp1/original_bugs.md](re/hp1/original_bugs.md)) |
| 08 | `Lev2_HogFront` | Hogwarts Grounds | yes | Logs `ChocolateFrog0 fell out of the world` (not looked into) |
| 09 | `Lev2_Inc_A` | Herbology Class | yes | The spell symbol on a doxie may not match the original ([re/hp1/spells.md](re/hp1/spells.md#not-checked-yet)). As in the original: doxies react only near Harry ([re/engine/physics.md](re/engine/physics.md)); Tut1Gnome9/10 and jelly beans fall out of the world ([re/hp1/original_bugs.md](re/hp1/original_bugs.md)) |
| 10 | `Lev2_Inc_B` | Incendio Challenge (also 11) | yes | |
| 12 | `Lev2_HogFront_2` | Hogwarts Grounds | yes | Logs `H2Crabbe1 fell out of the world` at load (not looked into) |
| 13 | `Lev2_RemChase` | Remembrall Chase | yes | |
| 14 | `Lev2_HogFront_3` | Hogwarts Grounds | yes | Logs `H2Crabbe1 fell out of the world` at load, as Lev2_HogFront_2 |
| 15 | `Lev2_Fire2` | Forest Edge | being played | Doxies attacking and the camera after a reload fixed, not yet checked by hand. Started with `--level` the screen stays black (entered by a level change it draws; not looked into). `rolllog5` and jelly beans fall out of the world |
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
