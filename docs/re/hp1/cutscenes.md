# HP1 cutscenes

How HP1's cutscene scripts drive the actors, and what happens in Lev_Tut1's scenes. HP2's cutscenes work differently
(scripts from text files, a built-in skip: [hp2/gameplay.md](../hp2/gameplay.md#cutscenes)); the engine behaviour they
rely on is in [physics.md](../engine/physics.md) and [scripting.md](../engine/scripting.md#latent-calls-on-other-actors).

HP1 cutscenes are `CutScene` / `CutScriptII` actors (HPBase) placed in the map. Each has up to 7 cast members with a
command list (`Cast0Script` .. `Cast6Script`, strings like `Moveto HpLoc`, `Talk FRED_GEORGE_014`, `Cue CutEnd`,
`Waitfor CutEnd`, `Trigger FGsec2`). The lists live in the map, not in the class scripts: dump them with
`tools/uelib_dump props <map.unr> <out.txt>` (e.g. `reference/hp1/maps/Lev_Tut1.txt`, not committed).

- **Starting.** `bTouchStarts` (default true) starts it when touched, `bTriggerStarts` (default true) when its Tag is
  triggered, `bLevelLoadStarts` on load. Several cutscenes share the tag `CutScriptII`, so most are touch-started.
- **Harry.** `Capture` sets the HUD's `bCutSceneMode` / `curCutScene` and calls `CutDoIdle` → state `CutIdleing`,
  which does `SetPhysics(PHYS_Rotating)`. `Moveto X` → `CutMoveTo` → state `CutMovingTo`: PHYS_Walking, and every
  PlayerTick moves him one step towards X with `MoveSmooth` (no velocity of its own), timing out after
  distance/GroundSpeed + 1 s with a `SetLocation`.
- **setPhysics stops actors** ([physics.md](../engine/physics.md#setphysics)). `CutMovingTo` depends on that:
  without it the player's last run velocity kept moving Harry under PHYS_Walking, past the mark, and he ran in place
  facing away from the NPC until the timeout.
- **CutSkip()** zeroes each cast member's next-action time; nothing in HP1 calls it (the CutsceneSkip mod does,
  [modding.md](../../modding.md)).

## Lev_Tut1 after the jump room

- CutScene55 (3126,-4319): Fred & George explain beans and frogs.
- Fred (`Tut1Fred3`) is a `merchant`: `fred.bump` sells when `numBeans >= salePrice` (25), triggering `saleScene`
  `WizardCardCut` (CutScene3), which spawns the Dumbledore card and triggers `FGsec2` (the secret passage).
- CutScene56 (2247,-5285): Filch. CutScene1 (2602,-5795): Malfoy, Crabbe and Goyle; triggers `FGsec3` and `DADA1`.
- CutScene58 (1978,-6232): Hermione. CutScene59 (1700,-6204): Quirrell, ends with `Trigger SpellLearnTrigger`
  (the Flipendo lesson). It also triggers `DADA1` (Mover58, TriggerToggle) again, closing the door Malfoy's scene opened.
- After the lesson `SpellLearnTrigger` triggers `CUTFLIPBEGIN`: CutScene60 (955,-6699). Harry's cast opens `enterFlip`
  (Mover36, the classroom's west door, TriggerToggle at 1152,-6656), Quirrell talks (`QUIRRELL_014`) and walks out,
  Harry follows to `AllPath1` and `CHANGELEVEL Lev_tut1b.unr` → `HPConsole.ChangeLevel` → `Level.ServerTravel(url, true)`
  (inventory travels). `HPConsole.Tick` saves to the selected slot on the first tick of the new level (NextURL empty
  again). Tested: `HP1_EXEC="60:@teleport 1621 -6578 827;61:@trigger CUTFLIPBEGIN"` reaches Lev_Tut1b, its intro and
  the Flipendo aiming help in ~15 s.
