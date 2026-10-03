# CLAUDE.md — hp1_re

Source port of Harry Potter and the Philosopher's Stone (PC 2001, UE1 build 433) on a
SurrealEngine fork. Read `README.md` first for layout, build and run commands.

## Rules

- **Never open a PR or push to SurrealEngine upstream.** Upstream bans LLM-written PRs
  (`engine/NO-AI Code Rule.md`). The `upstream` remote is fetch-only for us.
- **Mark every change under `engine/` with an `hp1_re:` comment** (zlib licence requires altered
  source to be marked). Gate HP1-only behaviour behind `engine->LaunchInfo.IsHarryPotter1()` so
  other UE1 games keep working.
- Don't push to `origin` without explicit permission.
- Never modify `../harry-potter-unpacked/` or `../harry-potter/` (pristine retail copies).
  Runs use `../game-work/` (disposable copy, SurrealEngine writes ini/save files into it).
- Don't download binaries or install tools without asking.

## Ground truth / facts established

- Game: KnowWonder UE1 build **433, Unicode**. Package file version **76** (`.u`), maps **72** (`.unr`).
- `System/HP.exe` on disc is **SafeDisc-wrapped** (sections `stxt774`/`stxt371`, EP in `stxt371`);
  `drvmgt.dll`/`secdrv.sys` are SafeDisc. Ignore them — the launcher is a thin WinMain anyway.
- **All HP gameplay packages (`HarryPotter`, `HPBase`, `HPMenu`, `Hub*`, `Tut*`, ...) are pure
  UnrealScript** — zero native functions, zero native classes. All native work is in
  KnowWonder's modified `Engine.dll` (+ small bits of `Core.dll`).
- Native classes HP1 has that SurrealEngine doesn't know at all: `AnimChannel`, `ClipMarker`,
  `GameSaveInfo`, `Gesture` (spell-drawing recognition), `ImpactSoundSet`, `InterpolationManager`,
  `LocationID`, `ParticleFX`, `SoundContainer`, `Wind`.
- HP1 uses its own native indices in places (e.g. `Actor.PlayAnim` = 259, `TraceTexture` = 285).

## Workflow for porting a native

1. `python tools/native_audit.py` → `docs/native_audit.md` lists MISSING / STUB / INDEX natives.
3. Reverse the real implementation in IDA from `../harry-potter-unpacked/System/Engine.dll`
   (find it by its exported/decorated name, e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
4. Implement in the engine:
   - registration: `engine/SurrealEngine/Native/N<Class>.cpp` `RegisterFunctions()`
     (`RegisterVMNativeFunc_<argc>("Class", "Name", &NClass::Fn, nativeIndex)`), inside an
     `IsHarryPotter1()` block when HP-specific;
   - behaviour: usually a method on the `U<Class>` object in `engine/SurrealEngine/Packages/...`;
   - script-visible fields: `engine/SurrealEngine/PropertyOffsets.cpp/.h`.
5. Rebuild, `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audit.

## Where things are in SurrealEngine

- Game detection: `UE1GameDatabase.h` (SHA-1 of `System/HP.exe`), `GameFolder.cpp/.h`
  (`IsHarryPotter1()` = exe stem `"HP"`).
- Existing HP1 hooks: grep `IsHarryPotter1` (NActor, NObject, UStruct/Bytecode
  `DynArrayToInt_HP1`, UAnimation, RenderCanvas, Engine.cpp resolution hack).
- Logging: `LogMessage` / `LogUnimplemented` (`Utils/Logger.h`). Missing natives log
  `Unimplemented: Class.Fn` at runtime.
- `SurrealDebugger` (console UnrealScript debugger: breakpoints, callstack, disassembly) and
  `SurrealEditor` build alongside the game.

## Build / run

- `tools/build.sh [Release|Debug|RelWithDebInfo] [target]` — VS 18 (2026) generator, x64, output in
  `build/<Config>/`. A full build takes a few minutes; `--target SurrealEngine` for the game only.
- Rebuilding fails if `SurrealEngine.exe` is running (file lock) — check `tasklist` first, don't
  kill a game the user started.
- `tools/run_hp1.sh [secs]` — `--autolaunch --logfile=build/hp1_run.log` against `../game-work`.
