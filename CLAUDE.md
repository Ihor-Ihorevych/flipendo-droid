# CLAUDE.md — Flipendo

Source port of KnowWonder's Harry Potter games on SurrealEngine (a dependency: git submodule + small patches): one
engine for Harry Potter and the Philosopher's Stone (PC 2001, UE1 build 433) and Chamber of Secrets (HP2, same engine
build). HP1 is the current focus, HP2 follows on the same code. Read `README.md` and `docs/development.md` first for
layout, build and run commands, and the next section before writing any engine code.

## HP1, HP2 and kw/: one engine, no duplication

Every change must keep the three apart and must not create a second copy of any behaviour. This applies to every
change, also HP1-only work: code written for HP1 today is the code HP2 runs tomorrow.

- **Where code goes:**
  - `kw/` (namespace `KW`): KnowWonder's engine, **everything both games do**. The default home for any port.
  - `hp1/` (namespace `HP1`): only what HP1 has and HP2 doesn't (HP1's menu canvas, HP1's extras in `hp1/mods/`).
  - `hp2/` (namespace `HP2`): only what HP2 has and HP1 doesn't (HP2-only natives, HP2's bytecode token table,
    OpenAL/Ogg music, HP2's menus). Details and examples: `hp2/README.md`.
  - Before putting anything in `hp1/` or `hp2/`, check the other game: `docs/re/hp2_compare.md` (per exported
    function: identical / offsets only / changed / missing / HP2 only) and the other game's scripts in
    `reference/<game>/ScriptSource/`. If the other game has the same thing, it goes in `kw/`.
- **Same behaviour, one implementation.** A function identical in both DLLs, or differing only in struct offsets,
  is written once in `kw/`. Never read fields at fixed offsets: script properties are looked up by name
  (`kw/KWActor.h`), which handles both games' layouts.
- **A real difference is a branch at the exact line that differs**, with `KW::IsHP1()` / `KW::IsHP2()`
  (`kw/KWGame.h`), inside the one shared function. Never copy a function into `hp1/` or `hp2/` to change part of
  it. If the difference is large (a whole different algorithm), split out only that part as a helper and keep the
  shared remainder in `kw/`.
- **A changed script signature gets a thin adapter, not a second body.** The shared body lives in `kw/` as a plain
  function taking the union of both games' parameters; each game's native only unpacks its own arguments and calls
  it (example: `KW::StopSound`, HP1's `NStopSound` in `kw/KWSound.cpp`, HP2's in `hp2/HP2Natives.cpp`).
- **Native registration order:** `KW::RegisterNatives()` registers with HP1's signatures (shared by both games for
  almost every native), then calls `HP2::RegisterNatives()` when HP2 runs, which adds HP2-only natives and
  overrides only the changed ones. HP1-only natives that HP2 lacks are registered from `hp1/` (or gated `IsHP1()`).
- **Read the other game's code before calling something a difference.** Most "changed" functions in
  `hp2_compare.md` differ only by HP2's DebugInfo check after native parameters (`docs/re/hp2_bytecode.md`), which
  is handled once in the bytecode reader, not per native.
- **Game checks:** inside `kw/` use `KW::IsHP1()` / `KW::IsHP2()`; engine hooks into `kw/` are gated by
  `IsKnowWonder()` (both KnowWonder games once HP2 is enabled), hooks into `hp1/` by `IsHarryPotter1()`, into `hp2/`
  by `IsHarryPotter2()`. Don't spread new game checks through `engine/`.
- **Keep the tools in sync:** after adding or moving a port, rerun `python tools/hp2_compare.py`,
  `python tools/native_audit.py hp1` and `python tools/native_audit.py hp2` (and `tools/dll_report.py`); they scan
  `kw/`, `hp1/` and `hp2/`. A duplicate shows up there as the same decorated name tagged in two folders: fix it.
- **Never mix the two games' binaries or databases.** HP1's DLLs come from `../eagames/hp1/System`, HP2's from
  `../eagames/hp2/System` (`Game.exe`); their IDA databases live in separate folders (`../ida/README.md`), and
  fingerprints are prefixed `hp1_` / `hp2_`. Always check which database is open before reading an address.

## Rules

- **Never open a PR or push to the SurrealEngine project.** SurrealEngine bans LLM-written PRs
  (`engine/NO-AI Code Rule.md`).
- **`engine/` is a git submodule (SurrealEngine, `github.com/dpjudas/SurrealEngine`). Never edit its
  files as a change of their own and never commit inside it.** Engine changes exist only as
  `patches/*.patch`, applied to the submodule working tree by `tools/apply_patches.sh` (run by
  `tools/build.sh`). To change a hook: edit the patched file in `engine/`, run
  `tools/refresh_patches.sh`, commit `patches/`. Temporary debug edits in `engine/` must be reverted
  (`tools/apply_patches.sh --reset`) before refreshing patches.
- **Features the original game doesn't have go in `hp1/mods/`** (see its README): optional, off with
  `--vanilla`, hooked only through `HP1::TickMods`/`ModsKeyDown`/`PostRenderMods`. Everything else is the faithful port.
- **Our code goes in `kw/`, `hp1/` or `hp2/` (previous section), not in `engine/`.** `kw/` files are `KW*.cpp`,
  `hp1/` `HP1*.cpp`, `hp2/` `HP2*.cpp`. `engine/` is SurrealEngine (actively developed); every line we change there
  is a future merge conflict. Engine files only get small hooks that call into `kw/`/`hp1/`/`hp2/` (see
  `docs/engine-hooks.md`). Prefer overriding natives from `kw/KWNatives.cpp` (`OverrideNative`) and side tables over
  editing SurrealEngine classes.
- **Every function in `kw/`/`hp1/`/`hp2/` that reimplements engine code carries an IDA tag** directly above its
  definition, one line per original function, in exactly this format (greppable with `// IDA `):
  ```cpp
  // IDA Engine.dll: ?PlayAnim@AActor@@QAEHVFName@@_NMMMW4EAnimType@@0@Z [HP1 0x10408E20]
  // IDA Core.dll: ?SlerpQuat@@YA?AVFQuat@@ABV1@0M@Z [HP1 Core 0x1014F5A0]
  ```
  The decorated export name is the key (it survives rebuilds and lets the same code be found in HP2's
  DLLs); the address is the real body (`..._0`) in that game's database. Say which game: `[HP1 0x...]`,
  `[HP2 0x...]` (`[HP2 Core 0x...]` for Core.dll); a shared `kw/` function whose HP2 code differs gets both
  (`[HP1 0x...] [HP2 0x...]`), and never one tag per game on two copies of the function. For non-exported functions write
  `not exported: sub_XXXXXXXX [HP1 0xXXXXXXXX]` plus how to find it again (exported caller/callee, string,
  xref). If the code was not reversed (written from script comments or stock UE1 behaviour), say so in
  the tag ("NOT yet verified against ..."). Add the tag in the same change that adds the function.
- **Mark every change under `engine/` with a `flipendo:` comment** (zlib licence requires altered
  source to be marked). Gate hooks into `kw/` behind `engine->LaunchInfo.IsKnowWonder()` (HP1 only for now; HP2
  joins when its differences are handled, `docs/re/hp2_compare.md`), HP1-only behaviour behind
  `IsHarryPotter1()` and HP2-only behaviour behind `IsHarryPotter2()`, so other UE1 games keep working. Fixes to SurrealEngine bugs that affect every game go
  ungated in `patches/0010-engine-fixes.patch` (`patches/routes.txt` assigns files to patches).
- **Never commit `reference/` or game data** (`*.u *.unr *.utx *.uax *.umx`, exes, DLLs).
  The extracted scripts are derived from the game; `.gitignore` covers them.
- **Never use Epic's UE1 source or headers as a reference**, including third-party header sets built
  from them (e.g. the "HP1 public headers" on archive.org). Layouts and behaviour come from HP1's own
  binaries and scripts (IDA, `.u` files), SurrealEngine, and observing the original game. Don't copy
  Epic type or field names that don't appear in HP1's exports or scripts; name things yourself.
- **Write down what you learn in `docs/re/`.** Whenever reversing, debugging or reading scripts teaches
  something new about how the game works (a struct layout, a native's behaviour, an event the engine raises, a level
  route, why a script state is never entered), add it to the matching `docs/re/<topic>.md`, or start a new topic
  file, in the same change. Don't leave findings only in commit messages or the conversation.
- **Flipendo is licensed PolyForm Noncommercial 1.0.0** (`LICENSE.md`); `engine/` stays zlib.
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
  struct offsets (`docs/re/hp2_compare.md`). Galaxy audio is replaced by OpenAL (`ALAudio.dll`) + Ogg Vorbis. Its
  `.u` files ship with most script source stripped (only ~150 of 827 `hgame` classes keep it); the rest is decompiled
  by `tools/extract_scripts.sh hp2`. Game code package: `hgame`.

## Where things are documented

Each file has one job; keep them apart:

- `README.md`: for players (what, why, screenshots, status, how to play, extras, community). Keep it short, plain
  and free of developer detail; link to `CONTRIBUTING.md` and `docs/`.
- `CONTRIBUTING.md`: for contributors (ways to help, building, developer flags, ground rules, the AI/SurrealEngine note).
- `ROADMAP.md`: the phase checklist, what's done and next. Update it when something lands. No how-it-works detail.
- `docs/`: how things work (index: `docs/README.md`). Reverse-engineering notes in `docs/re/<topic>.md`, every
  SurrealEngine hook in `docs/engine-hooks.md`, workflow and debug tools in `docs/development.md`, modding in
  `docs/modding.md`, player-facing error messages in `docs/troubleshooting.md`.
- Generated reports in `docs/re/` (rerun the tool, don't edit by hand): `dlls.md` (`tools/dll_report.py`: every
  DLL, its exports, our ports, HP2 status), `native_audit_hp1.md`/`native_audit_hp2.md` (`tools/native_audit.py
  [hp1|hp2]`), `hp2_compare.md` (`tools/hp2_compare.py`).

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
- `docs/re/script_events.md`: every script event HP1's native code raises, and which ones SurrealEngine never
  raises (the `Mount` kind of gap). Check it when a script state never gets entered.

## Workflow for porting a native

1. `python tools/native_audit.py` → `docs/re/native_audit_hp1.md` lists MISSING / STUB / INDEX natives (`hp2` as argument: the same for HP2).
2. Read the UnrealScript declaration and callers in `reference/hp1/ScriptSource/<Pkg>/Classes/` (our disc,
   `tools/extract_scripts.sh`) to get the signature; native-only field layouts come from IDA (step 3).
3. Reverse the real implementation in IDA from `../ida/hp1/Engine.dll` (a copy of `../eagames/hp1/System/Engine.dll`)
   (find it by its exported/decorated name, e.g. `?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z`).
4. Check HP2 first (`docs/re/hp2_compare.md`, `reference/hp2/ScriptSource/`), then implement in `kw/` when both games
   have it (the usual case), `hp1/` or `hp2/` only when the other game doesn't (see "HP1, HP2 and kw/"):
   - registration: in a `Register*Natives()` called from `KW::RegisterNatives()` (`kw/KWNatives.cpp`), using
     `OverrideNative(index, [] { RegisterVMNativeFunc_<argc>("Class", "Name", &Fn, index); })`
     (SurrealEngine may already have a stub at that index);
   - HP-only Actor properties: accessors in `kw/KWActor.h` (offsets looked up by name);
   - only if there's no other way, a gated `flipendo:` hook in an engine file, added to `docs/engine-hooks.md`;
   - the `// IDA <dll>: <decorated name> [HP1 0x...]` tag above every reimplemented function (see Rules).
5. Rebuild, `tools/run_hp1.sh 60`, check the `Unimplemented:` summary, rerun the audit (and `tools/dll_report.py`).

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

- `tools/update_engine.sh [--check|ref]` — move the `engine/` submodule to a newer SurrealEngine commit and
  re-apply `patches/`. `tools/apply_patches.sh [--reset]`, `tools/refresh_patches.sh` for the patch set.
- `tools/build.sh [Release|Debug|RelWithDebInfo] [target]` — VS 18 (2026) generator, x64, output in
  `build/<Config>/`. A full build takes a few minutes; `--target SurrealEngine` for the game only.
- Rebuilding fails if `SurrealEngine.exe` is running (file lock). The user is fine with Claude killing it
  (`taskkill //IM SurrealEngine.exe //F`) to rebuild or relaunch.
- `tools/run_hp1.sh [secs] [args]` — `--autolaunch --logfile=build/hp1_run.log` against `../eagames/hp1-work`.
  `--skip-splash` goes straight to the main menu, `--skip-intro` skips the New Game storybook,
  `--vanilla` disables the default-on mods (`hp1/mods/`).
- Debug env vars (`kw/KWDebug.cpp`, times in seconds since the first frame): `HP1_SHOTS="5,8.5"` +
  `HP1_SHOT_DIR` for in-engine screenshots (never capture the desktop), `HP1_KEYS="62:Up:3,66:Left:0.6"`
  to press keys (the Lev_Tut1 intro hands control to the player at ~58 s), `HP1_MOUSE="63:0:-30:4"` to move the
  mouse by dx,dy raw counts every frame for a duration (dy<0 = mouse up = camera looks up), `HP1_TRACE="harry0,gen_"` to
  log actors by name prefix every 0.5 s (state, zone, location, velocity, rotation, anim, tween, pawn speed/input),
  `HP1_CAMERA="x,y,z,pitch,yaw"` to look at something from a fixed camera (e.g. a particle effect).
  `HP1_DUMP="5,80"` logs every actor (class, name, state, location, Tag, Event) at those times (find triggers,
  doors, cutscenes); `HP1_GOTO="79:x,y;x,y,J;x,y,w3|126:..."` steers the player through waypoints (`J` = jump on
  arrival, within 8 units so it goes off at a ledge edge; `w3` = stop and wait 3 s; `|` starts another run at a later
  time) and logs `goto reached` / `goto stuck`. `HP1_HEIGHTMAP="12:x0,y0,x1,y1,step,ztop"` logs floor heights (player cylinder
  traced down from ztop) over a grid: the way to plan jumps and climbs. `HP1_EXEC="66:@console SaveSelectedSlot;70:open save99.usa"`
  runs console commands, or (`@console[.Prop] Fn [arg]`) script functions on the console / an object it references
  (`@console.MenuBook OpenBook Slot` opens the save slot page); `@set <actor> <prop> <value>` / `@get <actor> <prop>` set or log properties on live actors. `HP1_SKIPCUTS=1` presses Space in cutscenes. `HP1_BACKGROUND=1` keeps the game window in the background (no focus); use it for every automated run.
  Lev_Tut1 up to Fred & George's room: `HP1_KEYS="62:Up:5" HP1_GOTO="79:-400,-2000;-104,-2016;-20,-2016;140,-2016;232,-2095;225,-2887"`.
