# Speedrunning

What the HP1 PC speedrun community already has for the original game, and what Flipendo can add on top. The checklist
is in [ROADMAP.md](../ROADMAP.md#speedrun-practice-and-research-tools).

## What runners have today (vanilla, as of 2026-10)

Collected from public pages (speedrun.com blocks automated reading, so this comes from search results and should be
checked with the community before relying on it):

- **Leaderboards** on [speedrun.com/hp1pc](https://www.speedrun.com/hp1pc): Any%, Glitchless, 100%, plus categories
  like Intro Skip and Brightness Glitch. Runs may start from a prepared save that skips the intro.
- **Autosplitter**: blank LiveSplit splits that split on entering each map
  ([Glitchless resources](https://www.speedrun.com/hp1pc/resources/yh01j)). Splits are per map, nothing finer.
- **Framerate cap mod**: since 2021-07-10 runs must use a 60 FPS cap mod (dropped into `System/`) instead of Dxtory, and
  its FPS counter must be visible in the video ([forum thread](https://www.speedrun.com/hp1pc/forums/zy9j5)). Some of
  the game's behaviour depends on the framerate (the Lumos platform, [re/hp1/original_bugs.md](re/hp1/original_bugs.md)).
- **Debug mode for practice**: typing `harrydebugmodeon` (or `bDebugMode=True` in the ini) adds a Level Select to the
  main menu and opens the console ([guide](https://www.speedrun.com/hp1pc/guides/glud0)). With cheats on there are
  ghost (noclip), fly, `setspeed`, super/big jump, invincibility, give beans/points/cards
  ([cheat list](https://www.neoseeker.com/harry-potter-and-the-sorcerers-stone/cheats/pc/)).
- **Guides**: glitchless, Any% and 100% tutorials, "Hard tricks for HP1"
  ([guides](https://www.speedrun.com/hp1pc/guides), [hard tricks](https://www.speedrun.com/hp1pc/guides/c4uk8)).
- **Other tools**: Koop's Harry Potter Editor for maps
  ([ModDB](https://www.moddb.com/games/harry-potter-and-the-sorcerers-stone/downloads/koops-harry-potter-editor)).
  No trainer or practice tool was found (Cheat Happens lists none).

## What Flipendo can add

What a closed exe can't do: Flipendo owns the engine, so tools can read and change any actor directly, without memory
scanning. Everything here is a practice and research tool, a mod in `hp1/mods/` (off with `--vanilla`), and must never
change the game's own rules: practice only helps if the game behaves like the original.

- **Practice beyond Level Select**: save and restore the exact state anywhere (not only at save points), warp to a
  position, reset to the start of a trick with one key, replay recorded inputs, a floor-height map for planning jumps.
  Today's developer flags (`@teleport`, `HP1_GOTO`, `HP1_KEYS`, `HP1_HEIGHTMAP`, [debug-tools.md](debug-tools.md)) are
  the starting point; for players they need keys and an overlay, not environment variables.
- **Show what is hidden**: trigger and cutscene volumes in the world, Tag → Event links, collision shapes (CT_Box),
  which ledges can be grabbed (PF_SpecialPoly, [re/engine/physics.md](re/engine/physics.md)), where the auto-jump
  would land. Runners find skips by knowing where triggers are; today they guess.
- **A timer inside the engine**: splits from engine events (level change, cutscene start/end, lesson passed, save point)
  instead of map entry only, load time removed exactly, a link to LiveSplit (LiveSplit Server) so existing splits keep
  working.

Not a goal: replacing the original for leaderboard runs. A run in Flipendo is only comparable once its physics and
timing are shown to match the original frame for frame, and that is for the community to decide.
