# HP1 cutscenes

How HP1's cutscene scripts drive the actors, and what happens in Lev_Tut1's scenes. HP2's cutscenes work differently
(scripts from text files, a built-in skip: [hp2/gameplay.md](../hp2/gameplay.md#cutscenes)); the engine behaviour they
rely on is in [physics.md](../engine/physics.md) and [scripting.md](../engine/scripting.md#latent-calls-on-other-actors).

HP1 cutscenes are `CutScene` / `CutScriptII` actors (HPBase) placed in the map. Each has up to 7 cast members with a
command list (`Cast0Script` .. `Cast6Script`, strings like `Moveto HpLoc`, `Talk FRED_GEORGE_014`, `Cue CutEnd`,
`Waitfor CutEnd`, `Trigger FGsec2`). The lists live in the map, not in the class scripts: dump them with
UELib (not committed).

- **Starting.** `bTouchStarts` (default true) starts it when touched, `bTriggerStarts` (default true) when its Tag is
  triggered, `bLevelLoadStarts` on load. Several cutscenes share the tag `CutScriptII`, so most are touch-started.
- **Harry.** `Capture` sets the HUD's `bCutSceneMode` / `curCutScene` and calls `CutDoIdle` → state `CutIdleing`,
  which does `SetPhysics(PHYS_Rotating)`. `Moveto X` → `CutMoveTo` → state `CutMovingTo`: PHYS_Walking, and every
  PlayerTick moves him one step towards X with `MoveSmooth` (no velocity of its own), timing out after
  distance/GroundSpeed + 1 s with a `SetLocation`.
- **setPhysics stops actors** ([physics.md](../engine/physics.md#setphysics)). `CutMovingTo` depends on that:
  without it the player's last run velocity kept moving Harry under PHYS_Walking, past the mark, and he ran in place
  facing away from the NPC until the timeout.
- **Camera speed.** In `CutState` the camera (`BaseCam`, `PotCam0` in Lev3_Lumos) moves `CameraSpeed * dt` of the way
  to its mark every tick (`throttleTrack`; `Camspeed 0.3` makes it 0.6) and reports arrival within `CutCameraProx`
  (15, or `Camprox`). The step is tripled while `p.bMovingBackwards` is set, `p.BossTarget` isn't None or the camera
  is `CAM_Reverse`. `bMovingBackwards` is script only (`Harry.PlayRunning` / `TweenToRunning`): set when Harry runs
  backwards, cleared only when he next runs forwards, so a cast made after backing up plays every camera move of the
  next cutscene three times faster. Lev3_Lumos's bridges cutscene (`CutScene3`, from `Dispatcher2`) then lasts
  ~5.4 s instead of ~11.7 s and ends before the `Bridges` movers turn up at 7.2 s (measured 2026-10-11 with
  `HP1_KEYS` holding Down for 1 s before `@trigger BridgesSwitch`).
- **CutSkip()** zeroes each cast member's next-action time; nothing in HP1 calls it. Calling it breaks scenes: the cast
  members keep in step only through their waits (`Sleep`, and `Talk`'s line length), not through cues. Lev3_Quid2's
  common room scene (`CutScene3`, tag `Intro`) has the camera (cast 0) do `Sleep 9.0`, `Sleep 2.0`, `FadeOut 1.0`
  while the narrator (cast 6) reads three lines and then does `Trigger QuidIntro`; the stadium scene (`CutScene0`)
  begins with `Sleep 1.0`, `FadeIn 1.0`. Unskipped the camera's FadeOut comes first. With every wait zeroed the narrator
  got there first, the FadeOut came after the stadium's FadeIn, and the match was played with `ConstantGlowFog.W` at -1:
  a black world under the HUD. The previous CutsceneSkip mod shortened every cast wait by the same minimum,
  leaving waits unchanged while an actor moved or turned. That preserved order within the cast but did not advance
  external world/console timers by the time removed. It also set the saved `LevelInfo.TimeDilation` to 8.

## Cutscene skip transition

The optional `cutscene-skip` mod now fades to black in 0.3 real seconds, displays `Skipping...` over the black
screen, and runs the whole level at 8x in steps no larger than 1/60 game second. Console timers, actors and ViewFlash
advance together; no cast waits or saved TimeDilation are rewritten. Work is capped at 0.8 game second per rendered
frame. Shift does not add its own multiplier during a skip. Escape cancels and reveals the current scene.

A skip ends when the scene releases Harry and its remaining camera/actor tracks finish, or immediately when Harry
enters `SpellLearning`. The latter matters in Lev_Tut3: CutScene58/AlohomoraStart and CutScene61/CharmsStart leave
HUD.curCutScene pointing at their old scene. A non-null HUD reference alone therefore does not mean a skippable scene
still holds Harry. The lesson's world and console clocks stay stopped during the reveal, so the exercise does not
consume drawing time under the black overlay. These checks use script states, not map or actor-name exceptions.

Before UnloadMap destroys actors, the skip clears its scene identity, acceleration and speech suppression; only the
visual fade may continue across a map change or save load. Existing speech is stopped at skip start and new sounds
in SLOT_Interact/SLOT_Talk are suppressed until the reveal begins; music and saved volume settings are untouched.
The mod is off with `--no-mod=cutscene-skip` or `--vanilla`. This is optional mod behaviour, not a reconstructed
native cutscene-skip implementation.

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
