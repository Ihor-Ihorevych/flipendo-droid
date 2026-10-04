# Modding

Flipendo keeps a strict line between the **faithful port** and **additions**:

- `kw/` (KnowWonder's engine, shared by the HP games), `hp1/` and `hp2/` reproduce what the original engines did.
  Every function carries an `// IDA` tag saying which original function it reimplements.
- `hp1/mods/` holds features the original never had: quality-of-life changes, developer shortcuts, anything a player
  of the 2001 release never saw. They never change vanilla behaviour unless switched on, and `--vanilla` switches off
  all the ones that are on by default. Mods are HP1's for now; HP2 gets the same once it runs.

## Built-in mods

| Mod | Default | What it does |
|---|---|---|
| `CutsceneSkip.cpp` | on (`--vanilla` disables) | "Press Space to skip" during cutscenes; Space fast-forwards to the end |
| `StorybookSkip.cpp` | on (`--vanilla` disables) | "Press Space to skip" in storybooks (New Game intro, chapter interludes) |
| `LaunchSkips.cpp` | off (`--skip-splash`, `--skip-intro`) | skip the logo/title splash screens; skip the New Game storybook |

## Writing a mod

A mod is a C++ file in `hp1/mods/`. The build picks up new files automatically (`hp1/hp1.cmake` globs `hp1/`). Mods are HP1's: they
read HP1's script classes (`HPBase`, `HPMenu`).
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
4. Add it to the table above.

Rules:

- Mods hook in only through the three calls above, never through engine hooks of their own.
- No `// IDA` tags in a mod (nothing is reversed); say in a comment which script code it relies on.

Helpers in `HP1Mods.h`: `ObjectProperty` / `BoolProperty` read script properties by name, `SpacePressed` reports a
key press since the last tick, `DrawPrompt` draws a small hint line in the corner.

Work through the game's own script state (properties, script functions) where you can, so a mod stays a thin
layer over vanilla behaviour. To find what to touch, read the game's scripts: `tools/extract_scripts.sh` writes
them, with KnowWonder's comments, to `reference/hp1/ScriptSource/` (see [development.md](development.md#reference-material)).
`CutsceneSkip.cpp` is a short example that follows a script's state and calls into it.

If a mod needs a hook the three above don't give, add it to `HP1Mods.h` and to
[engine-hooks.md](engine-hooks.md); mods don't add engine hooks of their own.

## Tools for modders

[debug-tools.md](debug-tools.md): environment variables that dump every actor with its state, trace actors over
time, put the camera somewhere fixed, script key presses and player movement and call script functions; the
`SurrealDebugger` UnrealScript debugger; `tools/uelib_dump` for the objects in a package. How the game works under the
scripts is in the [reverse-engineering notes](re/README.md).

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
- **In-game modding tools**: a [Dear ImGui](https://github.com/ocornut/imgui) overlay (MIT licence) toggled with a
  key, off with `--vanilla` and in release builds unless asked for. It turns the debug environment variables into
  live panels:
  - *Actors*: every actor in the level with class, state, Tag/Event and location (today's `HP1_DUMP`), filterable,
    with "teleport to" and "look at" (`@teleport`, `HP1_CAMERA`);
  - *Inspector*: a selected actor's script properties, editable live (`@set` / `@get`), and its state over time
    (`HP1_TRACE`);
  - *Console*: console commands and script function calls (`HP1_EXEC`), with history;
  - *Mods*: the installed mods, on/off, their settings, load order;
  - later: a ParticleFX editor (all parameters live, export to a script mod), a cutscene step-through, and a
    navigation/collision view for level makers.

  Needs a renderer backend for SurrealEngine's render devices and an input hook in front of the game's own key
  handling; both go through `hp1/` the way mods do, not new engine hooks of their own.
