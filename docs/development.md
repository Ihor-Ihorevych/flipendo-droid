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
| `tools/` | The build scripts (`build.sh`, `apply_patches.sh`). |
| `docs/` | This documentation ([index](README.md)); [`docs/re/`](re/) holds the reverse-engineering notes and reports. |
| `images/` | Logos (`images/branding/`) and the README screenshots (`images/screenshots/`). |

Next to the repository (not in it): `../eagames/hp1` and `../eagames/hp2` are the retail game folders (never modified),
`../eagames/hp1-work` the disposable copy runs use, and `../ida/` the IDA databases (`hp1/` and `hp2/`, never mixed: `../ida/README.md`).

How code is split between `src/knowwonder/`, `src/hp1/` and `src/hp2/` so that each behaviour exists once: [one-engine.md](one-engine.md).

**Engine hooks** call into `src/knowwonder/` (`src/knowwonder/KW.h`) gated by `engine->LaunchInfo.IsKnowWonder()`, which is HP1 only for
now; HP1-only hooks (mods, menu canvas) call `src/hp1/HP1.h` gated by `IsHarryPotter1()`. HP2 joins `IsKnowWonder()`
once its differences are handled.

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

## README screenshots

The images in `images/screenshots/` (and `social-preview.jpg`) are taken in a 1920x1080 window. To retake or add one,
film the level with `HP1_SHOTS` every second or two and pick the frame.
