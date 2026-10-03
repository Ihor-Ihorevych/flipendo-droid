# Modding

Flipendo keeps a strict line between the **faithful port** and **additions**:

- `hp1/` (except `hp1/mods/`) reproduces what the 2001 engine did. Every function carries an `// IDA` tag saying
  which original function it reimplements.
- `hp1/mods/` holds features the original never had. They never change vanilla behaviour unless switched on,
  and `--vanilla` switches off all the ones that are on by default.

## Built-in mods

| Mod | Default | What it does |
|---|---|---|
| `CutsceneSkip.cpp` | on | "Press Space to skip" during cutscenes; Space fast-forwards to the end |
| `StorybookSkip.cpp` | on | "Press Space to skip" in storybooks (New Game intro, chapter interludes) |
| `LaunchSkips.cpp` | off (`--skip-splash`, `--skip-intro`) | skip the splash screens; skip the New Game storybook |

## Writing a mod

A mod is a C++ file in `hp1/mods/`. The build picks up new files automatically (`hp1/hp1.cmake` globs `hp1/`).
Mods hook in only through three calls in `hp1/mods/HP1Mods.cpp`:

| Hook | Called | Use it for |
|---|---|---|
| `HP1::TickMods` | every frame, after the console tick | reading and changing game state |
| `HP1::ModsKeyDown` | on every key press in the game window | key handling, also in menus |
| `HP1::PostRenderMods` | after the HUD and menus are drawn | drawing on top of everything |

Steps:

1. Add `hp1/mods/MyMod.cpp` with a tick function and, if it draws, a draw function.
2. Declare them in `HP1Mods.h` and call them from `TickMods` / `PostRenderMods` in `HP1Mods.cpp`.
3. Gate it: `Mods::Enabled()` for a mod that is on by default (off with `--vanilla`), or
   `Mods::HasFlag("--my-flag")` for an opt-in one.
4. Add it to the table above and to `hp1/mods/README.md`.

Helpers in `HP1Mods.h`: `ObjectProperty` / `BoolProperty` read script properties by name, `SpacePressed` reports a
key press since the last tick, `DrawPrompt` draws a small hint line in the corner.

Work through the game's own script state (properties, script functions) where you can, so a mod stays a thin
layer over vanilla behaviour. To find what to touch, read the game's scripts: `tools/extract_scripts.sh` writes
them, with KnowWonder's comments, to `reference/hp1/ScriptSource/` (see [development.md](development.md)).
`CutsceneSkip.cpp` is a short example that follows a script's state and calls into it.

If a mod needs a hook the three above don't give, add it to `HP1Mods.h` and to
[engine-hooks.md](engine-hooks.md); mods don't add engine hooks of their own.

## Tools for modders

- The debug environment variables ([development.md](development.md#debug-tools)): dump every actor with its
  state, trace actors over time, put the camera somewhere fixed, script key presses and player movement.
- `SurrealDebugger`, built next to the game: an UnrealScript debugger with breakpoints, call stacks and
  disassembly.
- `tools/uelib_dump props <package> <file>`: every object in a package with its properties (for example all actors
  of a map).

## Where modding is going

Phase 6 of the [roadmap](../ROADMAP.md):

- **Per-mod settings** in an ini section and an *Extras* page in the options book, instead of only command-line
  flags.
- **Drop-in content mods**: a `Mods/<name>/` folder whose packages (`.u`, `.unr`, `.utx`, `.uax`) load ahead of
  the originals, with no recompiling, for texture packs, custom levels and script mods. SurrealEngine already reads
  the package folders from the ini, and the first folder that has a package wins, which is the mechanism this
  needs.
- **Script mods** that replace a game class with their own subclass (a new Harry, a new spell) without touching
  the original packages.
- **Community content**: run the custom levels the HP1 modding community has made.
- **More built-in extras**: field-of-view slider, frame limiter / uncapped framerate, controller support,
  speedrun timer, free camera.
