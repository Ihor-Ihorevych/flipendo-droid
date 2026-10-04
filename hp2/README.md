# hp2/: what only Harry Potter 2 has

Flipendo is one engine for KnowWonder's Harry Potter games: point it at an HP1 folder and it plays HP1, point it at
HP2 and it plays HP2. Each game must behave exactly like its own original, and no behaviour may exist twice.

Status: groundwork. `IsKnowWonder()` (GameFolder.h) is still HP1 only, so none of this runs yet. The first natives and
the bytecode difference are reversed and written; the hooks that turn them on come with the HP2 port (ROADMAP.md).

## Where code goes

| Folder | What | Example |
|---|---|---|
| `engine/` | SurrealEngine, stock UE1 behaviour for every game | the VM, rendering, collision |
| `kw/` | KnowWonder's engine, **one implementation for both games** | skeletal animation, ParticleFX, Gesture, save games |
| `hp1/` | what only HP1 has | HP1's menu canvas, HP1's extras (`hp1/mods/`) |
| `hp2/` | what only HP2 has | HP2-only natives, HP2's bytecode tokens, OpenAL music |

HP2's engine DLLs are HP1's with additions (`docs/re/hp2_compare.md`): Fire.dll is 100% identical code, Core 82%,
Engine 62% identical plus 14% that differs only in struct offsets. So by default **everything goes in `kw/`**, and
`hp2/` stays small.

## The rules that prevent duplication

1. **Same behaviour, same code.** A function that is identical in both DLLs, or differs only in offsets, has one
   implementation in `kw/`. Offsets never matter to us: script properties are looked up by name (`kw/KWActor.h`),
   so HP2's different layouts are handled automatically.
2. **A real difference is a branch at the line that differs, not a copy.** Use `KW::IsHP2()` (`kw/KWGame.h`) inside
   the shared function, and give the function both addresses in its IDA tags. Never copy a function into `hp2/` to
   change three lines of it.
3. **A different script signature gets a thin adapter, not a second body.** When HP2's UnrealScript declares more
   parameters (StopSound gained `FadeOutTime`), `hp2/` registers an adapter for the HP2 signature that calls the
   same shared body as HP1's (`KW::StopSound`, `kw/KWSound.cpp`). The adapter only unpacks arguments.
4. **Only HP2-only things live here:** natives HP1 doesn't have (`BoneRot`, `IsSoftwareRendering`,
   `GetCurrentKeyState`, music), HP2's script token table, the OpenAL / Ogg audio that replaced Galaxy, HP2's menus.
5. **Registration order expresses it:** `KW::RegisterNatives()` registers every native with HP1's signature, then
   calls `HP2::RegisterNatives()` when HP2 runs, which adds HP2's natives and overrides only the changed ones.
6. **Check before porting.** `python tools/hp2_compare.py` says per function whether HP2's code is identical,
   offsets-only or changed. `python tools/native_audit.py hp2` lists what HP2's scripts still miss. Read HP2's code
   for every "changed" function before deciding it needs a branch: most are the same as HP1's apart from the
   DebugInfo check (next section).

## What's here so far

| File | What |
|---|---|
| `HP2Bytecode.cpp` | HP2's token table: the new DebugInfo token at 0x38, and the one-step shift of GlobalFunction..FloatToBool (`docs/re/hp2_bytecode.md`) |
| `HP2Natives.cpp` | `BoneRot`, `IsSoftwareRendering`, `GetCurrentKeyState`, and the `StopSound` adapter |

## IDA tags

Same format as in `kw/` (CLAUDE.md), with the HP2 address: `// IDA Engine.dll: <decorated name> [HP2 0x...]`, or
`[HP2 Core 0x...]` for Core.dll. HP2's databases are in `../ida/hp2/`, never mixed with HP1's.
