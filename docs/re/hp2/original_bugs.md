# Bugs in the original HP2

Bugs the retail game has that Flipendo should fix rather than reproduce. Same rules as HP1's list
([../hp1/original_bugs.md](../hp1/original_bugs.md)): each entry gives where, what happens, the known workaround in the
original, and the state of our fix. A fix that changes original behaviour is a mod in `src/hp1/mods/` (on by default,
off with `--vanilla`, [modding.md](../../modding.md)).

| Where | Bug | Workaround in the original | Fix |
|---|---|---|---|
| Adv6Goyle, the fire crab trap room (firecrabSmall0/1 on Mover22, the lift floor), issue #22 | A flipped small fire crab pushed near a side wall slides on its back to the opposite wall. `firecrab.OnALedge` treats a spot as a ledge when `FastTrace` finds nothing in the 35 units below it, and `FastTrace` sees only the level's BSP, not movers (HP2 `execFastTrace` [HP2 0x1041AF80]: `UModel::FastLineCheck`). On the lift every spot is a ledge, so the crab stays on its back (`FallOverLedge`), and `pushDirection` finds floor only in the headings that reach past a wall (details below). Seen 2026-11-13 with `--vanilla`: firecrabSmall0 pushed north with Flipendo to y 469 slid back to the south wall (y 152) and stayed there. | push the crab with Flipendo into the gap the west wall opens (not tested) | `src/hp1/mods/FireCrabLedgeFix.cpp` (`firecrab-ledge-fix`): `pushDirection` returns (0,0,-1) for a crab based on a mover, so it lies where it landed. `OnALedge` stays the original's (below: Ch1Rictusempra needs it) |

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

The challenge's end (the crab bucket): four `ElevatorTrigger`s (`CrabBucket1LiftTrigger1`-`4`, class proximity
`firecrabSmall`, trigger once) send `ElevatorMover3` (`CrabBucket1`) to keyframes 1-4 (z 0, -64, -128, -192, -352).
`ElevatorMover.MoveKeyframe` ignores a call while the mover is still moving (`bMoveKey` false), so two crabs arriving
within its 1 s move would lose one step for good. Checked 2026-11-13 by triggering the four tags 4 s apart: the bucket
reaches keyframe 4.
