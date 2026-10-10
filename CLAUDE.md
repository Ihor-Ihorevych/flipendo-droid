# CLAUDE.md — Flipendo

Source port of KnowWonder's Harry Potter games on SurrealEngine (a dependency: git submodule + small patches): one
engine for Harry Potter and the Philosopher's Stone (PC 2001, UE1 build 433) and Chamber of Secrets (HP2, same engine
build). HP1 is the current focus, HP2 follows on the same code. Read `README.md` and `docs/development.md` first for
layout, build and run commands, and the next section before writing any engine code.

## HP1, HP2 and src/knowwonder/: one engine, no duplication

Every change keeps the three apart and never creates a second copy of a behaviour, HP1-only work included: code
written for HP1 today is the code HP2 runs tomorrow. The rules with examples: `docs/one-engine.md`. In short:

- `src/knowwonder/` (`KW`) is the default home: everything both games do. `src/hp1/` / `src/hp2/` only for what the
  other game lacks; check first (`docs/re/reports/hp2_compare.md`, the other game's `reference/<game>/ScriptSource/`).
- One implementation per behaviour. Never read fields at fixed offsets (script properties by name, `KWActor.h`).
- A real difference is a `KW::IsHP1()` / `KW::IsHP2()` branch at the line that differs, never a copied function; a
  changed script signature gets a thin adapter calling the shared body.
- Most "changed" functions in `hp2_compare.md` differ only by HP2's DebugInfo check: read the code before branching.
- Game checks: `KW::IsHP1/IsHP2()` inside `src/knowwonder/`; engine hooks gated by `IsKnowWonder()` (into
  `src/knowwonder/`), `IsHarryPotter1()` / `IsHarryPotter2()` (into `src/hp1/` / `src/hp2/`). None spread through `src/engine/`.
- After adding or moving a port, rerun `python tools/hp2_compare.py`, `python tools/native_audit.py hp1` / `hp2` and
  `python tools/dll_report.py`; a decorated name tagged in two folders is a duplicate to fix.
- Never mix the games' binaries or IDA databases (`../eagames/hp1|hp2/System`, `../ida/hp1|hp2/`, fingerprints
  `hp1_` / `hp2_`); check which database is open before reading an address.

## Rules

- **Never open a PR or push to the SurrealEngine project.** SurrealEngine bans LLM-written PRs
  (`src/engine/NO-AI Code Rule.md`).
- **`src/engine/` is a git submodule (SurrealEngine, `github.com/dpjudas/SurrealEngine`). Never edit its
  files as a change of their own and never commit inside it.** Engine changes exist only as
  `src/surreal-patches/*.patch`, applied to the submodule working tree by `tools/apply_patches.sh` (run by
  `tools/build.sh`). To change a hook: edit the patched file in `src/engine/`, run
  `tools/refresh_patches.sh`, commit `src/surreal-patches/`. Temporary debug edits in `src/engine/` must be reverted
  (`tools/apply_patches.sh --reset`) before refreshing patches.
- **Features the original game doesn't have go in `src/hp1/mods/`** (`docs/modding.md`): optional, off with
  `--vanilla`, hooked only through `HP1::TickMods`/`ModsKeyDown`/`PostRenderMods`/`ModsTimeScale`. Everything else is the faithful port.
- **Our code goes in `src/knowwonder/`, `src/hp1/` or `src/hp2/` (previous section), not in `src/engine/`.** `src/knowwonder/` files are `KW*.cpp`,
  `src/hp1/` `HP1*.cpp`, `src/hp2/` `HP2*.cpp`. `src/engine/` is SurrealEngine (actively developed); every line we change there
  is a future merge conflict. Engine files only get small hooks that call into `src/knowwonder/`/`src/hp1/`/`src/hp2/` (see
  `docs/engine-hooks.md`). Prefer overriding natives from `src/knowwonder/KWNatives.cpp` (`OverrideNative`) and side tables over
  editing SurrealEngine classes.
- **Every function in `src/knowwonder/`/`src/hp1/`/`src/hp2/` that reimplements engine code carries an IDA tag** directly above its
  definition, one line per original function, in exactly this format (greppable with `// IDA `):
  ```cpp
  // IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
  // IDA Core.dll: ?SlerpQuat@@YA?AVFQuat@@ABV1@0M@Z [HP1 Core 0x1014F5A0]
  ```
  The decorated export name is the key (it survives rebuilds and lets the same code be found in HP2's
  DLLs); the address is the real body (`..._0`) in that game's database. Say which game: `[HP1 0x...]`,
  `[HP2 0x...]` (`[HP2 Core 0x...]` for Core.dll); a shared `src/knowwonder/` function whose HP2 code differs gets both
  (`[HP1 0x...] [HP2 0x...]`), and never one tag per game on two copies of the function. For non-exported functions write
  `not exported: sub_XXXXXXXX [HP1 0xXXXXXXXX]` plus how to find it again (exported caller/callee, string,
  xref). If the code was not reversed (written from script comments or stock UE1 behaviour), say so in
  the tag ("NOT yet verified against ..."). Add the tag in the same change that adds the function.
- **No `flipendo:` markers in engine code.** Our engine changes exist only as `src/surreal-patches/*.patch`, each headed
  `flipendo: ...` (from `src/surreal-patches/routes.txt`); that marks the altered source as the zlib licence requires. Comments in
  engine changes explain the code like any other comment. Gate hooks into `src/knowwonder/` behind `engine->LaunchInfo.IsKnowWonder()` (HP1 only for now; HP2
  joins when its differences are handled, `docs/re/reports/hp2_compare.md`), HP1-only behaviour behind
  `IsHarryPotter1()` and HP2-only behaviour behind `IsHarryPotter2()`, so other UE1 games keep working. Fixes to SurrealEngine bugs that affect every game go
  ungated in `src/surreal-patches/0010-engine-fixes.patch` (`src/surreal-patches/routes.txt` assigns files to patches).
- **Never commit `reference/` or game data** (`*.u *.unr *.utx *.uax *.umx`, exes, DLLs).
  The extracted scripts are derived from the game; `.gitignore` covers them.
- **Never use Epic's UE1 source or headers as a reference**, including third-party header sets built
  from them (e.g. the "HP1 public headers" on archive.org). Layouts and behaviour come from HP1's own
  binaries and scripts (IDA, `.u` files), SurrealEngine, and observing the original game. Don't copy
  Epic type or field names that don't appear in HP1's exports or scripts; name things yourself.
- **Write down what you learn in `docs/re/`.** Whenever reversing, debugging or reading scripts teaches
  something new about how the games work (a struct layout, a native's behaviour, an event the engine raises, a level
  route, why a script state is never entered), add it to the matching note, or start a new one, in the same change.
  Sorted like the code: `docs/re/engine/` for KnowWonder's engine (what both games do; each note says which game was
  checked and has an "HP1 and HP2" part), `docs/re/hp1/` / `docs/re/hp2/` for what only one game has. Index:
  `docs/re/README.md`. Don't leave findings only in commit messages or the conversation.
- **Flipendo is licensed GPLv3** (`LICENSE`, the unmodified GNU text); `src/engine/` stays zlib. Until 2026-10-05
  it was PolyForm Noncommercial 1.0.0 (commits before that keep it).
- Don't push to `origin` without explicit permission. `origin` is the Gitea server; it mirrors to GitHub
  (`github.com/kroplabeskidu/flipendo`, the README's clone URL) automatically, so a push to `origin` is all it takes.
- The repository is `hp_re/flipendo`. Never modify `../eagames/hp1/` or `../eagames/hp2/` (pristine retail copies of HP1 and HP2).
  Runs use `../eagames/hp1-work/` (disposable copy, SurrealEngine writes ini/save files into it; `tools/run_hp1.sh`
  creates it from `../eagames/hp1` when it's missing).
- Don't download binaries or install tools without asking.

## Ground truth / facts established

- Game: KnowWonder UE1 build **433, Unicode**. Package file version **76** (`.u`), maps **72** (`.unr`).
- The retail disc (readme says "Version 1.0", files dated 2001-10-29) **already contains the 1.1
  script fixes** — every 1.1 marker checked is present (`IsOSVer2kOrXP`, `CreateNativeFont`,
  `FEOptionsPage.IsSupportedResolution`, ...). No need for the official patch. Other script exports floating
  around are different builds (a few classes differ); only read ours.
- **Our scripts come from our own disc**: the `.u` files embed the original source text (with comments), which
  `tools/extract_scripts.sh` writes to `reference/hp1/ScriptSource/<Pkg>/Classes/` (UELib, `tools/Unreal-Library`
  submodule + `tools/uelib_dump`), 1247 classes. The defaultproperties blocks are generated (UELib formatting:
  enums as numbers, all struct fields written).
- `System/HP.exe` on disc is **SafeDisc-wrapped** (sections `stxt774`/`stxt371`, EP in `stxt371`);
  `drvmgt.dll`/`secdrv.sys` are SafeDisc. Ignore them — the launcher is a thin WinMain anyway.
- The engine DLLs are **not** wrapped and **export decorated C++ names** (Core 2046, Engine 2722,
  Window 1289 exports).
- **All HP gameplay packages (`HarryPotter`, `HPBase`, `HPMenu`, `Hub*`, `Tut*`, ...) are pure
  UnrealScript** — zero native functions, zero native classes. All native work is in
  KnowWonder's modified `Engine.dll` (+ small bits of `Core.dll`).
- Native classes HP1 has that SurrealEngine doesn't know at all: `AnimChannel`, `ClipMarker`,
  `GameSaveInfo`, `Gesture` (spell-drawing recognition), `ImpactSoundSet`, `InterpolationManager`,
  `LocationID`, `ParticleFX`, `SoundContainer`, `Wind`.
- HP1 uses its own native indices in places (e.g. `Actor.PlayAnim` = 259, `TraceTexture` = 285).
- **HP2** (retail 1.0, `../eagames/hp2`, `System/Game.exe`): same engine build 433, recognised by SurrealEngine. Its
  DLLs are HP1's with additions: Fire.dll 100% identical code, Core 82%, Engine 62% identical + 14% differing only in
  struct offsets (`docs/re/reports/hp2_compare.md`). Galaxy audio is replaced by OpenAL (`ALAudio.dll`) + Ogg Vorbis. Its
  `.u` files ship with most script source stripped (only ~150 of 827 `hgame` classes keep it); the rest is decompiled
  by `tools/extract_scripts.sh hp2`. Game code package: `hgame`.

## Where things are documented

Each file has one job; keep them apart:

- `README.md`: for players (what, why, screenshots, status, how to play, extras, community). Keep it short, plain
  and free of developer detail; link to `CONTRIBUTING.md` and `docs/`.
- `CONTRIBUTING.md`: for contributors (ways to help, building, developer flags, ground rules, the AI/SurrealEngine note).
- `ROADMAP.md`: the phase checklist of open work and what's next. Remove an item when it lands (no done items). No how-it-works detail.
  Per-level status and bugs are not repeated there, nor the reversing order (`docs/surrealengine-coverage.md`).
- `docs/playtest.md`: the only place for per-level status; notes hold only what is still open in a level and the
  original's own quirks. A fixed bug leaves it (how the fix works goes in `docs/re/`).
- `docs/`: all other documentation, how things work (index: `docs/README.md`). Player-facing error messages in
  `docs/troubleshooting.md`; modding in `docs/modding.md`, debug env vars in `docs/debug-tools.md`; workflow in
  `docs/development.md`, the src/knowwonder/hp1/hp2 split in `docs/one-engine.md`, every SurrealEngine hook in
  `docs/engine-hooks.md`; reverse-engineering notes in `docs/re/` (`engine/`, `hp1/`, `hp2/`). No READMEs or docs in
  code folders.
- Generated reports in `docs/re/reports/` (rerun the tool, don't edit by hand): `dlls.md` (`tools/dll_report.py`: every
  DLL, its exports, our ports, HP2 status), `native_audit_hp1.md`/`native_audit_hp2.md` (`tools/native_audit.py
  [hp1|hp2]`), `hp2_compare.md` (`tools/hp2_compare.py`).
- `images/`: `branding/` (logo, icon) and `screenshots/` (README gallery, social preview).

## IDA

- Databases: `../ida/hp1/<Dll>.dll(.i64)` for HP1 (Engine, Core, Fire, Render, Galaxy), `../ida/hp2/<Dll>.dll` for HP2
  (copies of the retail DLLs; never open the ones in `../eagames/`, IDA writes files next to them). Driven headless
  via the IDA MCP. HP1 vs HP2: `tools/ida_fingerprint.py` inside each database -> `../ida/fingerprints/`, then
  `tools/hp2_compare.py` and `tools/dll_report.py`.
- Types: define structs in IDA from what the binary shows: the sizeof each class registers, field accesses in
  the decompiled code, `Serialize` order, and the script property layout from our `.u` files.
- Engine.dll was built with incremental linking: `?Foo@...` at 0x103xxxxx is a `jmp` thunk, the real body
  is the `..._0` name. Set `this`/arg types on the `_0` function before decompiling.
- FArchive vtable: +4 Serialize, +20 CountBytes, +24 `<<UObject*`, +28 `<<FName`.
- **Decompiled dump** (search here first, before decompiling in IDA): `../ida/hp1/decomp/<Engine|Core|Render>/<ADDR>_<name>.c`,
  one file per function, headed with the decorated name and `[HP1 0x...]` (the `// IDA` tag key); index
  `../ida/hp1/decomp/<Dll>_index.tsv`. Regenerate with `tools/ida_dump.py` inside IDA (`dump_all('Engine')`). Not in the repo
  (derived from EA's binaries).
- `docs/re/engine/script_events.md`: every script event the native code raises (from HP1's Engine.dll), and which ones SurrealEngine never
  raises (the `Mount` kind of gap). Check it when a script state never gets entered.

## Workflow for porting a native

The steps (audit, scripts, IDA, HP2 check, registration, tag, rerun the audits): `docs/development.md`
("Porting a native"). Start from `docs/re/reports/native_audit_hp1.md` or an `Unimplemented:` line in the log.

## Where things are in SurrealEngine

- Game detection: `UE1GameDatabase.h` (SHA-1 of `System/HP.exe` / HP2's `System/Game.exe`), `GameFolder.cpp/.h`
  (`IsHarryPotter1()` = exe stem `"HP"`, `IsHarryPotter2()` = `"Game"`, our `IsKnowWonder()` = HP1 for now).
- Our hooks: grep `IsKnowWonder` and `IsHarryPotter1` (SurrealEngine's own pre-existing HP1 special cases also use
  `IsHarryPotter1`: NActor, UStruct/Bytecode `DynArrayToInt_HP1`, ...).
- Logging: `LogMessage` / `LogUnimplemented` (`Utils/Logger.h`). Missing natives log
  `Unimplemented: Class.Fn` at runtime.
- `SurrealDebugger` (console UnrealScript debugger: breakpoints, callstack, disassembly) and
  `SurrealEditor` build alongside the game.

## Build / run

- `tools/update_engine.sh [--check|ref]` — move the `src/engine/` submodule to a newer SurrealEngine commit and
  re-apply `src/surreal-patches/`. `tools/apply_patches.sh [--reset]`, `tools/refresh_patches.sh` for the patch set.
- `tools/build.sh [Release|Debug|RelWithDebInfo] [target]` — VS 18 (2026) generator, x64, output in
  `build/<Config>/`. A full build takes a few minutes; `--target SurrealEngine` for the game only.
- Rebuilding fails if `SurrealEngine.exe` is running (file lock). Killing it (`taskkill //IM SurrealEngine.exe //F`)
  is fine for Claude's own runs, never for a game the user is playing (below).
- `tools/run_hp1.sh [secs] [args]` — `--autolaunch --logfile=build/hp1_run.log` against `../eagames/hp1-work`.
  `--level=<map>` starts a level with the story flow and the state a player carries in (playtesting; `--url` only
  loads the map), `--skip-splash` goes straight to the main menu, `--skip-intro` skips the New Game storybook,
  `--vanilla` disables the default-on mods (`src/hp1/mods/`). `HP1_FULLSCREEN=1` runs borderless full screen at
  the desktop size, otherwise windowed at `HP1_WINDOW` (1280x720).
- **Starting the game for the user to playtest by hand:** always `HP1_FULLSCREEN=1`, never the small window. Never
  kill or restart a game the user is playing; test in a second instance (`HP1_BACKGROUND=1`, its own `HP1_LOG`,
  `--slot=5` so their save slot is left alone), and rebuild only once they have quit (the exe is locked).
- Debug env vars and `HP1_EXEC` commands (screenshots, keys, mouse, waypoints, traces, dumps, fixed camera, `@set` /
  `@get` / `@teleport` / `@trigger` / `@travel` ...): `docs/debug-tools.md`, code in `src/knowwonder/KWDebug.cpp`.
  Every automated run uses `HP1_BACKGROUND=1`; screenshots only with `HP1_SHOTS` (never capture the desktop).
