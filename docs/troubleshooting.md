# Troubleshooting

Flipendo runs the game files from your own installation. Most startup problems are a wrong folder or missing
files, and the error message says which. Harry Potter and the Philosopher's Stone (HP1) is the game that plays today;
Chamber of Secrets (HP2) is recognised but not playable yet. Run with `--logfile=flipendo.log` to keep the full log; it survives
crashes.

## Where the game files are expected

Point Flipendo at the game's **install folder**, the one that contains:

```
Harry Potter/
  System/     HP.exe, *.u (Core.u, Engine.u, HarryPotter.u, HPMenu.u, ...), *.ini, *.int
  Maps/       *.unr (Entry.unr, Startup.unr, Lev_Tut1.unr, ...)
  Textures/   *.utx
  Sounds/     *.uax
  Music/      *.umx
```

Packages are found by name in the folders listed as `Paths=` under `[Core.System]` in the game's ini
(`System/Default.ini`; SurrealEngine keeps its own copy as `System/SE-HP.ini`). The defaults are
`../System/*.u`, `../Maps/*.unr`, `../Textures/*.utx`, `../Sounds/*.uax`, `../Music/*.umx` and `../save/*.usa`,
relative to `System/`. If two folders contain a package with the same name, the folder listed first wins.

## Messages

### `no known game found` + a reason

The folder you gave isn't a recognised game folder. The reason line says why:

| Reason | Fix |
|---|---|
| `folder does not exist` | Check the path. Quote it if it contains spaces. |
| `looks like the game's System folder` | Use the folder above `System/`. |
| `no known game executable in its System folder` | The folder has no `System/HP.exe`. Use the install folder, not the disc or a parent folder. |
| `System/HP.exe is not a recognised version (SHA-1 ...)` | Your game release isn't in the list yet. [Open an issue](../CONTRIBUTING.md#other-game-versions) with the SHA-1, language and release. |

### `Could not find package 'X' (needed by package 'Y')`

A package the game needs isn't in any of the `Paths=` folders. The message lists every folder searched and
marks the ones that don't exist. Usually a folder (often `Textures/`, `Sounds/` or `Music/`) wasn't copied, or
the install is incomplete. Reinstall, or copy the missing folder from the disc.

### `Could not find map 'X.unr'. Looked in: ...`

The map isn't in the map folders listed. Check the `--url=` name (`Lev_Tut1`, not `Lev_Tut1.unr.unr`), or that
`Maps/` is complete.

### In the log only

These don't stop the game, but explain missing pictures, sounds or text:

- `Game data folder missing: <folder> (Paths=...)`: a `Paths=` entry points at a folder that doesn't exist.
- `Object.DynamicLoadObject: could not load 'Pkg.Name': ...`: a script asked for an asset at run time. The
  reason is either a missing package (with the folders searched, the first time) or a package that lacks the object.
  Not all of them are problems: the scripts also ask for files that only some language versions have (for
  example `HPFonts`, loaded only by the Polish release).
- `Package X imports Class Y.Z, which Y does not contain`: an object a package refers to is missing. Two show up
  on every start and are known: `Engine.ParticleList` and an editor icon in `HPEdit`
  ([docs/re/engine/particles.md](re/engine/particles.md)).
- `Unimplemented: Class.Function`: a native function Flipendo hasn't ported yet
  ([native audit](re/reports/native_audit_hp1.md)).

## Other problems

- **Rebuilding fails with a file lock**: `SurrealEngine.exe` is still running; close it first.
- **`CONFLICT` from `tools/build.sh`**: see [engine-hooks.md](engine-hooks.md#changing-a-hook).
