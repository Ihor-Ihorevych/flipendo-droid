# Flipendo documentation

| Document | For | What's in it |
|---|---|---|
| [troubleshooting.md](troubleshooting.md) | players | where the game files are expected, what the error messages mean |
| [modding.md](modding.md) | modders | the built-in mods, writing your own, where modding is going |
| [development.md](development.md) | contributors | code layout, ground rules, reference material, porting a native, debug tools |
| [engine-hooks.md](engine-hooks.md) | contributors | how Flipendo patches SurrealEngine, every hook by file, updating SurrealEngine |
| [native_audit.md](native_audit.md) | contributors | generated: natives still missing (`tools/native_audit.py`) |
| [native_audit_hp2.md](native_audit_hp2.md) | contributors | generated: the same for HP2 (`tools/native_audit.py hp2`); HP1_PORT = covered by an HP1 port |
| [hp2_compare.md](hp2_compare.md) | contributors | generated: HP1 vs HP2 engine code per DLL, and which ported functions carry over (`tools/hp2_compare.py`) |
| [re/](re/) | reverse engineers | what was learned from HP1's binaries: [animation](re/animation.md), [particles](re/particles.md), [save games](re/savegames.md), [spells](re/spells.md), [script events](re/script_events.md), [cutscenes](re/cutscenes.md) |

The other top-level files each have one job:

- [`README.md`](../README.md): what Flipendo is, its status, how to build and play.
- [`ROADMAP.md`](../ROADMAP.md): what's done and what's next. How things work goes here in `docs/`, not there.
- [`CLAUDE.md`](../CLAUDE.md): the project rules, written for AI assistants but binding for everyone.
