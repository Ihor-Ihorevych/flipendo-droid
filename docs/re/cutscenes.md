# Cutscenes

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
- **setPhysics stops actors** (`AActor::setPhysics` 0x103E5140): switching to PHYS_None or PHYS_Rotating zeroes
  Velocity and Acceleration. `CutMovingTo` depends on that: without it the player's last run velocity kept moving
  Harry under PHYS_Walking, past the mark, and he ran in place facing away from the NPC until the timeout
  (`hp1/HP1Collision.cpp` NSetPhysics).
- **CutSkip()** zeroes each cast member's next-action time; nothing in HP1 calls it (the CutsceneSkip mod does).

## Lev_Tut1 after the jump room

- CutScene55 (3126,-4319): Fred & George explain beans and frogs.
- Fred (`Tut1Fred3`) is a `merchant`: `fred.bump` sells when `numBeans >= salePrice` (25), triggering `saleScene`
  `WizardCardCut` (CutScene3), which spawns the Dumbledore card and triggers `FGsec2` (the secret passage).
- CutScene56 (2247,-5285): Filch. CutScene1 (2602,-5795): Malfoy, Crabbe and Goyle; triggers `FGsec3` and `DADA1`.
- CutScene58 (1978,-6232): Hermione. CutScene59 (1700,-6204): Quirrell, ends with `Trigger SpellLearnTrigger`
  (the Flipendo lesson; currently crashes, see ROADMAP).
