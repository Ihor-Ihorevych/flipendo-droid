# Bugs in the original HP1

Bugs the retail game has that Flipendo should fix rather than reproduce. Each entry: where, what happens, the
known workaround in the original, and the state of our fix. A fix that changes original behaviour goes in `src/hp1/mods/`
(on by default, off with `--vanilla`, [modding.md](../../modding.md)), so a vanilla run still plays like the original.
HP2's bugs will get their own list in `../hp2/` once HP2 runs.

| Where | Bug | Workaround in the original | Fix |
|---|---|---|---|
| Lumos lesson (Lev3_Lumos, the octagonal room) | A platform can stay too low after breaking nearby vases, leaving a gap Harry can't jump. | Type `Harry debug mode on`, then `Harry super jump` to cross the gap. Players also report that capping the original at 60 FPS (or 60 Hz with VSync) avoids it. | not reproduced in Flipendo (see below) |
| High frame rates, e.g. Lev_Tut3 (the students CutScene6 sends to class after the third Alohomora door) | Above ~110 fps the students stop at the foot of a ramp (-3400,-9740) until `CutMovingTo`'s timeout teleports them. `baseChar.CutMovingTo.Tick` moves them each frame by `MoveSmooth` up 15, `GroundSpeed * DeltaTime` forward, down 15; the drop lands on the slope and `moveSmooth` (HP1's, ported exactly) slides them back ~1.8 units, a fixed amount per frame, while the forward step shrinks with the frame time (200 u/s: 3.3 units at 60 fps). Every cutscene walk over a slope has it. | cap at 60 fps | `src/hp1/mods/FrameLimit.cpp`: 60 fps by default |
| Lev_Tut3b, Peeves after the duel (`tut3peeves2`) | Peeves T-poses and drifts away instead of flying off. `tut3Peeves.dieing` sends him along `exitfirstpath` HPath_F1 towards `exitstationdestination` baseStation1, but the map's paths never reach it: HPath_F1 links only to HPath_F2, HPath_F2 to F1 and F3, HPath_F3 has no reach specs at all, and baseStation1's only links are HPath_A3 and CutCameraPos18. `Pawn.FindPath` (HP1's search, ported exactly) returns None at HPath_F1, so `patrol` falls into its `idleLoop` and loops `idleAnimName`, which is None: no sequence, so `USkeletalMesh::ApplyAnim` poses the reference skeleton (HP1 does the same). He keeps the last `MoveTo` acceleration (a flying pawn that can't strafe keeps it on arrival, HP1's `moveToward`), so he drifts off through the walls (`bCollideWorld` is off). Seen in Flipendo 2026-10-05 (`@state tut3peeves2 dieing`); the original not watched yet. | none known | `src/hp1/mods/PathFixes.cpp` adds the HPath_F3 → baseStation1 reach spec at level load: Peeves flies F1, F2, F3 to baseStation1, still animating, and the station's `bh_die` removes him |
| Every Flipendo hit (`baseSpell.SpawnHitEffects`) | `spellFlip` sets no `reactParticleEffectClass`, so the spawn returns None and the next two lines log `Accessed None`. Harmless: the hit effect plays and the spell lands. | none needed | none (log noise only) |
| Every jelly bean pickup (`Jellybean.killbean`) | The bean flies to Harry by moving `distance / (fPickupFlyTime / delta)` each frame, but `begin:` only checks the time every 0.1 s, so for a few frames `fPickupFlyTime` is near zero or negative and the bean is flung far away (the log says `fell out of the world`, once at 87975,-27741,-56449). Harmless: `AddBeans(1)` already ran and the bean is destroyed right after. | none needed | none (log noise only) |
| Lev2_Inc_A, Tut1Gnome9 and Tut1Gnome10 | The map saved both outside the level (Region ZoneNumber 0, iLeaf -1), so they fall out of the world on the first frame and are removed (`SpawnCarcass` logs `should never call base spawncarcass`). Two gnomes the designers left behind. | none needed | none |

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
