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
| `HP1_GOTO` | `"79:x,y;x,y,J;x,y,w3"` | steer the player through waypoints (`J` jump on arrival, `w3` wait 3 s, `\|` starts another run later) |
| `HP1_TRACE` | `"harry0,gen_"` | log actors by name prefix every 0.5 s: state, zone, location, velocity, rotation, animation |
| `HP1_DUMP` | `"5,80"` | log every actor (class, name, state, location, Tag, Event) at those times |
| `HP1_CAMERA` | `"x,y,z,pitch,yaw"` | look from a fixed camera |
| `HP1_HEIGHTMAP` | `"12:x0,y0,x1,y1,step,ztop"` | floor heights over a grid, for planning jumps and climbs |
| `HP1_SKIPCUTS` | `1` | press Space whenever a cutscene holds Harry (the CutsceneSkip mod fast-forwards it) |
| `HP1_BACKGROUND` | `1` | open the window windowed, behind the other windows and without taking focus, so automated runs don't take over the screen |
| `HP1_EXEC` | `"3:@console.MenuBook.SlotPage LoadSelectedSlot"` | commands at those times, `;` separated (below) |

### `HP1_EXEC` commands

| Command | What it does |
|---|---|
| `open save99.usa`, `SaveGame 3`, ... | any console command |
| `@console[.Prop] Fn [arg]` | call a script function on the console or an object it references, e.g. `@console.MenuBook OpenBook Slot` |
| `@console.MenuBook.SlotPage LoadSelectedSlot` | load a save from the main menu (slot 99 when none is selected); a bare `open saveN.usa` leaves the menu book open over the game |
| `@console SaveSelectedSlot` | save (slot 99 without a selected slot) |
| `@set <actor prefix> <prop> <value>` | set a property on live actors, e.g. `@set CutScene3 bDebugScript True` |
| `@get <actor prefix> <prop>` | log a property, e.g. `@get harry numBeans` |
| `@teleport x y z` | move the player there (touches what is there, so it starts touch cutscenes); the game camera comes along |
| `@trigger <tag>` | trigger every actor with that Tag |
| `@bump <actor> <other>` | raise `Bump(other)` on every actor whose name starts with `<actor>` (push a GridMover without aiming a spell: `@set GridMover0 bProjTarget False` first, so it takes the player as the bumper) |
| `@polys <actor>` | log the brush polygons of every actor whose name starts with `<actor>`: normal, PolyFlags (0x1000 = mountable), texture |
| `@sweep sx sy sz ex ey ez bx by bz` | KnowWonder's BSP line check from start to end with a box of half size b, point checks at both ends (zone, leaf, push-out), and every actor the box sweep hits (MultiLineCheck), and the hit node's coplanar polygons with their flags and vertices: debugging collision and ledge grabs |
| `@hull <actor>` | log every collision leaf hull of the actor's brush: its planes in world space (node, flipped) and its local box: where a mover is solid |

## Crashes and the script debugger

Crashes leave a minidump and `<dump>.txt` with the symbolized call stack in `%LOCALAPPDATA%\SurrealEngine\CrashReports`
(build `RelWithDebInfo` for symbols). `SurrealDebugger` (an UnrealScript debugger: breakpoints, call stack,
disassembly) builds alongside the game.

## Script tools

- `tools/uelib_dump props <package> <file>`: every object in a package with its properties (for example all actors of
  a map, or the cutscene command lists).
- `tools/extract_scripts.sh [hp1|hp2]`: the games' scripts with KnowWonder's comments (HP1) or decompiled (HP2), into
  `reference/<game>/ScriptSource/`.
