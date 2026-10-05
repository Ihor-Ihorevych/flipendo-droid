# Physics and movement

KnowWonder's physics modes, pawn movement and latent moves where they differ from what SurrealEngine does. Ported in
`src/knowwonder/KWPawn.cpp`, `src/knowwonder/KWMover.cpp`, `src/knowwonder/KWInterpolation.cpp`, `src/knowwonder/KWNavigation.cpp`, `src/knowwonder/KWAttach.cpp`
(`physTrailer`, [animation.md](animation.md#bones-and-attachments)), `src/knowwonder/KWCollision.cpp` (`setPhysics`) and
`src/knowwonder/KWTouch.cpp` (Touch).
Root motion is in [animation.md](animation.md#root-motion-banimmove). Addresses are HP1's.

## HP1 and HP2

Both games' `Pawn.uc` / `PlayerPawn.uc` declare the KnowWonder additions below (`MaxMountHeight`, `bAutoJump`), and both
`Mover.uc` re-declare `PhysAlpha`/`PhysRate`. In [hp2_compare.md](../reports/hp2_compare.md): `setPhysics`, `findPath`
and the latent polls (`execPollMoveTo`, `execPollMoveToward`, `execPollStrafeFacing`, `execPollWaitForLanding`) differ
only in offsets; `physWalking` (14847 -> 13679 bytes), `physFlying`, `physSwimming`, `physRolling`, `physMovingBrush`,
`physTrailer`, `APawn::performPhysics`, `physicsRotation`, `moveToward`, `Mount` and
`AInterpolationManager::performPhysics` changed, HP2 code not read yet. `IsBlockedBy` differs only in offsets.

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
  A mover's sides count too: its polygons go into the level's BSP with their flags, so Lev_Tut1b's pushable blocks
  (`GridMover`, `PF_SpecialPoly` on the four sides) can be grabbed. SurrealEngine keeps a mover's polygons in the brush's
  own BSP, whose surfaces have no flags, so `src/knowwonder/KWPawn.cpp` takes them from the polygon (`BspSurface.BrushPoly`).
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

## Touch

`AActor::BeginTouch` (0x10379FE0, byte-identical in HP2) is called by `ULevel::MoveActor` on the moving actor for
every actor it moved into: `sub_1037A0A0(Actor, Other)` and only if that returns true `sub_1037A0A0(Other, Actor)`.
`sub_1037A0A0` (0x1037A0A0) puts Other in the last free slot of Actor's `Touching[4]` and raises `Actor.Touch(Other)`;
it returns false only when that event took Other out of the slot again. A full array first drops every entry with
PHYS_None (`EndTouch` with UnTouch); still full, a Pawn Other drops the first non-pawn; still full, a `bIsPlayer` Other
(Pawn's second bool) drops the first pawn that isn't one; otherwise there is no touch.

The second side also runs when the first side's Touch destroyed Actor: `ULevel::DestroyActor` (0x103A72C0) only unlinks
actors whose `Touching` lists the destroyed one (`EndTouch(.., 1)`), and Other doesn't yet. A spell explodes
(`Destroy()`) in its own Touch (`baseSpell.Flying.ProcessTouch`), then the `spellTrigger` it hit gets
`Touch(spell)` and fires its Event: Lev_Tut1b's Flipendo wall symbol (`spellTrigger9` -> `dispatcher155`, which
opens the way). SurrealEngine linked both arrays before the first event, the spell's Destroy unlinked the trigger and
the trigger was never told, so the symbol did nothing.

## Blocking and Bump

`AActor::IsBlockedBy` [HP1 0x10352140] decides whether a moving actor stops at another one (`ULevel::MoveActor`
raises `Bump` on both, then the physics mode handles the hit) or passes through it (Touch). Ported as
`KW::IsBlockedBy` (`src/knowwonder/KWCollision.cpp`), used by `TryMove`:

- Other is the level: `bCollideWorld`.
- Other is a brush (a Mover): `bCollideWorld`, and Other's `bBlockPlayers` for a player (PlayerPawn whose dword at
  +0x4BC is set, read as `Player`), else Other's `bBlockActors`. The reverse when the moving actor is the brush.
- Otherwise both sides: a player or a Projectile looks at the other's `bBlockPlayers`, anything else at its
  `bBlockActors`.

SurrealEngine required `bBlockPlayers` on both sides for a projectile, so a spell (Projectile: `bCollideWorld`, no
blocking flags) only touched movers. Lev_Tut1b's block puzzle (`GridMover`, `state BumpMove`) moves only on `Bump`,
pushed `MoveIncrement` away from the bumper (the Flipendo spell, `Mover.IsRelevant` -> `baseSpell.IsRelevantToMover`):
the block never moved.

`AActor::physProjectile` [HP1 0x103F2AB0] raises `HitWall(Hit.Normal, Hit.Actor)` after any blocking hit, actors
included (not after a teleport or Destroy). SurrealEngine only raised it for world hits; with the block now stopping
the spell, the spell has to explode there (`Projectile.HitWall` -> `Explode`), or it stays against the block and
bumps it again when `BumpMove` re-enables Bump. Checked 2026-10-05: each cast moves `GridMover1` one step (128).

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
