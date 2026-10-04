# Bugs in the original HP1

Bugs the retail game has that Flipendo should fix rather than reproduce. Each entry: where, what happens, the
known workaround in the original, and the state of our fix. A fix that changes original behaviour goes in `hp1/mods/`
(on by default, off with `--vanilla`, [modding.md](../../modding.md)), so a vanilla run still plays like the original.
HP2's bugs will get their own list in `../hp2/` once HP2 runs.

| Where | Bug | Workaround in the original | Fix |
|---|---|---|---|
| Lumos lesson (Lev3_Lumos, the octagonal room) | A platform can stay too low after breaking nearby vases, leaving a gap Harry can't jump. | Type `Harry debug mode on`, then `Harry super jump` to cross the gap. Players also report that capping the original at 60 FPS (or 60 Hz with VSync) avoids it. | not reproduced in Flipendo (see below) |
| Every Flipendo hit (`baseSpell.SpawnHitEffects`) | `spellFlip` sets no `reactParticleEffectClass`, so the spawn returns None and the next two lines log `Accessed None`. Harmless: the hit effect plays and the spell lands. | none needed | none (log noise only) |

### Not checked

- **Dialogue cut off on fast PCs.** At high frame rates the original may cut dialogue short, possibly from timing by
  frames instead of seconds. The scripts time dialogue in seconds (`baseDialog`, `baseNarrator`, `SpellLearnTrigger`
  wait `GetSoundDuration(dlgSound)`), so if the bug is real it is elsewhere: Galaxy's sound duration, cutscene command
  timing, or one line ending the next. Flipendo can't have it: `GetSoundDuration` is the decoded sample count over the
  sample rate (`USound::GetDuration`), independent of frame rate (read 2026-10-05).

### Lumos lesson platform

The room's "staircase" is the four AttachMovers tagged `GargoylePlatforms2` (gargoyle7 + spellTrigger1 raise them, Lumos
only, once): AttachMover2/3 rise from Z -176 by 80/144, AttachMover0/1 come down from Z 912 by 880/816, MoveTime 1 s,
DelayTime 4.2 s, TriggerToggle with bTriggerOnceOnly. The two vases on the pedestal next to them are
FlipendoVaseBronze0/1 (1055, -400/303, -45). Script-side nothing ties the two together: the vases have Tag
`FlipendoVaseBronze` and no Event, their break spawns beans, a broken vase and six shards, and the movers have
MoverEncroachType 3 (ME_IgnoreWhenEncroach), so actors in the way can't stop or reverse them through
`Mover.EncroachingOn`. The 60 FPS workaround points at native mover physics in the original instead (HP1's
`AActor::physMovingBrush`, Engine.dll 0x104061F0, has KnowWonder additions such as a gravity term; not reversed).

Test in Flipendo (2026-10-04, `HP1_EXEC` triggers on Lev3_Lumos, uncapped framerate): breaking both vases before
raising the platforms, and raising them first then breaking the vases while they wait out DelayTime, both end with
all four platforms at their keyframes (Z -96, -32, 32, 96). Still to check: by hand, with Harry standing on or
jumping between the platforms while they move.
