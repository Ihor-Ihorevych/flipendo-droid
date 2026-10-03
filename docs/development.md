# Development

How the code is laid out, where the knowledge comes from, and the tools for working on the port. Build and run
basics are in the [README](../README.md#getting-started).

## Layout

| Path | What |
|---|---|
| `hp1/` | The HP1 port: native functions, animation, particles, physics, collision. Built into the engine via `hp1/hp1.cmake`. |
| `hp1/mods/` | Additions the original game doesn't have ([modding.md](modding.md)). |
| `engine/` | [SurrealEngine](https://github.com/dpjudas/SurrealEngine), our engine dependency, as a git submodule. Never committed to. |
| `patches/` | Our small changes to SurrealEngine, applied to `engine/` by the build ([engine-hooks.md](engine-hooks.md)). |
| `tools/` | Build, run, audit and extraction scripts. |
| `docs/` | This documentation; `docs/re/` holds the reverse-engineering notes. |
| `reference/` | Scripts extracted from your own install. **Gitignored** (derived from the game). |

## Ground rules

- **Never use Epic's UE1 source or headers** as a reference, including header sets built from them. Layouts and
  behaviour come from HP1's own binaries and scripts, SurrealEngine, and observing the original game.
- **Never commit game data** or anything extracted from it (`*.u *.unr *.utx *.uax *.umx`, exes, DLLs,
  `reference/`, decompiled code).
- **HP1 code goes in `hp1/`**; SurrealEngine files only get small hooks ([engine-hooks.md](engine-hooks.md)).
- **Every reimplemented function carries an IDA tag** directly above it, one line per original function:
  ```cpp
  // IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
  ```
  The decorated name is the key: it survives rebuilds and finds the same function in HP2's DLLs. Code written
  from script comments or stock UE1 behaviour instead of reversing says so in the tag.
- **Write down what you learn** in `docs/re/<topic>.md` in the same change.

[`CLAUDE.md`](../CLAUDE.md) has the full rule set (it doubles as the guide for AI assistants).

## Reference material

```sh
tools/extract_scripts.sh   # -> reference/hp1/ScriptSource/ (needs the .NET 10 SDK)
python tools/native_audit.py
```

- **The game's own scripts.** All HP1 gameplay packages are pure UnrealScript, and the `.u` files embed their
  source text with KnowWonder's comments. `tools/extract_scripts.sh` extracts it from your install with
  [UELib](https://github.com/EliotVU/Unreal-Library) (`tools/Unreal-Library` submodule, our extractor in
  `tools/uelib_dump/`): 1247 classes. `tools/uelib_dump props <package> <file>` dumps every object of a package with
  its properties (e.g. all actors of a map). Use the scripts from your own disc, not other exports floating around
  (they are different builds).
- **The native audit.** `tools/native_audit.py` compares the natives the scripts declare with what SurrealEngine
  and `hp1/` implement, and writes [`native_audit.md`](native_audit.md) (MISSING / STUB / INDEX).
- **The original binaries.** The native code is in KnowWonder's modified `Engine.dll` (plus bits of `Core.dll` and
  `Render.dll`). They aren't SafeDisc-wrapped and export decorated C++ names, so functions can be found by name in
  IDA or Ghidra.
- **Reverse-engineering notes** in [`re/`](re/): [animation](re/animation.md), [particles](re/particles.md),
  [script events](re/script_events.md) (every event HP1's native code raises, and which ones SurrealEngine
  doesn't).
- Harry Potter modding community resources:
  [HarryPotterUnrealWiki](https://github.com/metallicafan212/HarryPotterUnrealWiki/wiki/Main-Resources).

## Porting a native

1. Find it in [`native_audit.md`](native_audit.md), or from an `Unimplemented: Class.Function` line in the log.
2. Read its declaration and callers in `reference/hp1/ScriptSource/<Package>/Classes/`.
3. Reverse the original in `Engine.dll` by its decorated name (e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
   A script state that never gets entered is usually an event the engine doesn't raise: check
   [script_events.md](re/script_events.md) first.
4. Implement it in `hp1/`: register it from `HP1::RegisterNatives()` with `OverrideNative(index, ...)`
   (`hp1/HP1Natives.cpp`), and add the IDA tag.
5. Rebuild, run `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audit, and note what you
   learned in `docs/re/`.

## Debug tools

Environment variables read by `hp1/HP1Debug.cpp`. Times are seconds since the first frame.

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
| `HP1_EXEC` | `"66:@console SaveSelectedSlot;70:open save99.usa"` | console commands at those times (`;` separates); `@console[.Prop] Fn [arg]` calls a script function on the console (or an object it references) with an optional string, e.g. `@console.MenuBook OpenBook Slot` |

The first level hands control to the player at about 58 s. To walk up to Fred & George's room:

```sh
HP1_KEYS="62:Up:5" HP1_GOTO="79:-400,-2000;-104,-2016;-20,-2016;140,-2016;232,-2095;225,-2887" \
  tools/run_hp1.sh 140 --url=Lev_Tut1
```

`SurrealDebugger` (an UnrealScript debugger: breakpoints, call stack, disassembly) builds alongside the game.
