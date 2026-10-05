# Contributing to Flipendo

There's work for every skill level, and much of it needs no programming. Come say hi on
[Discord](README.md#community) first if you're unsure where to start.

## Ways to help

- **Playtesters**: play a level, compare it with the original game, report what looks or behaves differently.
  Side-by-side screenshots or short clips help most. Attach the log (`--logfile=flipendo.log`).
- **Owners of other game versions**: see [below](#other-game-versions).
- **Modders**: build an extra in `src/hp1/mods/`, test community levels, or help design content-mod loading and the
  in-game modding tools ([docs/modding.md](docs/modding.md)).
- **Reverse engineers**: the remaining work is KnowWonder's native code, in HP1's and HP2's DLLs.
  [docs/re/reports/dlls.md](docs/re/reports/dlls.md) shows what every DLL does and what's been reimplemented;
  [docs/re/](docs/re/README.md) has what's been learned so far, the shared engine and each game's own parts.
- **C++ developers**: start with [docs/development.md](docs/development.md) (layout, ground rules, reference
  material), [docs/one-engine.md](docs/one-engine.md) (one code path for both games) and the [roadmap](ROADMAP.md).
- **Writers and artists**: documentation, guides, screenshots and clips for the README and the Discord.

## Other game versions

If your `System/HP.exe` isn't recognised, the error shows its SHA-1. Open an issue with that SHA-1, the language,
and where the copy comes from (retail disc and region, budget re-release, patched, ...). Each new hash makes one
more release work out of the box.

## Building

### Windows

Needs Visual Studio 2026 (v18) with the C++ workload (its bundled CMake is used if `cmake` isn't on `PATH`) and
Git for Windows. From Git Bash:

```sh
git clone --recursive https://github.com/kroplabeskidu/flipendo   # or: git submodule update --init
cd flipendo
tools/build.sh                                                     # Release; binaries in build/Release/
build/Release/SurrealEngine.exe "C:/Games/Harry Potter"            # opens the launcher
```

`tools/build.sh [Release|Debug|RelWithDebInfo] [target]` applies our patches to `src/engine/` and builds.
`tools/run_hp1.sh` launches straight into the game with a log ([docs/development.md](docs/development.md)).

### Linux / macOS

See SurrealEngine's [`src/engine/Docs/Building.md`](src/engine/Docs/Building.md). Not tested with Flipendo yet; reports
are welcome.

### Developer flags

| Flag | What it does |
|---|---|
| `--autolaunch` | boot the first detected game, no launcher and no modal error boxes |
| `--logfile=<path>` | stream the log to a file (survives crashes) |
| `--url=<map>` | start in a level, e.g. `--url=Lev_Tut1` |
| `--skip-splash`, `--skip-intro`, `--vanilla` | as in the [README](README.md#launch-options) |

The debug environment variables (screenshots, actor dumps, fixed cameras, scripted input) are in
[docs/debug-tools.md](docs/debug-tools.md).

## Ground rules

1. **Never use Epic's UE1 source or headers** (or header sets derived from them) as a reference. Layouts and
   behaviour come from the games' own binaries and scripts, SurrealEngine, and observing the original games.
2. **Never commit game data** or anything extracted from it.
3. **Faithful port and additions stay apart.** Anything the original didn't have goes in `src/hp1/mods/` and is off
   with `--vanilla`.
4. **One implementation for both games**: KnowWonder's engine goes in `src/knowwonder/`, only what one game lacks goes in `src/hp1/`
   or `src/hp2/` ([docs/one-engine.md](docs/one-engine.md)).
5. **Write down what you learn** in `docs/re/` in the same change.

[docs/development.md](docs/development.md) explains these, and [`CLAUDE.md`](CLAUDE.md) is the full rule set.

## SurrealEngine and AI-assisted code

Much of this port is written with LLM assistance (Claude). SurrealEngine asks that LLM-assisted changes are
**not** sent to it as pull requests (`src/engine/NO-AI Code Rule.md`), so our engine changes stay here as
`src/surreal-patches/*.patch`, applied by the build; each patch is headed as Flipendo's change, which marks the altered source as the zlib licence requires
([docs/engine-hooks.md](docs/engine-hooks.md)). Please don't report Flipendo problems to SurrealEngine.

## Licence

By contributing you agree that your contribution is licensed under the
[GNU General Public License, version 3](LICENSE), like the rest of Flipendo's own code (`src/knowwonder/`, `src/hp1/`,
`src/hp2/`, `src/surreal-patches/`, `tools/`, `docs/`).
