# Bugs in the original HP2

Bugs the retail game has that Flipendo should fix rather than reproduce. Same rules as HP1's list
([../hp1/original_bugs.md](../hp1/original_bugs.md)): each entry gives where, what happens, the known workaround in the
original, and the state of our fix. Fixes are part of the game, not mods: always on, also with `--vanilla`.

| Where | Bug | Workaround in the original | Fix |
|---|---|---|---|
| Adv6Goyle, the fire crab trap room (firecrabSmall0/1 on Mover22, the lift floor), issue #22 | A flipped small fire crab pushed near a side wall slides on its back to the opposite wall. `firecrab.OnALedge` treats a spot as a ledge when `FastTrace` finds nothing in the 35 units below it, and `FastTrace` sees only the level's BSP, not movers (HP2 `execFastTrace` [HP2 0x1041AF80]: `UModel::FastLineCheck`). On the lift every spot is a ledge, so the crab stays on its back (`FallOverLedge`), and `pushDirection` finds floor only in the headings that reach past a wall (details below). Seen 2026-11-13 with `--vanilla`: firecrabSmall0 pushed north with Flipendo to y 469 slid back to the south wall (y 152) and stayed there. | push the crab with Flipendo into the gap the west wall opens (not tested) | `src/hp2/HP2FireCrab.cpp`: `pushDirection` returns (0,0,-1) for a crab based on a mover, so it lies where it landed; a pushed crab keeps its lying radius (below). `OnALedge` stays the original's (below: Ch1Rictusempra needs it) |
| BeanRewardRoom and any map opening with a level-start cutscene, a bean picked up in its first 2 s, issue #20 | The bean's flight to the HUD counter aims at NaN: `StatusGroup.GetItemLocation` divides by the StatusManager's canvas size, which stays 0 while `HPHud` hides the status HUD for the first 2 s (details: [gameplay.md](gameplay.md#pickups-flying-to-the-hud)). The original moves the bean to NaN and carries on; Flipendo froze for players who ran into the beans next to Harry's start right away (Windows 10 and 11, 1.3 to 1.5; not reproduced on our machines, where the bean sat at NaN without a freeze). Waiting a few seconds before picking up a bean avoided it | none needed | `src/knowwonder/KWMove.cpp`: `MoveActor` and `FarMoveActor` refuse a non-finite delta or destination (logged as "Refused moving"), so no actor ends up at NaN. Checked headless 2026-11-13 on Linux and Windows from the player's save: the bean stays in place for its 0.25 s, is counted and destroyed |
| Ch1Rictusempra, the crab bucket at the challenge's end (ElevatorMover3), issue #26 | The bucket stops for good on its way down to the fourth keyframe (z -145.7 instead of -304, playtest 2026-11-13), so the star never comes down. `MoveActor` sweeps a mover's whole bounding box and stops it at the first actor in it (HP1 Engine 0x103AA3A0; HP2 skips the sweep only for `MoverEncroachType` 4-7), and the bucket's brush has corner poles reaching about 700 units below it. The first puzzle's crab (firecrabSmall25), left lying on Mover38 at the bottom of the shaft, is inside that box though nowhere near the brush. Not watched in the original; same code, so it should stall there too whenever that crab lies there | none known | `src/knowwonder/KWMove.cpp` (`MoverShapeMisses`): a mover's sweep that hits an actor (not a mover, not static) also checks the actor's cylinder against the mover's brush and moves on when it touches none of it. Checked headless 2026-11-13 from the playtest's save with four crabs placed in the bucket (`@place`): the bucket and FinalStar0 go down the whole 352 units |

## Fire crab ledge logic (`hgame.firecrab`, `firecrabSmall`)

- A Flipendo hit (`HChar.HandleSpellFlipendo`, `bFlipPushable`) sets PHYS_Falling and throws the crab away from Harry
  at `fFlipPushForceXY` (260) and up at `fFlipPushForceZ` (100). `HChar.Landed` zeroes the velocity, so one hit is one
  short hop.
- `DoFlip.Tick`: once the speed is under 5 and `OnALedge(Location)` is true, `vPush = pushDirection()`, the collision
  radius shrinks by 10, and the crab goes to `FallOverLedge`. That state has no way back: its `Tick` moves the crab by
  `vPush * GroundSpeed * 3` each second (`MoveSmooth`) until it lands after a fall of more than 25 (`Landed`:
  `stayFlipped`) or is hit again.
- `pushDirection` probes `OnALedge` at 16 headings, one collision radius out, and counts the headings with floor.
  2 or fewer: (0,0,-1), so the crab doesn't move sideways. 3 to 9: away from the middle one of those, over the edge. More
  than 9: a zero vector, so the crab lies on the edge and doesn't move.
- `stayFlipped` (small crabs): Flipendo pushes at half strength from then on (130 / 40).

On the Adv6Goyle lift, `OnALedge` is true everywhere: the crab's centre is 22 above the lift and the room's real floor is
more than 35 below. In the middle of the room the push comes out as (0,0,-1). Near a side wall only the headings over
the wall find BSP, so the push is away from that wall.

Staying on its back on a mover floor is the original's behaviour and levels are built on it, so the fix doesn't touch
`OnALedge`. Ch1Rictusempra's lift pit (firecrabSmall6, issues #26 and #16): the corridor's floor there is a mover,
Mover16 (top at z -751.5, 16 above the BSP floor). The crab, flipped once, stays in `FallOverLedge`, and each Flipendo
hit moves it one hop (about 100 units) towards the pit until it drops onto Mover3 (top at -800, `Trigger6` lowers it,
event `FirecrabSmallLift`). An earlier version of the mod (2026-11-11, in 1.4) made `OnALedge` see movers instead:
the crab then counted as standing on floor, got up again after its time on its back, and needed a second hit to flip
before each push, so players got stuck at the rim with the crab shooting at them.

Each push of a crab lying on a ledge also shrinks it again: `DoFlip.Tick` sets `CollisionRadius - 10` every time it
sends the crab to `FallOverLedge`, and only `firecrab.Landed` of a large crab (`DrawScale != default.DrawScale` and
`IsA('firecrabLarge')`) gives the size back. firecrabSmall6 goes 18, 8, -2 after its first push; with a negative radius
the spell cursor can't target it and spells fly through it (seen in a playtest 2026-11-13, spellFlipendo passing 1 unit
from its axis). Same scripts in the original, not watched there. The fire crab fix (`src/hp2/HP2FireCrab.cpp`) puts the crab back to its first
radius before each shrink, so it lies at 8.

The challenge's end (the crab bucket): four `ElevatorTrigger`s (`CrabBucket1LiftTrigger1`-`4`, class proximity
`firecrabSmall`, trigger once) send `ElevatorMover3` (`CrabBucket1`) to keyframes 1-4 (z 0, -64, -128, -192, -352).
`ElevatorMover.MoveKeyframe` ignores a call while the mover is still moving (`bMoveKey` false), so two crabs arriving
within its 1 s move would lose one step for good. Checked 2026-11-13 by triggering the four tags 4 s apart: the bucket
reaches keyframe 4. With firecrabSmall25 lying under the shaft the bucket stopped at -145.7 instead (the row above).
