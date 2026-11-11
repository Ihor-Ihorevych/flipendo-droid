# Bugs in the original HP2

Bugs the retail game has that Flipendo should fix rather than reproduce. Same rules as HP1's list
([../hp1/original_bugs.md](../hp1/original_bugs.md)): each entry gives where, what happens, the known workaround in the
original, and the state of our fix. A fix that changes original behaviour is a mod in `src/hp1/mods/` (on by default,
off with `--vanilla`, [modding.md](../../modding.md)).

| Where | Bug | Workaround in the original | Fix |
|---|---|---|---|
| Adv6Goyle, the fire crab trap room (firecrabSmall0/1 on Mover22, the lift floor), issue #22 | A flipped small fire crab never turns back over, and near a side wall it slides on its back to the opposite wall. `firecrab.OnALedge` treats a spot as a ledge when `FastTrace` finds nothing in the 35 units below it, and `FastTrace` sees only the level's BSP, not movers (HP2 `execFastTrace` [HP2 0x1041AF80]: `UModel::FastLineCheck`). On the lift every spot is a ledge (details below). Seen in Flipendo 2026-11-11 (`@state firecrabSmall0 DoFlip`); the original not watched yet, but its `FastTrace` and the crab scripts are the same. | push the crab with Flipendo into the gap the west wall opens (not tested) | `src/hp1/mods/FireCrabLedgeFix.cpp` (`firecrab-ledge-fix`): `OnALedge` traces the same line against the level and movers |

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

Issue #26 (Ch1Rictusempra, a crab Flipendo barely moves at the rim of a sunken pit) runs through the same code, but
which branch it takes hasn't been checked yet.
