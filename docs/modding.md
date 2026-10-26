# Modding

Flipendo keeps a strict line between the **faithful port** and **additions**:

- `src/knowwonder/` (KnowWonder's engine, shared by the HP games), `src/hp1/` and `src/hp2/` reproduce what the original engines did.
  Every function carries an `// IDA` tag saying which original function it reimplements.
- `src/hp1/mods/` holds features the original never had: quality-of-life changes, developer shortcuts, anything a player
  of the 2001 release never saw. They never change vanilla behaviour unless switched on, and `--vanilla` switches off
  all the ones that are on by default.

Every mod has an id (the list in `HP1Mods.cpp`, `ModList`). `--mod=<id,...>` switches mods on, `--no-mod=<id,...>`
switches them off (`all` for every mod), and `--list-mods` prints each with its default and whether the given command
line runs it. The strongest wins: an id in `--no-mod`, an id in `--mod`, the mod's older flag (`--classic-movement`,
`--no-discord`, `--skip-splash`, `--skip-intro`, still working), `--no-mod=all` or `--vanilla`, `--mod=all`, then the
default. So `--vanilla --mod=improved-movement` is the original game with only the new controls. Most are HP1's; the Games column says which run in HP2 too (HP2 gets the rest once
  it is playable).

## Built-in mods

| Mod | Id | Games | Default | What it does |
|---|---|---|---|---|
| `CutsceneSkip.cpp` | `cutscene-skip` | HP1 | on | "Press Space to skip" during cutscenes; Space fast-forwards to the end |
| `StorybookSkip.cpp` | `storybook-skip` | HP1 | on | "Press Space to skip" in storybooks (New Game intro, chapter interludes) |
| `DiscordPresence.cpp` | `discord` | HP1 | on (also off with `--no-discord`) | Discord Rich Presence: the level (the game's own level titles), cutscene / paused, Gryffindor's points, play time. Uses Flipendo's Discord application (its name and App Icon); `--discord-app=<id>` uses another |
| `FastForward.cpp` | `fast-forward` | HP1, HP2 | on | hold Shift to run the game 2.5 times as fast (Shift is unbound in HP1) |
| `FrameLimit.cpp` | `frame-limit` | HP1, HP2 | on at 60 fps (`--fps=<n>`; `--fps=0` also turns it off) | waits at the end of each frame's tick so the game runs at most n frames a second: HP1's per-frame movement breaks at high frame rates ([original_bugs.md](re/hp1/original_bugs.md)) |
| `PathFixes.cpp` | `path-fixes` | HP1 | on | adds reach specs the original maps are missing, so `Pawn.FindPath` finds the route the designers meant: Lev_Tut3b's HPath_F3 → baseStation1, Peeves' exit after the duel ([original_bugs.md](re/hp1/original_bugs.md)) |
| `TextureOverrides.cpp` | `texture-overrides` | HP1, HP2 | on | draws a texture from `Textures/<package>/<group>/<name>.dds` in the game folder when that file exists (DXT1/3/5, the layout D3D11Drv texture packs such as the ESRGAN Upscale Pack use); realtime and procedural textures are left alone. Vulkan only |
| `FreeCamera.cpp` | `free-cam` | HP1 | off | independent smoothed mouse orbit, after AdamJD’s camera mod; keeps the original close distance and pulls in at obstacles. With `improved-movement`, movement is relative to the camera and Harry turns towards movement; without it, the original keyboard turning and movement remain. Aiming faces the camera. Uses configured keys and never enables another mod implicitly. |
| `ImprovedMovement.cpp` | `improved-movement` | HP1 | on (also off with `--classic-movement`) | HP2's controls, after AdamJD's Mouse Fix and Strafing mod: Left / Right strafe (the mouse turns, in the air too), Harry keeps moving while a spell is held (the mouse aims it up and down; he can jump with it or start one in the air, but doesn't climb until it is cast), the mouse looks up and down while Harry moves, not only standing still, a ledge caught mid-jump is climbed at once, and AdamJD's smaller tweaks: half the sideways air control, slower blends, the idle while sliding with no input, spells hitting a pawn within 128 at once ([controls.md](re/hp1/controls.md)) |
| `LoadingScreenHold.cpp` | `loading-screen-hold` | HP1 | on | keeps the loading screen (the level's title and objective on the parchment) up for at least 3.5 s, load time included; on today's machines it otherwise flashes by |
| `LaunchSkips.cpp` | `splash-skip`, `intro-skip` | HP1, HP2 | splash skip on (`--skip-splash` turns it on with `--vanilla` too); intro skip off (`--skip-intro`, HP1) | skip the logo/title splash screens (HP2: the EA and WB splashes before its front end); skip the New Game storybook |

## Writing a mod

A mod is a C++ file in `src/hp1/mods/`. The build picks up new files automatically (`src/hp1/hp1.cmake` globs `src/hp1/`). Mods are HP1's: they
read HP1's script classes (`HPBase`, `HPMenu`). The exceptions read no scripts and run for HP2 too: `TextureOverrides.cpp`,
`FastForward.cpp` (`ModsTimeScale`), `FrameLimit.cpp` (the only mod `TickMods` runs for HP2) and the splash skip
(`HP1::ModsSkipSplash`, asked by HP2's front end); their hooks are gated `IsKnowWonder()`.
Mods hook in only through these calls (the first four in `src/hp1/mods/HP1Mods.cpp`):

| Hook | Called | Use it for |
|---|---|---|
| `HP1::TickMods` | every frame, after the console tick | reading and changing game state |
| `HP1::ModsKeyDown` | on every key press in the game window | key handling, also in menus |
| `HP1::PostRenderMods` | after the HUD and menus are drawn | drawing on top of everything |
| `HP1::ModsTimeScale` | every frame, when the frame's game time is worked out | running the game faster or slower (multiplies the frame time; leaves `LevelInfo.TimeDilation`, which save games store, alone) |
| `HP1::ModsSkipSplash` | by HP2's front end, before Game.exe's EA and WB splashes | skipping them |
| `HP1::LoadTextureOverride` | when the render device uploads a texture (Vulkan) | replacing a texture's image (the only mod hook below the scene code) |

Steps:

1. Add `src/hp1/mods/MyMod.cpp` with a tick function and, if it draws, a draw function.
2. Declare them in `HP1Mods.h` and call them from `TickMods` / `PostRenderMods` in `HP1Mods.cpp`.
3. Add a line to `ModList` in `HP1Mods.cpp` (id, on or off by default, games, summary) and gate the mod with
   `Mods::IsOn("my-mod")`. No flags of its own: `--mod=` / `--no-mod=` switch it.
4. Add it to the table above (and to the README's extras for players).

Rules:

- Mods hook in only through the calls above, never through engine hooks of their own.
- No `// IDA` tags in a mod (nothing is reversed); say in a comment which script code it relies on.

Helpers in `HP1Mods.h`: `ObjectProperty` / `BoolProperty` read script properties by name, `SpacePressed` reports a
key press since the last tick, `DrawPrompt` draws a small hint line in the corner. `WrapScriptFunction` puts a native
in place of one of a class's script functions (its own or one of its states', for every object of the class from then
on) and hands back the script function, which the native can still run with `Frame::Call`: a mod that has to change a
value in the middle of a script's frame does it around the script instead of copying it (`ImprovedMovement.cpp` wraps
Harry's `PlayerMove`, `ProcessMove`, `Mount` and `DoJump`). Install it from `TickMods` only when the mod is on, so
`--vanilla` runs the scripts untouched.

Work through the game's own script state (properties, script functions) where you can, so a mod stays a thin
layer over vanilla behaviour. To find what to touch, read the game's scripts, extracted with KnowWonder's
comments (see [development.md](development.md#reference-material)).
`CutsceneSkip.cpp` is a short example that follows a script's state and calls into it.

If a mod needs a hook the four above don't give, add it to `HP1Mods.h` and to
[engine-hooks.md](engine-hooks.md); mods don't add engine hooks of their own.

## Tools for modders

[debug-tools.md](debug-tools.md): environment variables that dump every actor with its state, trace actors over
time, put the camera somewhere fixed, script key presses and player movement and call script functions; the
`SurrealDebugger` UnrealScript debugger; [UELib](https://github.com/EliotVU/Unreal-Library) for the objects in a package. How the game works under the
scripts is in the [reverse-engineering notes](re/README.md).

## Where modding is going

What is planned (an Extras page, per-mod settings, more extras) is phase 6 of the
[roadmap](../ROADMAP.md#extras-for-hp1-srchp1mods). Not planned: speedrun tools (timer, splits, practice states), since the
HP1 PC leaderboards accept runs of the original game only, not of reimplementations.
