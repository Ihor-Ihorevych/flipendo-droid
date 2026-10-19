# Contributing to Flipendo

There's work for every skill level, and much of it needs no programming.

## Ways to help

- **Playtesters**: play a level, compare it with the original game, report what looks or behaves differently.
  Side-by-side screenshots or short clips help most. Attach the log (`--logfile=flipendo.log`).
- **Owners of other game versions**: see [below](#other-game-versions).
- **Modders**: build an extra in `src/hp1/mods/` ([docs/modding.md](docs/modding.md)).
- **C++ developers**: start with [docs/development.md](docs/development.md) (layout, ground rules, reference
  material), [docs/one-engine.md](docs/one-engine.md) (one code path for both games) and the [roadmap](ROADMAP.md).
- **Writers and artists**: documentation, guides, screenshots and clips for the README.

Reverse engineering isn't open to outside help. What was learned is in [docs/re/](docs/re/README.md).

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
`build/Release/SurrealEngine.exe --autolaunch --logfile=hp1.log <game folder>` launches straight into the game with a
log ([docs/development.md](docs/development.md)).

### Linux

Tested on Fedora 44 (GCC 16, Wayland through XWayland). Development packages (Fedora names; other distributions in
SurrealEngine's [`src/engine/Docs/Building.md`](src/engine/Docs/Building.md)):

```sh
sudo dnf install cmake ninja-build gcc-c++ libX11-devel libxkbcommon-devel alsa-lib-devel openal-soft-devel
```

```sh
bash tools/apply_patches.sh                                         # tools/build.sh is Windows-only (VS generator)
cmake -S src/engine -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target SurrealEngine
build-linux/SurrealEngine --autolaunch --logfile=hp1.log ~/Games/HarryPotter
```

Linux file names are case-sensitive: the game saves into `Save/` (`SavePath=../Save` in the ini). A folder copied
from Windows may have it as `save/`; rename it, or saves made there won't show up.

### macOS

Tested on Apple Silicon (macOS 26, Xcode's command-line tools and Ninja; CMake fetches MoltenVK, OpenAL is bundled):

```sh
bash tools/apply_patches.sh
cmake -S src/engine -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --target SurrealEngine
build-mac/SurrealEngine.app/Contents/MacOS/SurrealEngine --autolaunch --logfile=hp1.log ~/Games/HarryPotter
```

The game renders at the display's native (Retina) resolution.

### Developer flags

| Flag | What it does |
|---|---|
| `--autolaunch` | boot the first detected game, no launcher and no modal error boxes |
| `--logfile=<path>` | stream the log to a file (survives crashes) |
| `--level=<map>` | start in a level as a player reaches it in the story: story flow, earlier cards, average beans and house points, save slot 0 (`--slot=<0-5>` for another) ([docs/debug-tools.md](docs/debug-tools.md#starting-a-level---level)); use this for playtesting |
| `--url=<map>` | only load a level, with a new game's state and outside the story flow (the flying lesson and the Quidditch matches then run as their menu versions) |
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

[`CLAUDE.md`](CLAUDE.md) is the full rule set (it doubles as the guide for AI assistants).

## SurrealEngine and AI-assisted code

Much of this port is written with LLM assistance (Claude). SurrealEngine asks that LLM-assisted changes are
**not** sent to it as pull requests (`src/engine/NO-AI Code Rule.md`), so our engine changes stay here as
`src/surreal-patches/*.patch`, applied by the build; each patch is headed as Flipendo's change, which marks the altered source as the zlib licence requires
([docs/engine-hooks.md](docs/engine-hooks.md)). Please don't report Flipendo problems to SurrealEngine.

## Licence

By contributing you agree that your contribution is licensed under the
[GNU General Public License, version 3](LICENSE), like the rest of Flipendo's own code (`src/knowwonder/`, `src/hp1/`,
`src/hp2/`, `src/surreal-patches/`, `tools/`, `docs/`).
