# Debug tools

For contributors and modders: drive the game from the command line, look inside it, and see what the scripts do.
Use them with `tools/run_hp1.sh` ([development.md](development.md)). The variables are named `HP1_*` because HP1 is the
game that runs today; the code behind them is in the shared `src/knowwonder/KWDebug.cpp`.

## Environment variables

Times are seconds since the first frame.

| Variable | Example | What it does |
|---|---|---|
| `FLIPENDO_SE_PHYSICS` | `1` | run SurrealEngine's physics and movement instead of KnowWonder's own (to compare behaviour) |
| `HP1_SHOTS`, `HP1_SHOT_DIR` | `HP1_SHOTS="5,8.5"` | in-engine screenshots at those times |
| `HP1_KEYS` | `"62:Up:3,66:Left:0.6"` | hold a key from a time for a duration |
| `HP1_MOUSE` | `"63:0:-30:4"` | move the mouse by dx,dy raw counts every frame for a duration (dy<0 looks up) |
| `HP1_GOTO` | `"79:x,y;x,y,J;x,y,w3"` | steer the player through waypoints (`J` jump on arrival, within 8 units so it goes off at a ledge edge; `w3` wait 3 s; `\|` starts another run later); logs `goto reached` / `goto stuck`. A route through Lev_Tut1: [below](#automated-route-through-lev_tut1) |
| `HP1_TRACE` | `"harry0,gen_"` | log actors by name prefix every 0.5 s: state, zone, location, velocity, rotation, animation |
| `HP1_TRACE_INTERVAL` | `"0"` | seconds between `HP1_TRACE` lines (default 0.5; 0 logs every frame, e.g. to see a camera shake) |
| `HP1_DUMP` | `"5,80"` | log every actor (class, name, state, location, Tag, Event) at those times |
| `HP1_CAMERA` | `"x,y,z,pitch,yaw"` | look from a fixed camera |
| `HP1_HEIGHTMAP` | `"12:x0,y0,x1,y1,step,ztop"` | floor heights over a grid, for planning jumps and climbs |
| `HP1_SKIPCUTS` | `1` | press Space whenever a cutscene holds Harry (the CutsceneSkip mod fast-forwards it) |
| `HP1_GOD` | `1` | refill Harry's health (`lifePotions`) to `MaxLifePotions` every tick (UE1's `God` cheat does nothing in HP1: `baseHarry.TakeDamage` ignores `ReducedDamageType`) |
| `HP1_FLY` | `1` | F toggles fly mode: UE1's `PlayerPawn.Fly` cheat (state `CheatFlying`, collision on; Jump goes up, Duck down), F again `Walk`s back to `PlayerWalking` |
| `HP1_BACKGROUND` | `1` | open the window windowed, behind the other windows and without taking focus, so automated runs don't take over the screen |
| `HP1_EXEC` | `"3:@console.MenuBook.SlotPage LoadSelectedSlot"` | commands at those times, `;` separated (below) |

### `HP1_EXEC` commands

| Command | What it does |
|---|---|
| `open save99.usa`, `SaveGame 3`, ... | any console command |
| `@console[.Prop] Fn [arg]` | call a script function on the console or an object it references, e.g. `@console.MenuBook OpenBook Slot`; a number is passed as an int (`@console.MenuBook.SlotPage SetSelectedSlot 0` before `LoadSelectedSlot` loads slot 0 and keeps later saves there) |
| `@console.MenuBook.SlotPage LoadSelectedSlot` | load a save from the main menu (slot 99 when none is selected); a bare `open saveN.usa` leaves the menu book open over the game and `HPConsole.bInHubFlow` false (a Quidditch match then runs in league mode), so for a playtest copy the save to `Save99.usa` and use this |
| `@console SaveSelectedSlot` | save (slot 99 without a selected slot) |
| `@set <actor prefix> <prop> <value>` | set a property on live actors, e.g. `@set CutScene3 bDebugScript True` |
| `@get <actor prefix> <prop>` | log a property, e.g. `@get harry numBeans`. Some properties hang the game (seen 2026-10-10 with `harry0 FlashScale` and a name that isn't a property; not looked into) |
| `@teleport x y z` | move the player there (touches what is there, so it starts touch cutscenes); the game camera comes along |
| `@trigger <tag>` | trigger every actor with that Tag |
| `@state <actor> <state>` | `GotoState(state)` on every actor whose name starts with `<actor>` (reach a script state without playing up to it, e.g. `@state tut3peeves2 dieing`) |
| `@travel <map>` | change level as the game's level exits do (`baseConsole.ChangeLevel(map, true)`: Harry's travel properties come along), e.g. `@travel Lev2_HogFront` |
| `@bump <actor> <other>` | raise `Bump(other)` on every actor whose name starts with `<actor>` (push a GridMover without aiming a spell: `@set GridMover0 bProjTarget False` first, so it takes the player as the bumper) |
| `@polys <actor>` | log the brush polygons of every actor whose name starts with `<actor>`: normal, PolyFlags (0x1000 = mountable), texture, vertices (brush space) |
| `@sweep sx sy sz ex ey ez bx by bz` | KnowWonder's BSP line check from start to end with a box of half size b, point checks at both ends (zone, leaf, push-out), and every actor the box sweep hits (MultiLineCheck), and the hit node's coplanar polygons with their flags and vertices: debugging collision and ledge grabs |
| `@hull <actor>` | log every collision leaf hull of the actor's brush: its planes in world space (node, flipped) and its local box: where a mover is solid |

## Starting a level: `--level`

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

### Automated route through Lev_Tut1

`HP1_GOTO` waypoints ([debug-tools.md](debug-tools.md)) that take Harry through Lev_Tut1 (`--url=Lev_Tut1`): stairs →
Ron cutscene → door D1stA → CutScene52 → Fred & George's room (~126 s) → bookcase climb (`HP1_KEYS="137:Up:2.5"`) →
shelves along the jelly-bean trail (-32,-4470; 600,-4470; 768,-4432; 864,-3952; 1056,-3744) → climbexit trigger
(1541,-3749) → jumping-help cutscene → jump room (2080,-3808; 2304,-3824; 2288,-3456; 2640,-3488; 2650,-3392,J;
2650,-3150 = the west balcony) → through the west arch past the candle stand (2656,-3020; 2656,-2944) → corridor
(2656,-2790; 3136,-2790) → east arch (3136,-2960; 3150,-3030) → jump down onto box C (3150,-3068,J; 3150,-3300) →
box D (3150,-3346,J; 3150,-3640) → south ledge (3150,-3748,J; 3150,-3930) → jumpexit doors (3136,-4100).

## Comparing with the original

`tools/orig_shots.ps1 -Map Lev_Tut2 [-Seconds 28] [-Out dir]` runs the original `HP.exe` from `../eagames/hp1-orig`
straight into a map and saves its window every second (PrintWindow: the game window only, never the desktop). Take
ours at the same times with `HP1_SHOTS="1,2,3,..."`; the original loads a few seconds slower, so pair frames by
content. `hp1-orig`'s `System/HP.ini` skips the first-run wizard (`FirstRun=433`), runs windowed (`StartupFullscreen`,
D3DDrv `UseFullscreen` False) with Direct3D at Brightness 0.4 like ours. A `Running.ini` left by a killed run starts
the original in Recovery Mode; the script deletes it. Its window takes the focus while it runs. The original's `shot`
command wrote nothing (F9 bound in `User.ini`), hence the window capture.

## Crashes and the script debugger

Crashes leave a minidump and `<dump>.txt` with the symbolized call stack in `%LOCALAPPDATA%\SurrealEngine\CrashReports`
(build `RelWithDebInfo` for symbols). `SurrealDebugger` (an UnrealScript debugger: breakpoints, call stack,
disassembly) builds alongside the game.

## Script tools

- `tools/uelib_dump props <package> <file>`: every object in a package with its properties (for example all actors of
  a map, or the cutscene command lists).
- `tools/extract_scripts.sh [hp1|hp2]`: the games' scripts with KnowWonder's comments (HP1) or decompiled (HP2), into
  `reference/<game>/ScriptSource/`.
