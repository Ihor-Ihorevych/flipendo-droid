# Development

How the code is laid out, where the knowledge comes from, and the tools for working on the port. Build and run
basics are in [CONTRIBUTING.md](../CONTRIBUTING.md#building).

## Layout

| Path | What |
|---|---|
| `src/knowwonder/` | **KnowWonder's engine**, reimplemented: what the Harry Potter games' modified `Engine.dll` / `Fire.dll` / `Render.dll` do differently from stock Unreal (skeletal animation, ParticleFX, Wind, Gesture, physics and pawn movement, collision, interpolation, save games, IceTexture, natives), the widescreen canvas for the menus (`KWCanvas.cpp`) plus the debug tools. Namespace `KW`, files `KW*.cpp`. |
| `src/hp1/` | **HP1 only**: the optional extras (`src/hp1/mods/`, [modding.md](modding.md)) and `--level`. |
| `src/hp2/` | **HP2 only**: HP2's own natives and bytecode differences, and adapters where HP2's script signatures differ. |
| `src/tests/` | Unit tests for code that needs no game data (`FlipendoTests`, [below](#tests)). |
| `src/flipendo.cmake` | Adds `src/knowwonder/`, `src/hp1/` and `src/hp2/` to SurrealEngine's build (one line in `src/engine/CMakeLists.txt`). |
| `src/engine/` | [SurrealEngine](https://github.com/dpjudas/SurrealEngine), our engine dependency, as a git submodule. Never committed to. |
| `src/surreal-patches/` | Our changes to SurrealEngine, applied to `src/engine/` by the build, split by topic ([engine-hooks.md](engine-hooks.md)). |
| `tools/` | The build scripts (`build.sh`, `apply_patches.sh`), the checks and smoke tests ([below](#tests)), packaging (`release/`). |
| `docs/` | This documentation ([index](README.md)); [`docs/re/`](re/) holds the reverse-engineering notes and reports. |
| `images/` | Logos (`images/branding/`) and the README screenshots (`images/screenshots/`). |

Next to the repository (not in it): `../eagames/hp1` and `../eagames/hp2` are the retail game folders (never modified),
`../eagames/hp1-work` the disposable copy runs use, and `../ida/` the IDA databases (`hp1/` and `hp2/`, never mixed: `../ida/README.md`).

How code is split between `src/knowwonder/`, `src/hp1/` and `src/hp2/` so that each behaviour exists once: [one-engine.md](one-engine.md).

**Engine hooks** call into `src/knowwonder/` (`src/knowwonder/KW.h`) gated by `engine->LaunchInfo.IsKnowWonder()` (HP1 and
HP2); HP1-only hooks (mods, menu canvas) call `src/hp1/HP1.h` gated by `IsHarryPotter1()`, HP2-only ones `src/hp2/HP2.h`
gated by `IsHarryPotter2()`.

The ground rules are in [CONTRIBUTING.md](../CONTRIBUTING.md#ground-rules), the full set in [`CLAUDE.md`](../CLAUDE.md).

## Reference material

- **The games' own scripts.** All gameplay packages are pure UnrealScript. HP1's `.u` files embed their source text
  with KnowWonder's comments (1247 classes); HP2 stripped most of it, so those classes are decompiled from bytecode
  (1749 classes, 1602 decompiled). Extract them from your own install with
  [UELib](https://github.com/EliotVU/Unreal-Library) (never commit them); UELib also dumps every object of a package with its properties (e.g. all actors of
  a map, cutscene scripts). Use the scripts
  from your own disc, not other exports floating around (they are different builds).
- **Reverse-engineering notes** in [`re/`](re/) ([index](re/README.md)).
- Harry Potter modding community resources:
  [HarryPotterUnrealWiki](https://github.com/metallicafan212/HarryPotterUnrealWiki/wiki/Main-Resources).

## Debug tools

Environment variables for screenshots, scripted input, actor dumps and traces, fixed cameras and console commands at
set times, plus crash reports and the script debugger: [debug-tools.md](debug-tools.md).

## Tests

`.github/workflows/ci.yml` runs on every push to `master` and every pull request, without the games' files:

- `tools/check_repo.py`: the rules in [`CLAUDE.md`](../CLAUDE.md) a script can check. Every patch in
  `src/surreal-patches/` starts with its `routes.txt` description and changes only files routed to it, no added engine
  line carries a `flipendo:` marker, no game data or extracted script is tracked, and every `// IDA ` tag names its
  DLL and game (`[HP1 0x...]`, `[HP2 0x...]`) or says it isn't verified.
- `tools/apply_patches.sh` on the pinned SurrealEngine.
- The game and `FlipendoTests` built on Windows, Linux and macOS, and the tests run; on Linux the tests also run under
  AddressSanitizer and UBSan.

**Unit tests** (`src/tests/`) cover code that works on plain values: the XA decoder, ALAudio's volumes, `ReadBMP`, the
view and canvas scaling, `FGetHSV`, `OrthoRotation`, `TwoWallAdjust`. `TEST(name)` and the `CHECK` macros are in
`src/tests/Test.h`; a new `src/tests/*.cpp` is picked up when CMake runs again. The tests link `SurrealCommon` like the
game, so they can call anything in `src/knowwonder/`, `src/hp1/` and `src/hp2/` that doesn't need a loaded game:

```sh
cmake --build build-mac --target FlipendoTests
ctest --test-dir build-mac --output-on-failure      # or build-mac/FlipendoTests [name filter]
```

**Smoke tests** need HP1, so they run locally: `tools/smoke_test.py <HP1 folder>` plays short scripted runs with the
[debug tools](debug-tools.md) and checks their logs (the game reaches the main menu and quits; `--level=Lev_Tut1` skips
the opening cutscene, Harry runs and the game saves; the main menu loads that save with Harry where it was made).
Give it a disposable copy like `../eagames/hp1-work`: the runs write its ini files and save slot 5. It finds the game
in `build/`, `build-mac/` or `build-linux/` (`--exe` for another), `--only <name>` runs one test, and the logs are in
`build/smoke/`. Run it before sending a change to the engine or the scripts' natives.

## README screenshots

The images in `images/screenshots/` (and `social-preview.jpg`) are taken in a 1920x1080 window. To retake or add one,
film the level with `HP1_SHOTS` every second or two and pick the frame.

## Releases

`.github/workflows/release.yml` builds Windows (x64), Linux (x86-64, Ubuntu 24.04) and macOS (Apple Silicon) on
GitHub's runners and zips each with `tools/release/package.sh` (the binaries, licenses and the platform's
`tools/release/README-first-<platform>.txt`). Run it by hand from the Actions tab to get the zips as build artifacts;
pushing a tag (`hp*` or `v*`, e.g. `hp1-stable-hp2-beta1`) also makes a draft release with the three zips, to check
and publish on GitHub. The macOS app is ad-hoc signed, not notarised (the README-first says how to open it).
