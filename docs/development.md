# Development

How the code is laid out, where the knowledge comes from, and the tools for working on the port. Build and run
basics are in the [README](../README.md#getting-started).

## Layout

| Path | What |
|---|---|
| `kw/` | **KnowWonder's engine**, reimplemented: what the Harry Potter games' modified `Engine.dll` / `Fire.dll` / `Render.dll` do differently from stock Unreal (skeletal animation, ParticleFX, Wind, Gesture, physics and pawn movement, collision, interpolation, save games, IceTexture, natives) plus the debug tools. Namespace `KW`, files `KW*.cpp`. |
| `hp1/` | **HP1 only**: the optional extras (`hp1/mods/`, [modding.md](modding.md)) and the widescreen canvas for HP1's menu classes (`HP1Canvas.cpp`). |
| `flipendo.cmake` | Adds `kw/` and `hp1/` to SurrealEngine's build (one `flipendo:` line in `engine/CMakeLists.txt`). |
| `engine/` | [SurrealEngine](https://github.com/dpjudas/SurrealEngine), our engine dependency, as a git submodule. Never committed to. |
| `patches/` | Our changes to SurrealEngine, applied to `engine/` by the build, split by topic ([engine-hooks.md](engine-hooks.md)). |
| `tools/` | Build, run, extraction, audit and comparison scripts. |
| `docs/` | This documentation; [`docs/re/`](re/) holds the reverse-engineering notes and generated reports. |
| `reference/` | Scripts and map dumps extracted from your own installs (`reference/hp1/`, `reference/hp2/`). **Gitignored**. |

Next to the repository (not in it): `../eagames/hp1` and `../eagames/hp2` are the retail game folders (never modified),
`../eagames/hp1-work` the disposable copy runs use (made by `tools/run_hp1.sh`), and `../ida/` the IDA databases.

**Engine hooks** call into `kw/` (`kw/KW.h`) gated by `engine->LaunchInfo.IsKnowWonder()`, which is HP1 only for
now; HP1-only hooks (mods, menu canvas) call `hp1/HP1.h` gated by `IsHarryPotter1()`. HP2 joins `IsKnowWonder()`
once its differences are handled ([re/hp2_compare.md](re/hp2_compare.md)).

## Ground rules

- **Never use Epic's UE1 source or headers** as a reference, including header sets built from them. Layouts and
  behaviour come from the games' own binaries and scripts, SurrealEngine, and observing the original games.
- **Never commit game data** or anything extracted from it (`*.u *.unr *.utx *.uax *.umx`, exes, DLLs,
  `reference/`, decompiled code).
- **Our code goes in `kw/` (shared) or `hp1/` (HP1 only)**; SurrealEngine files only get small hooks
  ([engine-hooks.md](engine-hooks.md)).
- **Every reimplemented function carries an IDA tag** directly above it, one line per original function:
  ```cpp
  // IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
  ```
  The decorated name is the key: it survives rebuilds and finds the same function in HP2's DLLs
  ([re/hp2_compare.md](re/hp2_compare.md), [re/dlls.md](re/dlls.md) are built from these tags). Code written from
  script comments or stock UE1 behaviour instead of reversing says so in the tag.
- **Write down what you learn** in `docs/re/<topic>.md` in the same change.

[`CLAUDE.md`](../CLAUDE.md) has the full rule set (it doubles as the guide for AI assistants).

## Reference material

```sh
tools/extract_scripts.sh [hp1|hp2]      # -> reference/<game>/ScriptSource/ (needs the .NET 10 SDK)
python tools/native_audit.py [hp1|hp2]  # -> docs/re/native_audit_<game>.md
python tools/dll_report.py              # -> docs/re/dlls.md
```

- **The games' own scripts.** All gameplay packages are pure UnrealScript. HP1's `.u` files embed their source text
  with KnowWonder's comments (1247 classes); HP2 stripped most of it, so those classes are decompiled from bytecode
  (1749 classes, 1602 decompiled). `tools/extract_scripts.sh` uses [UELib](https://github.com/EliotVU/Unreal-Library)
  (`tools/Unreal-Library` submodule, our extractor in `tools/uelib_dump/`). `tools/uelib_dump props <package> <file>`
  dumps every object of a package with its properties (e.g. all actors of a map, cutscene scripts). Use the scripts
  from your own disc, not other exports floating around (they are different builds).
- **The DLLs.** [re/dlls.md](re/dlls.md): what each DLL does, its exports, what we reimplemented and HP2 status.
  The native code is in KnowWonder's `Engine.dll` (plus bits of `Core.dll`, `Fire.dll`, `Render.dll`); they aren't
  SafeDisc-wrapped and export decorated C++ names, so functions can be found by name in IDA or Ghidra.
- **The native audits.** [re/native_audit_hp1.md](re/native_audit_hp1.md) compares the natives the scripts declare
  with what SurrealEngine, `kw/` and `hp1/` implement (MISSING / STUB / INDEX / OK); the HP2 one adds HP1_PORT.
- **Reverse-engineering notes** in [`re/`](re/) ([index](README.md)).
- Harry Potter modding community resources:
  [HarryPotterUnrealWiki](https://github.com/metallicafan212/HarryPotterUnrealWiki/wiki/Main-Resources).

## Porting a native

1. Find it in [native_audit_hp1.md](re/native_audit_hp1.md), or from an `Unimplemented: Class.Function` line in the log.
2. Read its declaration and callers in `reference/hp1/ScriptSource/<Package>/Classes/`.
3. Reverse the original in `Engine.dll` by its decorated name (e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
   A script state that never gets entered is usually an event the engine doesn't raise: check
   [script_events.md](re/script_events.md) first.
4. Implement it in `kw/`: register it from `KW::RegisterNatives()` (`kw/KWNatives.cpp`) with
   `OverrideNative(index, ...)`, and add the IDA tag.
5. Rebuild, run `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audit, and note what you
   learned in `docs/re/`.

## Debug tools

Environment variables read by `kw/KWDebug.cpp`. Times are seconds since the first frame.

| Variable | Example | What it does |
|---|---|---|
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

`HP1_EXEC` entries:

| Command | What it does |
|---|---|
| `open save99.usa`, `SaveGame 3`, ... | any console command |
| `@console[.Prop] Fn [arg]` | call a script function on the console or an object it references, e.g. `@console.MenuBook OpenBook Slot` |
| `@console.MenuBook.SlotPage LoadSelectedSlot` | load a save from the main menu (slot 99 when none is selected); a bare `open saveN.usa` leaves the menu book open over the game |
| `@console SaveSelectedSlot` | save (slot 99 without a selected slot) |
| `@set <actor prefix> <prop> <value>` | set a property on live actors, e.g. `@set CutScene3 bDebugScript True` |
| `@get <actor prefix> <prop>` | log a property, e.g. `@get harry numBeans` |
| `@teleport x y z` | move the player there (touches what is there, so it starts touch cutscenes) |
| `@trigger <tag>` | trigger every actor with that Tag |

Crashes leave a minidump and `<dump>.txt` with the symbolized call stack in `%LOCALAPPDATA%\SurrealEngine\CrashReports`
(build `RelWithDebInfo` for symbols). `SurrealDebugger` (an UnrealScript debugger: breakpoints, call stack,
disassembly) builds alongside the game.

## Comparing with HP2

HP2 (retail 1.0, `../eagames/hp2`) runs on the same engine build (433). To see which ports carry over:

1. Open each DLL in IDA (HP1: `../ida/<Dll>.dll(.i64)`, HP2: copies in `../ida/hp2/`).
2. In each database run `exec(open(r'<repo>/tools/ida_fingerprint.py').read()); fingerprint(r'../ida/fingerprints/<hp1|hp2>_<Dll>.json')`.
3. `python tools/hp2_compare.py` writes [re/hp2_compare.md](re/hp2_compare.md): per DLL how many exports are identical,
   differ only in struct offsets/constants, or changed, and the same for every `// IDA`-tagged function;
   `python tools/dll_report.py` refreshes [re/dlls.md](re/dlls.md).

Native `exec*` wrappers mostly show as changed by about 29 bytes each; that looks systematic (not yet checked), so
read the HP2 code before treating one of those as a real change.
