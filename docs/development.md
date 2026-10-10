# Development

How the code is laid out, where the knowledge comes from, and the tools for working on the port. Build and run
basics are in [CONTRIBUTING.md](../CONTRIBUTING.md#building).

## Layout

| Path | What |
|---|---|
| `src/knowwonder/` | **KnowWonder's engine**, reimplemented: what the Harry Potter games' modified `Engine.dll` / `Fire.dll` / `Render.dll` do differently from stock Unreal (skeletal animation, ParticleFX, Wind, Gesture, physics and pawn movement, collision, interpolation, save games, IceTexture, natives) plus the debug tools. Namespace `KW`, files `KW*.cpp`. |
| `src/hp1/` | **HP1 only**: the optional extras (`src/hp1/mods/`, [modding.md](modding.md)) and the widescreen canvas for HP1's menu classes (`HP1Canvas.cpp`). |
| `src/hp2/` | **HP2 only**: HP2's own natives and bytecode differences, and adapters where HP2's script signatures differ. |
| `src/flipendo.cmake` | Adds `src/knowwonder/`, `src/hp1/` and `src/hp2/` to SurrealEngine's build (one line in `src/engine/CMakeLists.txt`). |
| `src/engine/` | [SurrealEngine](https://github.com/dpjudas/SurrealEngine), our engine dependency, as a git submodule. Never committed to. |
| `src/surreal-patches/` | Our changes to SurrealEngine, applied to `src/engine/` by the build, split by topic ([engine-hooks.md](engine-hooks.md)). |
| `tools/` | Build, run, extraction, audit and comparison scripts. |
| `docs/` | This documentation ([index](README.md)); [`docs/re/`](re/) holds the reverse-engineering notes and reports. |
| `images/` | Logos (`images/branding/`) and the README screenshots (`images/screenshots/`). |
| `reference/` | Scripts and map dumps extracted from your own installs (`reference/hp1/`, `reference/hp2/`). **Gitignored**. |

Next to the repository (not in it): `../eagames/hp1` and `../eagames/hp2` are the retail game folders (never modified),
`../eagames/hp1-work` the disposable copy runs use (made by `tools/run_hp1.sh`), and `../ida/` the IDA databases (`hp1/` and `hp2/`, never mixed: `../ida/README.md`).

How code is split between `src/knowwonder/`, `src/hp1/` and `src/hp2/` so that each behaviour exists once: [one-engine.md](one-engine.md).

**Engine hooks** call into `src/knowwonder/` (`src/knowwonder/KW.h`) gated by `engine->LaunchInfo.IsKnowWonder()`, which is HP1 only for
now; HP1-only hooks (mods, menu canvas) call `src/hp1/HP1.h` gated by `IsHarryPotter1()`. HP2 joins `IsKnowWonder()`
once its differences are handled ([re/reports/hp2_compare.md](re/reports/hp2_compare.md)).

## IDA tags

The ground rules are in [CONTRIBUTING.md](../CONTRIBUTING.md#ground-rules), the full set in [`CLAUDE.md`](../CLAUDE.md).
The one that needs the detail: every function in `src/knowwonder/`, `src/hp1/` or `src/hp2/` that reimplements the
original carries an IDA tag directly above it, one line per original function:

```cpp
// IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
// IDA Core.dll: ?SlerpQuat@@YA?AVFQuat@@ABV1@0M@Z [HP1 Core 0x1014F5A0]
```

The decorated name is the key: it survives rebuilds and finds the same function in HP2's DLLs
([re/reports/hp2_compare.md](re/reports/hp2_compare.md) and [re/reports/dlls.md](re/reports/dlls.md) are built from
these tags). The address is the real body (`..._0`) in that game's database. A shared `src/knowwonder/` function whose
HP2 code differs gets both addresses (`[HP1 0x...] [HP2 0x...]`), never one tag per game on two copies. A function
that isn't exported: `not exported: sub_XXXXXXXX [HP1 0xXXXXXXXX]` and how to find it again. Code written from script
comments or stock UE1 behaviour instead of reversing says so in the tag.

## Reference material

```sh
tools/extract_scripts.sh [hp1|hp2]      # -> reference/<game>/ScriptSource/ (needs the .NET 10 SDK)
python tools/native_audit.py [hp1|hp2]  # -> docs/re/reports/native_audit_<game>.md
python tools/dll_report.py              # -> docs/re/reports/dlls.md
```

- **The games' own scripts.** All gameplay packages are pure UnrealScript. HP1's `.u` files embed their source text
  with KnowWonder's comments (1247 classes); HP2 stripped most of it, so those classes are decompiled from bytecode
  (1749 classes, 1602 decompiled). `tools/extract_scripts.sh` uses [UELib](https://github.com/EliotVU/Unreal-Library)
  (`tools/Unreal-Library` submodule, our extractor in `tools/uelib_dump/`). `tools/uelib_dump props <package> <file>`
  dumps every object of a package with its properties (e.g. all actors of a map, cutscene scripts). Use the scripts
  from your own disc, not other exports floating around (they are different builds).
- **The DLLs.** [re/reports/dlls.md](re/reports/dlls.md): what each DLL does, its exports, what we reimplemented and
  HP2 status. The native code is in KnowWonder's `Engine.dll` (plus bits of `Core.dll`, `Fire.dll`, `Render.dll`); they aren't
  SafeDisc-wrapped and export decorated C++ names, so functions can be found by name in IDA or Ghidra.
- **The native audits.** [re/reports/native_audit_hp1.md](re/reports/native_audit_hp1.md) compares the natives the
  scripts declare with what SurrealEngine, `src/knowwonder/`, `src/hp1/` and `src/hp2/` implement (MISSING / STUB / INDEX / OK); the HP2
  one ([native_audit_hp2.md](re/reports/native_audit_hp2.md)) adds HP1_PORT.
- **Reverse-engineering notes** in [`re/`](re/) ([index](re/README.md)).
- Harry Potter modding community resources:
  [HarryPotterUnrealWiki](https://github.com/metallicafan212/HarryPotterUnrealWiki/wiki/Main-Resources).

## Porting a native

1. Find it in [native_audit_hp1.md](re/reports/native_audit_hp1.md) (or `_hp2`), or from an
   `Unimplemented: Class.Function` line in the log.
2. Read its declaration and callers in `reference/hp1/ScriptSource/<Package>/Classes/`, and HP2's in
   `reference/hp2/ScriptSource/`.
3. Reverse the original in `Engine.dll` by its decorated name (e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
   A script state that never gets entered is usually an event the engine doesn't raise: check
   [script_events.md](re/engine/script_events.md) first.
4. Check HP2 ([hp2_compare.md](re/reports/hp2_compare.md)), then implement it in `src/knowwonder/` (or `src/hp1/` / `src/hp2/` when the
   other game lacks it, [one-engine.md](one-engine.md)):
   - register it in a `Register*Natives()` called from `KW::RegisterNatives()` (`src/knowwonder/KWNatives.cpp`) with
     `OverrideNative(index, [] { RegisterVMNativeFunc_<argc>("Class", "Name", &Fn, index); })` (SurrealEngine may
     already have a stub at that index);
   - HP-only Actor properties: accessors in `src/knowwonder/KWActor.h` (offsets looked up by name);
   - only if there's no other way, a gated hook in an engine file, listed in [engine-hooks.md](engine-hooks.md);
   - the [IDA tag](#ida-tags) above every reimplemented function.
5. Rebuild, run `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audits
   (`python tools/native_audit.py hp1`, `hp2`, `tools/hp2_compare.py`, `tools/dll_report.py`), and note what you
   learned in `docs/re/`.

## Debug tools

Environment variables for screenshots, scripted input, actor dumps and traces, fixed cameras and console commands at
set times, plus crash reports and the script debugger: [debug-tools.md](debug-tools.md).

## README screenshots

`tools/readme_shots.sh` retakes every image in `images/screenshots/` (and `social-preview.jpg`) from fixed recipes:
map, the second the shot is taken at, and whether it's cropped to the cutscene picture. Rerun it after a visual fix
(`tools/readme_shots.sh dumbledore hagrid` for some). It uses its own game copy, `../eagames/hp1-shots`, set to a
1920x1080 window, so `hp1-work` keeps its settings. To add or move a shot, film the level with `HP1_SHOTS` every second
or two, pick the frame, and add a line to the script's list.

## Comparing with HP2

HP2 (retail 1.0, `../eagames/hp2`) runs on the same engine build (433). To see which ports carry over:

1. Open each DLL in IDA (HP1: `../ida/hp1/<Dll>.dll(.i64)`, HP2: copies in `../ida/hp2/`).
2. In each database run `exec(open(r'<repo>/tools/ida_fingerprint.py').read()); fingerprint(r'../ida/fingerprints/<hp1|hp2>_<Dll>.json')`.
3. `python tools/hp2_compare.py` writes [re/reports/hp2_compare.md](re/reports/hp2_compare.md): per DLL how many exports
   are identical, differ only in struct offsets/constants, or changed, and the same for every `// IDA`-tagged function;
   `python tools/dll_report.py` refreshes [re/reports/dlls.md](re/reports/dlls.md).

Native `exec*` wrappers mostly show as changed by about 29 bytes each: that is HP2's DebugInfo check after the
parameters ([re/engine/scripting.md](re/engine/scripting.md#debuginfo-0x38)), not a behaviour change. Read the HP2 code
before treating any "changed" function as a real difference.
