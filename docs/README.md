# Flipendo documentation

Flipendo covers both of KnowWonder's Harry Potter games: HP1 (Philosopher's Stone) runs today, HP2 (Chamber of
Secrets) is being ported onto the same engine. The documents below say when something applies to only one of them.

## Playing

| Document | What's in it |
|---|---|
| [troubleshooting.md](troubleshooting.md) | where the game files are expected, what the error messages mean |

## Modding

| Document | What's in it |
|---|---|
| [modding.md](modding.md) | the built-in mods, writing your own, where modding is going |
| [debug-tools.md](debug-tools.md) | environment variables to script input, dump and trace actors, call script functions; crash reports, the script debugger |
| [playtest.md](playtest.md) | every HP1 level in story order and whether it has been played through in Flipendo, with what was seen |
| [speedrunning.md](speedrunning.md) | what HP1 speedrunners already have for the original, and the practice, research and timer tools Flipendo can add |

## Developing

| Document | What's in it |
|---|---|
| [development.md](development.md) | code layout, ground rules, reference material, porting a native, comparing with HP2 |
| [one-engine.md](one-engine.md) | how code is split between `src/knowwonder/` (both games), `src/hp1/` and `src/hp2/` so that no behaviour exists twice |
| [surrealengine-coverage.md](surrealengine-coverage.md) | which parts of SurrealEngine HP1 runs on: file formats (safe), HP1's own reversed code, or SurrealEngine's unverified behaviour, and what to reverse next |
| [engine-hooks.md](engine-hooks.md) | how Flipendo patches SurrealEngine, the patch set, every hook by file, updating SurrealEngine |

## How the games work

[re/](re/README.md): what was learned from the games' binaries and scripts.

- [re/engine/](re/README.md#knowwonders-engine-engine): KnowWonder's engine, shared by both games (animation,
  particles, lighting, collision, physics, the script VM, save games, ...), each note with what differs in HP2.
- [re/hp1/](re/README.md#harry-potter-1-hp1) and [re/hp2/](re/README.md#harry-potter-2-hp2): what only one game has.
- [re/reports/](re/README.md#generated-reports-reports): generated reports (every DLL, the native audits, HP1 vs HP2
  code).

## Elsewhere in the repository

- [`README.md`](../README.md): for players. What Flipendo is, screenshots, status, how to play, extras, community.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md): for contributors. Ways to help, building, developer flags, ground rules.
- [`ROADMAP.md`](../ROADMAP.md): what's left and what's next. How things work goes here in `docs/`, not there.
- [`CLAUDE.md`](../CLAUDE.md): the project rules, written for AI assistants but binding for everyone.
- [`images/`](../images/): [`branding/`](../images/branding/) (logo, icon) and [`screenshots/`](../images/screenshots/)
  (the README gallery and `social-preview.jpg`, 1280x640, for the repository's social preview). Screenshots are taken
  in Flipendo with `HP1_SHOTS` ([debug-tools.md](debug-tools.md)) and never show the official game logos.
