# Flipendo documentation

| Document | For | What's in it |
|---|---|---|
| [troubleshooting.md](troubleshooting.md) | players | where the game files are expected, what the error messages mean |
| [modding.md](modding.md) | modders | the built-in mods, writing your own, where modding is going |
| [development.md](development.md) | contributors | code layout (`kw/`, `hp1/`), ground rules, reference material, porting a native, debug tools, comparing with HP2 |
| [engine-hooks.md](engine-hooks.md) | contributors | how Flipendo patches SurrealEngine, the patch set, every hook by file, updating SurrealEngine |

## Reverse engineering (`re/`)

What was learned from the games' binaries and scripts.

**Generated reports** (rerun the tool after changes):

| Report | What's in it | Tool |
|---|---|---|
| [re/dlls.md](re/dlls.md) | every DLL of HP1 and HP2: what it does, its exports and classes, who provides it in Flipendo, every function we reimplemented and whether HP2 has the same code, script native counts | `tools/dll_report.py` |
| [re/native_audit_hp1.md](re/native_audit_hp1.md) | every native HP1's scripts declare: OK / STUB / MISSING / INDEX | `tools/native_audit.py` |
| [re/native_audit_hp2.md](re/native_audit_hp2.md) | the same for HP2; HP1_PORT = covered by a `kw/` port gated to HP1 | `tools/native_audit.py hp2` |
| [re/hp2_compare.md](re/hp2_compare.md) | HP1 vs HP2 code, export by export, and which ported functions carry over | `tools/hp2_compare.py` |

**Topic notes** (written by hand):

| Note | Topic |
|---|---|
| [re/animation.md](re/animation.md) | skeletal meshes, the animation format, channels, tweening, root motion |
| [re/particles.md](re/particles.md) | ParticleFX: parameters, emission, update, rendering, wind |
| [re/spells.md](re/spells.md) | spell casting, gestures, the spell lesson and its rendering (IceTexture) |
| [re/cutscenes.md](re/cutscenes.md) | CutScene command lists, how cutscenes move Harry, Lev_Tut1's scenes and merchants |
| [re/savegames.md](re/savegames.md) | save slots, GameSaveInfo, thumbnails, loading |
| [re/script_events.md](re/script_events.md) | every script event the native code raises, and which ones SurrealEngine didn't |
| [re/lighting.md](re/lighting.md) | mesh lighting: light picking, colours and effects, per-vertex light, back-face culling |
| [re/original_bugs.md](re/original_bugs.md) | bugs in the original game that Flipendo fixes instead of reproducing |

## The top-level files

- [`README.md`](../README.md): what Flipendo is, its status, how to build and play.
- [`ROADMAP.md`](../ROADMAP.md): what's done and what's next. How things work goes here in `docs/`, not there.
- [`CLAUDE.md`](../CLAUDE.md): the project rules, written for AI assistants but binding for everyone.
