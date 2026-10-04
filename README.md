<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/branding/flipendo-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="docs/branding/flipendo-logo.png">
    <img src="docs/branding/flipendo-logo.png" alt="Flipendo" width="560">
  </picture>
</p>

<p align="center">
  <b>KnowWonder's Harry Potter games on a modern engine:<br>
  any resolution, real widescreen, no SafeDisc, moddable.</b><br>
  <sub>Named after the first spell Harry learns in the first game.</sub>
</p>

<p align="center">
  <a href="#status">Status</a> ·
  <a href="#getting-started">Getting started</a> ·
  <a href="#modding">Modding</a> ·
  <a href="#contributing">Contributing</a> ·
  <a href="ROADMAP.md">Roadmap</a> ·
  <a href="docs/README.md">Docs</a>
</p>

Flipendo is a source port of KnowWonder's Harry Potter games for PC, built on
[SurrealEngine](https://github.com/dpjudas/SurrealEngine), an open-source reimplementation of Unreal Engine 1.

**Right now it is focused on one game: Harry Potter and the Philosopher's Stone** (PC, 2001, KnowWonder / EA,
Unreal Engine 1 build 433), **getting it right down to the details.** It isn't meant to stop there: Harry Potter
and the Chamber of Secrets (PC, 2002) runs on the same engine build, and much of its engine code is identical to
HP1's ([comparison](docs/re/hp2_compare.md)). HP2 support comes after HP1 is complete.

Almost all of the games' logic is UnrealScript that ships inside their `.u` packages, and SurrealEngine runs that
bytecode unchanged. So the port is the *engine*: KnowWonder's modifications to Unreal's `Engine.dll` (skeletal
animation, particles, spell gestures, physics changes and more), reimplemented in C++. This repository contains no
Epic or KnowWonder code. **You need your own copy of the game.**

## Why play it this way?

The 2001 PC release is getting hard to run. Its SafeDisc copy protection doesn't work on Windows 10/11, the menus
stop at 1024x768, and widescreen needs community fixes. Flipendo runs the **original, unmodified game files** on a
modern engine instead:

- **Any resolution, real widescreen.** Native resolution (1440p, 4K, ultrawide); the 3D view gets wider instead of
  being cropped ("Hor+"), the HUD and cutscene bars fill the screen, the menu book keeps its 4:3 shape.
- **Modern renderer.** Vulkan (default), Direct3D 11/12 or OpenGL instead of 2001's Direct3D 7.
- **No disc, no SafeDisc, no compatibility patches.** Point it at an installed copy of the game.
- **Same game.** Levels, scripts, dialogue and voice acting come from your copy; nothing is remade or altered.
- **Optional extras.** Cutscene and storybook skipping, faster startup ([Modding](#modding)); `--vanilla` turns
  them all off.
- **Fixable and portable.** Engine bugs can be fixed in source; Linux, macOS and Steam Deck become possible
  because SurrealEngine already runs on them.

## Status

> **Early work in progress. The game is not playable start to finish yet.** No prebuilt downloads; you build it
> yourself (below). [`ROADMAP.md`](ROADMAP.md) has the detailed checklist.

| Area | State |
|---|---|
| Front-end menu book, options (incl. key rebinding), New Game storybook | works |
| Widescreen / any resolution, music, screen fades | works |
| Characters: skeletal animation, climbing, ledge grabbing, Harry's wand | works; not yet compared side by side with the original |
| First level (`Lev_Tut1`): cutscenes, jump room, wizard cards, Malfoy, the Flipendo lesson | plays through to the lesson; drawing and scoring the spell not tested yet |
| Doors, cutscenes, NPCs, Peeves, jelly beans, wizard cards | works |
| Spells: gesture recognition, particle effects | mostly works |
| Save and load | works (save slots, thumbnails) |
| Broom and Quidditch levels | load and run their intros; not playable yet |
| Level-to-level travel, full playthrough | not yet |

## Getting started

### What you need

- An installed copy of **Harry Potter and the Philosopher's Stone (PC, 2001)**: the folder with `System/`,
  `Maps/`, `Textures/`, ... Recognised `System/HP.exe` builds: UK 1.1, EN retail (SafeDisc) and a community No-CD.
  If yours isn't detected, the error shows its SHA-1; please open an issue with it.
- **Windows**: Visual Studio 2026 (v18) with the C++ workload (its bundled CMake is used if `cmake` isn't on
  `PATH`). **Linux/macOS**: see [`engine/Docs/Building.md`](engine/Docs/Building.md) (untested with Flipendo).

### Build and run

From Git Bash:

```sh
git clone --recursive https://github.com/kroplabeskidu/flipendo   # or: git submodule update --init
cd flipendo
tools/build.sh                                                     # Release; binaries in build/Release/
build/Release/SurrealEngine.exe "C:/Games/Harry Potter"            # opens the launcher
```

| Flag | What it does |
|---|---|
| `--autolaunch` | boot the first detected game, no launcher and no modal error boxes |
| `--logfile=<path>` | stream the log to a file (survives crashes) |
| `--skip-splash` | skip the EA/KnowWonder/title splash screens |
| `--skip-intro` | New Game goes straight to the first level |
| `--vanilla` | turn off every addition the original doesn't have |

If the game doesn't start, the error says which folder or file is missing and where it was looked for;
[docs/troubleshooting.md](docs/troubleshooting.md) explains each message. For development, `tools/run_hp1.sh`
launches straight into the game with a log ([docs/development.md](docs/development.md)).

## Modding

Flipendo keeps the **faithful port** apart from **additions**: additions never change vanilla behaviour unless
switched on, and `--vanilla` switches all of them off.

| Mod | Default | What it does |
|---|---|---|
| Cutscene skip | on | "Press Space to skip" during cutscenes |
| Storybook skip | on | "Press Space to skip" in storybooks (New Game intro, chapter interludes) |
| Launch skips | off (`--skip-splash`, `--skip-intro`) | skip the splash screens and the New Game storybook |

A mod is one C++ file in `hp1/mods/`. The longer-term plan is a mod platform: per-mod settings, drop-in
`Mods/<name>/` folders for texture packs, custom levels and script mods, and the HP1 community's custom levels.
[docs/modding.md](docs/modding.md) has the guide and the plan.

## Contributing

There's work for every skill level, and much of it needs no programming:

- **Playtesters**: play a level, compare it with the original, report what looks or behaves differently.
  Side-by-side screenshots or short clips help most.
- **Owners of other game versions**: if your `HP.exe` isn't detected, report the SHA-1 the error shows, with the
  language and release.
- **Modders**: build an extra in `hp1/mods/`, or help design content-mod loading ([docs/modding.md](docs/modding.md)).
- **Reverse engineers**: the remaining work is KnowWonder's native code. [docs/re/dlls.md](docs/re/dlls.md) shows
  what every DLL does and what's been reimplemented; [docs/re/](docs/re/) has what's been learned so far.
- **C++ developers**: start with [docs/development.md](docs/development.md) (layout, ground rules, reference
  material, debug tools).

Two ground rules: never use Epic's UE1 source or headers (or header sets derived from them) as a reference, and never
commit game data or anything extracted from it.

Much of this port is written with LLM assistance (Claude), which is why our engine patches aren't sent to the
SurrealEngine project (see [below](#surrealengine--licence)).

## SurrealEngine & licence

Flipendo's own code (`kw/`, `hp1/`, `patches/`, `tools/`, `docs/`) is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md): free to use, modify and share, not for commercial use. It is
written from the games' own binaries and scripts and from observing the original games; the project does not use
Epic source code or headers as a reference.

Flipendo is an independent project, **not affiliated with or endorsed by SurrealEngine**. Please report problems
here, not to SurrealEngine. `engine/` is SurrealEngine (zlib licence, see `engine/LICENSE.md`), used as a git
submodule. Our changes to it are the `patches/*.patch` files, applied by the build, with altered lines marked
`flipendo:` as the licence requires ([docs/engine-hooks.md](docs/engine-hooks.md)). SurrealEngine asks that
LLM-assisted changes are **not** sent as pull requests (`engine/NO-AI Code Rule.md`), so our patches stay here.

Harry Potter is a trademark of Warner Bros. Entertainment. The game data is copyright EA / KnowWonder and is not
included.
