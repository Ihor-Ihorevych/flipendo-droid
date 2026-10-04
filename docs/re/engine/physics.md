# Physics and movement

KnowWonder's physics modes, pawn movement and latent moves where they differ from what SurrealEngine does. Ported in
`kw/KWPawn.cpp`, `kw/KWMover.cpp`, `kw/KWInterpolation.cpp`, `kw/KWNavigation.cpp`, `kw/KWAttach.cpp`
(`physTrailer`, [animation.md](animation.md#bones-and-attachments)) and `kw/KWCollision.cpp` (`setPhysics`).
Root motion is in [animation.md](animation.md#root-motion-banimmove). Addresses are HP1's.

## HP1 and HP2

Both games' `Pawn.uc` / `PlayerPawn.uc` declare the KnowWonder additions below (`MaxMountHeight`, `bAutoJump`), and both
`Mover.uc` re-declare `PhysAlpha`/`PhysRate`. In [hp2_compare.md](../reports/hp2_compare.md): `setPhysics`, `findPath`
and the latent polls (`execPollMoveTo`, `execPollMoveToward`, `execPollStrafeFacing`, `execPollWaitForLanding`) differ
only in offsets; `physWalking` (14847 -> 13679 bytes), `physFlying`, `physSwimming`, `physRolling`, `physMovingBrush`,
`physTrailer`, `APawn::performPhysics`, `physicsRotation`, `moveToward`, `Mount` and
`AInterpolationManager::performPhysics` changed, HP2 code not read yet.

## setPhysics

`AActor::setPhysics` (0x103E5140): switching to PHYS_None or PHYS_Rotating zeroes Velocity and Acceleration, so the
actor stops dead. HP1's cutscenes depend on it ([cutscenes.md](../hp1/cutscenes.md)). The original's Base handling
(FindBase/SetBase by physics mode) is left to SurrealEngine; not ported.

## Walking, falling, ledges

- **Out of the world.** Only `physWalking` (0x103E6B60) and `physFalling` test `Region.ZoneNumber == 0` and raise
  `FellOutOfWorld`; `physFlying`, `physSwimming` and `physRolling` never do. (SurrealEngine checked every pawn mode,
  which killed HP1's flying `tut1Peeves0`, parked outside the BSP, on the first tick.)
- **Walking off a ledge** (`physWalking`, and the same in `physRolling` for PHYS_Rolling): Velocity.Z = 0 (walking),
  the `Falling` event, then PHYS_Falling only if the script left the physics alone.
- **No floor ahead:** `MayFall` once (if bCanJump and probed) lets the script decide; a pawn that can't jump, or walks
  (`bIsWalking`), stops dead at the ledge (back to OldLocation, MoveTimer = -1).
- **Auto jump** (`PlayerPawn.bAutoJump`, "Auto Jump" in the options; the block before `eventDoJump` in `physWalking`):
  walking off a ledge, find the edge just behind the feet (a ray from below the feet backwards along the walk direction
  hitting the ledge's face, facing forward by more than 0.25), predict where a fall from there lands with and without
  JumpZ, and if jumping lands more than 10 units higher, move to the edge and raise `DoJump(1)`. The fall prediction is
  `sub_103E6310` (not exported): steps of at most 0.2 s, sweeping the pawn's cylinder and sliding along walls; it ends
  on a floor (normal.z > 0.7), on a mountable wall the pawn faces, or after maxTime or 10 steps.
- **Ledge grabbing**, `APawn::Mount` (0x103EBFB0): `physFalling` calls it with (0,0,1) when a falling pawn hits a wall,
  `stepUp` with -GravDir when a walking pawn runs into one. Only BSP surfaces flagged `PF_SpecialPoly` (0x1000,
  KnowWonder's "mountable") qualify, and the pawn must face the wall. It looks for a floor at most `Pawn.MaxMountHeight`
  up (a KnowWonder property; HP1's Harry: 96.5) just behind the wall, checks the way up and over is clear, sets the base
  and raises `Pawn.Mount(ledge - Location)`; the script's mounting states do the climb (HP1:
  [animation.md](animation.md#root-motion-banimmove)). The surface hit is found by `sub_103FF1B0` / `sub_103FEBD0` (not
  exported): walk the hit node's coplanar chain for the polygon containing the hit location.
- `execWaitForLanding` (0x103D6EB0) sets LatentFloat = 2.5 and is latent only while falling; the poll is done once the
  pawn stops falling and raises `LongFall` every tick after LatentFloat runs out.

## Pawn rotation

`APawn::performPhysics` (0x103E5520) keeps a running average of the physics step, and turns every pawn (the player
too) towards DesiredRotation after its movement via `physicsRotation` (0x103E5950), unless PHYS_Spider. Yaw and pitch
turn at RotationRate; roll is cleared, or with RotationRate.Roll > 0 eased back to level while walking. HP1's cutscene
movement (MoveSmooth + DesiredRotation) depends on it. The flying/swimming bank from lateral acceleration is not
ported.

## Latent moves

`APawn::moveToward` (0x103D96F0), used by the MoveTo / MoveToward / StrafeTo polls (stock UE1 behaviour, which
SurrealEngine approximated): reached at 16 units horizontally (and |dz| < max(CollisionHeight, 48), dz = 0 when
walking); the sideways velocity component is damped when moving fast, so the path bends towards the target;
DesiredSpeed is halved once when the next physics step would pass the target; air control only in low gravity zones.

- `execPollMoveToward` (0x103D89C0): Destination = MoveTarget's location (flying towards a pawn aims at 0.7 of its
  height), Focus = Destination. A walking pawn with bAdvancedTactics gets `AlterDestination` for this step, which is
  then put back. A pawn target keeps DesiredSpeed; one in a water zone ends the move for a pawn that can't swim.
- `execPollStrafeFacing` (0x103D9010): Focus = FaceTarget's location, moves to Destination (AlterDestination may change
  it for this step).
- `Pawn.FindPath` (native 553, KnowWonder's): the first step from startPoint towards the navigation point named
  DestName, used by HP1's station-to-station AI (`tut1Peeves` and the other "basestation" patrollers).

## Movers

`Mover.uc` re-declares `PhysAlpha` and `PhysRate`, shadowing Actor's. Mover script (InterpolateTo etc.) and
`AActor::physMovingBrush` (0x104061F0) use the Mover copies; SurrealEngine's TickMovingBrush used Actor's, which stay
0, so doors were triggered but never moved. `physMovingBrush` also has KnowWonder additions such as a gravity term,
not reversed ([original_bugs.md](../hp1/original_bugs.md#lumos-lesson-platform)).

## InterpolationManager

`Engine.InterpolationManager` (native) is spawned by `Actor.StartInterpolation` and by HP1's broom/Quidditch scripts
(BroomHarry, QuidPlayer, QuidditchPawn) to fly its Owner along a path of InterpolationPoints. The manager has
PHYS_Interpolating; its `performPhysics` (0x103F7BA0) moves the Owner (PHYS_None with bInterpolating) along cubic
Bezier segments and raises `UpdateCamera`, `InterpolationPoint.InterpolateEnd` and `FinishedInterpolation`.
KnowWonder's InterpolationPoint is not UT's: arrays per pause (`Pause[8]`, `ViewTargetTag[8]`, ...), Bezier control
points, DesiredSpeed/PathDist (HP2's has the same `Pause[8]` arrays).
