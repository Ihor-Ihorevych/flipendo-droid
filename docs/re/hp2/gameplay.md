# HP2 gameplay scripts: what differs from HP1

HP2's gameplay package is `hgame` (HP1's are `HarryPotter`, `HPBase`, `HPMenu`, ...). Its `.u` files ship with most
source stripped, so these notes come from the scripts `tools/extract_scripts.sh hp2` decompiles into
`reference/hp2/ScriptSource/` (natives show up there as `__NFUN_<index>__`). The shared engine underneath is in
[`../engine/`](../engine/); the bytecode difference is in [scripting.md](../engine/scripting.md#bytecode-hp1-vs-hp2).

First notes, not yet a full study. Each entry says what HP1 does instead.

## Cutscenes

HP1's cutscenes are command lists stored in the map ([hp1/cutscenes.md](../hp1/cutscenes.md)). HP2's `CutScene`
(hgame) reads them from text files instead: with `FileName` set, `CreateThreads` loads up to 20 threads (`thread_<n>`
sections) from `System/cutscenes/<FileName>.int` (214 files on the retail disc) through `Localize` and
`CutScriptDisk.load`; otherwise from the `aThreadScripts[20]` strings. It starts on bump, trigger or level load
(`bBumpStarts`, `bTriggerStarts`, `bLevelLoadStarts`), and has its own skip: state `FastForwarding`, allowed when
`bSkipAllowed` (default true). HP1 has no skip (Flipendo adds one as a mod).

## Saving

`PlayerPawn.SaveGame()` only queues a save for the end of the level tick; the slot page reads save number 0 from a
directory per slot; `LoadGame 0` reloads after death. Details: [savegames.md](../engine/savegames.md#hp2s-flow-hgame-scripts).

## Spells

HP1's `Target` / `SpellLearnTrigger` ([hp1/spells.md](../hp1/spells.md)) are gone. HP2 aims with `SpellCursor` (a
ParticleFX that shows the spell's `GestureSprite`), teaches with `SpellLessonTrigger` (with `SpellLessonShape`,
`SpellLessonWand`, `SpellLessonInterpolationPoint`) and has duelling spells (`spellDuelExpelliarmus`,
`spellDuelMimblewimble`, `spellDuelRictusempra`) and `SpellChallengeTrigger`. None of them calls the Gesture natives
([gestures.md](../engine/gestures.md)). Not studied yet.

## Collision

Two new collide types, `CT_AlignedOvalCylinder` and `CT_OrientedOvalCylinder` ([collision.md](../engine/collision.md#hp1-and-hp2)).

## HP2-only natives

`Actor.BoneRot` (328), `IsSoftwareRendering` (329), `GetCurrentKeyState` (330) (`hp2/HP2Natives.cpp`), the OpenAL / Ogg Vorbis
music natives (`PlayMusic`, `StopMusic`, `StopAllMusic`, not ported), and the changed signatures of `StopSound`
(`FadeOutTime`) and `CreateAnimChannel` (`bNotReplaceable`). The full list: [native_audit_hp2.md](../reports/native_audit_hp2.md).
