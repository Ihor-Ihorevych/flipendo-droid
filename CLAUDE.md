# CLAUDE.md — hp1_re

Source port of Harry Potter and the Philosopher's Stone (PC 2001, UE1 build 433) on a
SurrealEngine fork. Read `README.md` first for layout, build and run commands.

## Rules

- **Never open a PR or push to SurrealEngine upstream.** Upstream bans LLM-written PRs
  (`engine/NO-AI Code Rule.md`).
- **`engine/` is a git submodule (upstream `github.com/dpjudas/SurrealEngine`). Never edit its
  files as a change of their own and never commit inside it.** Engine changes exist only as
  `patches/*.patch`, applied to the submodule working tree by `tools/apply_patches.sh` (run by
  `tools/build.sh`). To change a hook: edit the patched file in `engine/`, run
  `tools/refresh_patches.sh`, commit `patches/`. Temporary debug edits in `engine/` must be reverted
  (`tools/apply_patches.sh --reset`) before refreshing patches.
- **Features the original game doesn't have go in `hp1/mods/`** (see its README): optional, off with
  `--vanilla`, hooked only through `HP1::TickMods`/`ModsKeyDown`/`PostRenderMods`. The rest of `hp1/` is the faithful port.
- **HP1 code goes in `hp1/`, not in `engine/`.** `engine/` is upstream SurrealEngine (actively
  developed); every line we change there is a future merge conflict. Engine files only get small
  hooks that call into `hp1/` (see the table in `ROADMAP.md`). Prefer overriding natives from
  `hp1/HP1Natives.cpp` (`OverrideNative`) and side tables over editing upstream classes.
- **Every function in `hp1/` that reimplements engine code carries an IDA tag** directly above its
  definition, one line per original function, in exactly this format (greppable with `// IDA `):
  ```cpp
  // IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
  // IDA Core.dll: ?SlerpQuat@@YA?AVFQuat@@ABV1@0M@Z [HP1 Core 0x1014F5A0]
  ```
  The decorated export name is the key (it survives rebuilds and lets the same code be found in HP2's
  DLLs); the address is the real body (`..._0`) in our HP1 database. For non-exported functions write
  `not exported: sub_XXXXXXXX [HP1 0xXXXXXXXX]` plus how to find it again (exported caller/callee, string,
  xref). If the code was not reversed (written from script comments or stock UE1 behaviour), say so in
  the tag ("NOT yet verified against ..."). Add the tag in the same change that adds the function.
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

## Where we are

`ROADMAP.md` has the phase checklist; update it when something lands. Reverse-engineering notes go
in `docs/re/<topic>.md`.

## IDA

- Database: `../ida/Engine.dll.i64` (a copy of the retail DLL; never open the one in
  `../harry-potter-unpacked/`, IDA writes files next to it). Driven headless via the IDA MCP.
- Engine.dll was built with incremental linking: `?Foo@...` at 0x103xxxxx is a `jmp` thunk, the real body
  is the `..._0` name. Set `this`/arg types on the `_0` function before decompiling.
- FArchive vtable: +4 Serialize, +20 CountBytes, +24 `<<UObject*`, +28 `<<FName`.
- **Decompiled dump** (search here first, before decompiling in IDA): `../ida/decomp/<Engine|Core|Render>/<ADDR>_<name>.c`,
  one file per function, headed with the decorated name and `[HP1 0x...]` (the `// IDA` tag key); index
  `../ida/decomp/<Dll>_index.tsv`. Regenerate with `tools/ida_dump.py` inside IDA (`dump_all('Engine')`). Not in the repo
  (derived from EA's binaries).
- `docs/re/script_events.md`: every script event HP1's native code raises, and which ones SurrealEngine never
  raises (the `Mount` kind of gap). Check it when a script state never gets entered.

## Workflow for porting a native

1. `python tools/native_audit.py` → `docs/native_audit.md` lists MISSING / STUB / INDEX natives.
3. Reverse the real implementation in IDA from `../harry-potter-unpacked/System/Engine.dll`
   (find it by its exported/decorated name, e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
4. Implement in `hp1/`:
   - registration: in a `Register*Natives()` called from `HP1::RegisterNatives()`, using
     `OverrideNative(index, [] { RegisterVMNativeFunc_<argc>("Class", "Name", &Fn, index); })`
     (upstream may already have a stub at that index);
   - HP-only Actor properties: accessors in `hp1/HP1Actor.h` (offsets looked up by name);
   - only if there's no other way, a gated `hp1_re:` hook in an engine file, added to the ROADMAP table;
   - the `// IDA <dll>: <decorated name> [HP1 0x...]` tag above every reimplemented function (see Rules).
5. Rebuild, `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audit.

## Where things are in SurrealEngine

- Game detection: `UE1GameDatabase.h` (SHA-1 of `System/HP.exe`), `GameFolder.cpp/.h`
  (`IsHarryPotter1()` = exe stem `"HP"`).
- Existing HP1 hooks: grep `IsHarryPotter1` (NActor, NObject, UStruct/Bytecode
  `DynArrayToInt_HP1`, UAnimation, RenderCanvas widescreen canvas, Engine.cpp `getres`).
- Logging: `LogMessage` / `LogUnimplemented` (`Utils/Logger.h`). Missing natives log
  `Unimplemented: Class.Fn` at runtime.
- `SurrealDebugger` (console UnrealScript debugger: breakpoints, callstack, disassembly) and
  `SurrealEditor` build alongside the game.

## Build / run

- `tools/update_engine.sh [--check|ref]` — move the `engine/` submodule to a newer upstream commit and
  re-apply `patches/`. `tools/apply_patches.sh [--reset]`, `tools/refresh_patches.sh` for the patch set.
- `tools/build.sh [Release|Debug|RelWithDebInfo] [target]` — VS 18 (2026) generator, x64, output in
  `build/<Config>/`. A full build takes a few minutes; `--target SurrealEngine` for the game only.
- Rebuilding fails if `SurrealEngine.exe` is running (file lock). The user is fine with Claude killing it
  (`taskkill //IM SurrealEngine.exe //F`) to rebuild or relaunch.
- `tools/run_hp1.sh [secs] [args]` — `--autolaunch --logfile=build/hp1_run.log` against `../game-work`.
  `--skip-splash` goes straight to the main menu, `--skip-intro` skips the New Game storybook,
  `--vanilla` disables the default-on mods (`hp1/mods/`).
- Debug env vars (`hp1/HP1Debug.cpp`, times in seconds since the first frame): `HP1_SHOTS="5,8.5"` +
  `HP1_SHOT_DIR` for in-engine screenshots (never capture the desktop), `HP1_KEYS="62:Up:3,66:Left:0.6"`
  to press keys (the Lev_Tut1 intro hands control to the player at ~58 s), `HP1_MOUSE="63:0:-30:4"` to move the
  mouse by dx,dy raw counts every frame for a duration (dy<0 = mouse up = camera looks up), `HP1_TRACE="harry0,gen_"` to
  log actors by name prefix every 0.5 s (state, zone, location, velocity, rotation, anim, tween, pawn speed/input),
  `HP1_CAMERA="x,y,z,pitch,yaw"` to look at something from a fixed camera (e.g. a particle effect).
  `HP1_DUMP="5,80"` logs every actor (class, name, state, location, Tag, Event) at those times (find triggers,
  doors, cutscenes); `HP1_GOTO="79:x,y;x,y,J;x,y,w3|126:..."` steers the player through waypoints (`J` = jump on
  arrival, `w3` = stop and wait 3 s; `|` starts another run at a later time) and logs `goto reached` / `goto stuck`.
  Lev_Tut1 up to Fred & George's room: `HP1_KEYS="62:Up:5" HP1_GOTO="79:-400,-2000;-104,-2016;-20,-2016;140,-2016;232,-2095;225,-2887"`.
