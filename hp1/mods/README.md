# hp1/mods — additions the original game doesn't have

Everything else under `hp1/` reimplements what HP1's own Engine.dll/Render.dll did (and carries `// IDA`
tags saying where). This folder is for features **we** add on top: quality-of-life changes, developer
shortcuts, anything a player of the 2001 release never saw.

Rules:

- A mod never changes vanilla behaviour unless it is switched on. Mods that are on by default
  (`Mods::Enabled()`) are all switched off by the `--vanilla` command line flag; opt-in mods have their own
  flag.
- Mods hook in only through `HP1::TickMods` / `HP1::ModsKeyDown` / `HP1::PostRenderMods` (`HP1Mods.cpp`), not through new engine
  hooks of their own. If a mod needs a new hook, add it to `HP1Mods.h` and to `docs/engine-hooks.md`.
- Work through the game's own script state (properties, script functions) where possible, so a mod
  stays a thin layer over vanilla behaviour.
- No `// IDA` tags here (nothing is reversed); say in a comment which script code a mod relies on.

Adding one:

1. `MyMod.cpp` here with a tick function (and a draw function if it draws), using the helpers in `HP1Mods.h`
   (`ObjectProperty`, `BoolProperty`, `SpacePressed`, `DrawPrompt`).
2. Declare them in `HP1Mods.h` and call them from `HP1::TickMods` / `HP1::PostRenderMods` in `HP1Mods.cpp`.
3. Gate it on `Mods::Enabled()` (on by default) or its own flag (`Mods::HasFlag("--my-flag")`).
4. Add a row below. The full guide is `docs/modding.md`; plans for ini settings, content mods and script mods are phase 6 of `ROADMAP.md`.

| Mod | Default | What it does |
|---|---|---|
| `CutsceneSkip.cpp` | on (`--vanilla` disables) | "Press Space to skip" during cutscenes; Space fast-forwards to the end |
| `StorybookSkip.cpp` | on (`--vanilla` disables) | "Press Space to skip" in storybooks (New Game intro, chapter interludes) |
| `LaunchSkips.cpp` | off (`--skip-splash`, `--skip-intro`) | skip the logo/title splash screens; skip the New Game storybook |
