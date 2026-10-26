# Physics and movement

KnowWonder's physics modes, pawn movement and latent moves where they differ from what SurrealEngine does. Ported in
`src/knowwonder/KWPawn.cpp`, `src/knowwonder/KWMover.cpp`, `src/knowwonder/KWInterpolation.cpp`, `src/knowwonder/KWNavigation.cpp`, `src/knowwonder/KWAttach.cpp`
(`physTrailer`, [animation.md](animation.md#bones-and-attachments)), `src/knowwonder/KWCollision.cpp` (`setPhysics`) and
`src/knowwonder/KWTouch.cpp` (Touch).
Root motion is in [animation.md](animation.md#root-motion-banimmove). Addresses are HP1's.

## HP1 and HP2

Both games' `Pawn.uc` / `PlayerPawn.uc` declare the KnowWonder additions below (`MaxMountHeight`, `bAutoJump`), and both
`Mover.uc` re-declare `PhysAlpha`/`PhysRate`. In the HP1 vs HP2 code comparison: `setPhysics`, `findPath`
and the latent polls (`execPollMoveTo`, `execPollMoveToward`, `execPollStrafeFacing`, `execPollWaitForLanding`) differ
only in offsets; `physWalking` (14847 -> 13679 bytes), `physFlying`, `physSwimming`, `physRolling`, `physMovingBrush`,
`physTrailer`, `APawn::performPhysics`, `physicsRotation`, `moveToward`, `Mount` and
`AInterpolationManager::performPhysics` changed, HP2 code not read yet (for the last one only the final move is read,
[InterpolationManager](#interpolationmanager)). `IsBlockedBy` differs only in offsets.
The turn natives changed too (`execTurnTo` 250 -> 295 bytes, `execTurnToward`, `rotateToward`): HP2 flattens
TurnTo's focal point while walking ([hp2/engine.md](../hp2/engine.md#pawn-turning)).

## The full port (src/knowwonder/KWPhysics.cpp, KWMove.cpp)

Since 2026-10-05 KnowWonder games run HP1's own physics instead of SurrealEngine's (`FLIPENDO_SE_PHYSICS=1` brings
SurrealEngine's back, to compare). Layers, each on the one below:

- collision checks ([collision.md](collision.md#bsp-checks)): `KWBspCheck.cpp`, `KWLevelCheck.cpp`;
- moving actors (below): `KWMove.cpp`, and the natives that move or trace (`KWMoveNatives.cpp`: Move, MoveSmooth,
  SetLocation, SetRotation, Trace, FastTrace, AutonomousPhysics, SetBase, IsOverlapping, SetPhysics);
- physics modes: `KWPhysics.cpp`: walking, falling, flying, swimming, projectile, rolling, rotation, landing, hit-wall.
  Spider physics keeps SurrealEngine's code: no HP1 script uses PHYS_Spider.

`AActor::Tick` (0x103B3840) calls `performPhysics` once per tick with the whole frame time, after the script Tick,
latent code, Timer and LifeSpan, and only when Physics isn't PHYS_None and Role isn't ROLE_AutonomousProxy. The modes
split it themselves (falling: steps of at most 0.1 s, walking 0.05 s, up to 8). SurrealEngine split every tick into
0.02 s steps before the Timer.

The latent code is `UObject::ProcessState`, called right after the script `Tick` event: it polls the latent action
(`execPollMoveTo`, `execPollTurnToward`, ...), and when that ends it runs the state code on in the same tick. So a
latent move or turn sees where the actor's own `Tick` put it this frame. SurrealEngine polled a pawn's latent action
in `UPawn::Tick`, before the `Tick` event. BaseCam's `RotateAroundHarry` (the Wizard Card pickup) moves the camera in
`Tick` and turns it with a `sleep(0.0005); turntoward(p)` loop. Polled first, the poll aimed from last frame's spot,
so the yaw moved only every other frame (+220, 0, +220) and the view shook left and right. Flipendo polls after the
`Tick` event for KnowWonder (`UPawn::PollLatentAction`, called from `UActor::Tick`). Checked in HP1; HP2's
`AActor::Tick` not read yet.

The latent moves take their first step when they start: `execMoveTo` (0x103D8580), `execMoveToward` (0x103D87A0),
`execStrafeTo` (0x103D8C60) and `execStrafeFacing` (0x103D8E60) end with `rotateToward(Focus)` and
`moveToward(Destination)`, which sets the acceleration. SurrealEngine only stored the destination and left the step
to the poll. A script that restarts its move every tick then never moves: `Doxie1.MoveToScowl`'s `Tick` does
`gotostate('MoveToScowl')` each frame while it is more than 128 units from Harry, back to `Begin` and its
`moveto(playerHarry.Location)`, before the poll ever runs, so every woken doxie hovered in place (Lev2_Fire2). Ported
(`KW::PawnStartMove`). `execMoveToward` gives a pawn target a MoveTimer of 1.2 s (SurrealEngine 1.0), and
`execStrafeFacing` sets StrafeTo's DesiredSpeed and Focus = the face target. HP2 not checked.

`AActor::performPhysics` (0x103E52C0) handles falling, projectile, rolling, moving brush and trailer only; a non-pawn in
PHYS_Walking or PHYS_Interpolating doesn't move by itself (an InterpolationManager moves its owner).
`APawn::performPhysics` (0x103E5520) adds walking, swimming, flying, spider, then turns the pawn (below; which pawns
differs between the games), counts MoveTimer down and averages AvgPhysicsTime. Both raise
PostTouch. Nothing else counts MoveTimer down, and a pawn without physics never does. SurrealEngine's `UPawn::Tick` did it as
well, so with this port every latent move's timer ran out at half its time: gnomes (`Tut1Gnome.hide`) gave up their
`MoveTo` halfway, short of the 100 units the state waits for, and ran on the spot; ones that got close glided past with
the move's acceleration still set (fixed 2026-10-05).

### Moving actors

- `ULevel::MoveActor` (0x103AA3A0): static or non-movable actors don't move; a move under 0.0001 with no rotation change
  succeeds at once (a non-mover with nothing standing on it just takes the rotation). It sweeps the primitive's world
  collision box (a mover's shrunk by 0.51, everything else 2 units further than the move) through `MultiLineCheck`; the
  first hit that isn't ignored (bIgnorePawns: pawns and decorations that aren't static; bIgnoreBases: what the actor
  stands on; always: what stands on the actor) and blocks it (`IsBlockedBy`) stops it, the time pulled back by the
  probe. What stands on it is carried (the yaw change turns them, a pawn's ViewRotation too). The arm from the based
  actor to the mover is turned by `GMath.UnitCoords *= FRotator(0, turn, 0)` (yaws masked with 0xFFFC): the inverse of
  SurrealEngine's `Coords::Rotation`, which is `GetAxes`' frame (for a pure yaw `Coords::Rotation` and
  `Coords::InverseRotation` are the same matrix, so only `.Inverse()` flips it). With `Coords::Rotation` the carried
  actor swung the opposite way round the mover: Lev4_Sneak2's mirror scene camera (`CutCameraPos29`, attached to
  `Mover1` by AttachTag, which turns -90°) ended behind the mirror instead of in front of it. Non-pawns check
  encroachment (a refused one cancels the move). Then Bump both ways (not against the level or its own base),
  BeginTouch for the non-blocking hits before the stop (unless it blocks actors and players and no hit was looked at),
  EndTouch for touches no longer overlapping, SetActorZone.
  HP2's movers of `MoverEncroachType` 4-7 skip the sweep ([hp2/engine.md](../hp2/engine.md#mover-sweep)).
  A turning HP2 mover also pushes by its turn ([hp2/engine.md](../hp2/engine.md#turning-movers-push)).
- `AActor::IsBasedOn` (0x10352430): this stands on other, through its base chain. **SurrealEngine's `a->IsBasedOn(b)`
  is the other way round** ("b stands on a"); `KW::IsBasedOn(a, b)` is HP1's. Mixing them up skipped the floor under
  every pawn standing on the level (Harry fell through Lev_Tut1's floor in the intro).
- `AActor::IsOverlapping` (0x10379D90): a brush never overlaps anything (touches with movers end at once); otherwise the
  plain cylinders (no CollisionWidth offset), strictly within the summed radii and heights.
- `ULevel::CheckEncroachment` (0x103AB5F0): actors that collide at the new place (`ActorEncroachmentCheck`: other
  actors' cylinders point checked against this actor's primitive, other movers skipped) and that it blocks; a mover
  first pushes each along (`moveSmooth`), and only what is still in the way afterwards gets EncroachingOn (true refuses
  the move; the pushed actor is put back). Then EncroachedBy for blocked actors, touch for the rest.
  **A mover based on what it pushes** loops: HP2's Entryhall_hub has `Mover33` (Tag `StatueBase01`, the plinth beside
  Gregory the Smarmy's statue that slides aside to open a hole) with `Base=StatueGregorySmarmy0` in the map. Opening,
  it encroaches the statue and pushes it; the statue's MoveActor carries what stands on it, `Mover33`, before the statue
  itself has moved, so `Mover33` encroaches it again: MoveActor, CheckEncroachment, moveSmooth, MoveActor ... until the
  stack overflows (`@trigger StatueBase01` crashes every time). Flipendo doesn't carry an actor whose MoveActor is
  still running further up the stack; the statue then stays where it is and the plinth opens (it looks right). How
  the original avoids the loop isn't checked in IDA (neither game).
- `ULevel::FarMoveActor` (0x103A9DE0): FindSpot (bCollideWorld, or bCollideWhenPlacing off clients), encroachment with
  touch, unbases what stood on it, bJustTeleported, OldLocation = Location.
- `ULevel::FindSpot` (0x103A9690): its last argument decides whether a spot that is already free is kept (SpawnActor
  passes 1); FarMoveActor passes 0, so the push-out steps always run: `AdjustSpot` (0x103A9570, a ray towards a test
  point, pushed back along the hit normal by (1.05 - time) times the extent) along -X, -Y, -Z, +X, +Y, +Z, then the 8
  diagonals; it fails when that moved the spot more than sqrt(1.5) times the extent or the spot still isn't free.
  `ULevel::SpawnActor` (0x103A65A0) places bCollideWorld / bCollideWhenPlacing actors with it (SurrealEngine's
  CheckLocation now calls it).
- `ULevel::SetActorZone` (0x103ACDD0): ActorLeaving, then ZoneChange **before** Region changes (the script still sees
  the old zone), then ActorEntered; a pawn also gets FootZoneChange (Location - CollisionHeight) and HeadZoneChange
  (Location + EyeHeight). No carcass or inventory destruction (SurrealEngine's UpdateActorZone had UT's).
- `AActor::moveSmooth` (0x103E4C30): on a hit, HitWall, then the rest along the wall, then `TwoWallAdjust`
  (0x1031C3E0) along a second wall. `AActor::FindBase` (0x103E4FD0): 8 units down with the cylinder.
- Script movement (`baseChar` moves itself with MoveSmooth), the InterpolationManager, physTrailer and movers all go
  through these; with half of them on SurrealEngine's movement, students walked into Lev_Tut1's walls and fell out of
  the world.

### Falling (physFalling 0x103EEA20)

Air control first (pawns): AirControl over 0.15 is cut to 0.05 when a trace a step ahead hits something; the
acceleration is capped at AirControl * AccelRate (more below a horizontal speed of 10; at GroundSpeed or more only that
speed is kept, or with little air control hardly any acceleration). Each step integrates half of gravity plus
acceleration (in water with buoyancy and fluid friction; a bBobbing decoration with half gravity; a player whose feet
are in a water zone, falling, with that zone's friction), splits once at the top of the arc, adds ZoneVelocity (a player
only over 200), moves, and averages the velocity with the step's displacement (capped at ZoneTerminalVelocity). Hits:
bBounce raises HitWall; a floor (normal.z > 0.7) lands (`processLanded`); a wall tries a ledge grab (`Mount`), HitWall
(`processHitWall`), and slides along it and a second wall, landing on a ditch or floor.

`processHitWall` (0x103ECF20): never against pawns. A pawn steering into the wall (towards Destination, flat when
walking, within MinHitWall) gets HitWall only if its script probes it (falling: always); otherwise its move ends
(MoveTimer -1, bFromWall). HP2's (0x103F9D30) only sets bFromWall and leaves MoveTimer alone, so the MoveTo /
MoveToward goes on along the wall (otherwise the same function). `processLanded` (0x103ED210): a decoration landing over an edge rolls off it (a ray under it
finds nothing, four short traces under its corners pick the way), 5 times at most; a bSlidingCarcass on a slope slides;
a non-pawn in a bBounceVelocity zone bounces; else Landed, then walking (pawns, spending the time left) or none.

HP2's `physFalling` [HP2 0x103FB560] (compared at the bounce only, 2026-10-25): bBounce raises HitWall only when the hit
actor isn't a Mover; landing on a mover takes the floor/wall path above (`processLanded` on a floor), so a bouncing
actor comes to rest on a mover with the mover as its base and rides it. HP1 bounces off anything.

### Walking (physWalking 0x103E6B60)

`calcVelocity` (0x103EB3E0) with ground friction: acceleration capped at AccelRate (a walking player 30%), friction
turns the velocity towards it; braking in 0.03 s steps, stopping dead under 10; non-players' MaxSpeed times
DesiredSpeed; a walking player slows towards 30% of it. ZoneVelocity x25 (a player only over 300). Then per step:

HP1 and HP2: HP2's physWalking (0x103F4130) calls calcVelocity (0x103F8400, the same code) without braking, so a pawn
whose acceleration stops slows by friction alone (Harry: about 0.45 s and 13 units from running speed, against 0.1 s and 4
in HP1's way), and with no ground friction while `Pawn.bNoZoneFriction` (HP2 only; HPawn's and Pawn's patrol states set it
and clear it when they end). The rest of HP2's physWalking is not compared yet.


- AI pawns that can't fly, and walking players, look ahead for a ledge (a probe one radius ahead, half without
  bAvoidLedges, down past MaxStepHeight). At one: the edge face under it; none found: a side step either way; found:
  back away (bAvoidLedges; bStopAtLedges also ends the move) or slide along it. MayFall once; a pawn that can jump walks
  off, one that can't stops (MoveTimer -1, or -0.1 by chance). A walking player just stops at the edge.
- bHitSlopedWall (the last step hit a wall sloped 0.01..0.7): slide along it; a player probes for one 100 ahead and
  45 degrees either side.
- The move, `stepUp` on a hit (0x103EC690: a ledge grab first, else up MaxStepHeight, the move, down; a player pushes a
  bPushable decoration hit head on, the velocity shared by mass; a short shallow hit steps up again).
- The floor (MaxStepHeight + 2 down with the cylinder): kept (lifted to 2.1 above it), snapped down to, slid down a
  slope (normal.z * ZoneGroundFriction < 3.3), or lost: MayFall, and a pawn that can't jump or walks (bIsWalking) is put
  back where it started (FarMoveActor, MoveTimer -1); else Falling and physFalling with the time left.
- PlayerPawn.bAutoJump: walking off an edge whose face (a ray behind the feet) faces forward by more than 0.25, if a
  jump from the edge lands more than 10 higher than a fall (`sub_103E6310`, steps of 0.2 s), DoJump(1).
- At the end the velocity is the step's displacement, projected on the floor (keeping its speed).

The ledge grab (`APawn::Mount` 0x103EBFB0) finds the wall's surface from the hit's node (`Item`) along its coplanar chain
(`sub_103FEBD0`: the polygon facing the hit that contains the hit point within the pawn's largest extent; a mover's hit
taken into its brush's frame first).

### Flying, swimming, projectiles, rolling

- `physFlying` (0x103F13A0): only a flyer that collides with the world and isn't a player is destroyed outside it ("flew
  out of the world"; Lev_Tut1's Peeves waits outside without bCollideWorld). calcVelocity with AirSpeed and fluid
  friction, ZoneVelocity (a player only over 300), the move; a steep wall, or moving mostly up or down, slides along it
  (HitWall, a second wall), anything else steps up.
- `physSwimming` (0x103F20A0): a rise slows while the head is out of the water; calcVelocity with WaterSpeed, fluid
  friction and buoyancy; `Swim` (0x103F1C10) moves and goes back to the water line (`findWaterLine` 0x103F1ED0 bisects
  the zones along the move to a unit); out of the water: falling, with a hop (40 + 0.4 x horizontal speed).
  `startSwimming` (0x103F0EA0): into the water back to the water line, the velocity averaged like falling (at most 4000),
  a dive slowed to at least 80 down.
- `physProjectile` (0x103F2AB0): up to 8 passes; velocity plus acceleration (water friction), capped at MaxSpeed,
  moved by the **whole frame time**; a hit raises HitWall (the level's LevelInfo for the world); a bouncing projectile
  goes on with the time left (twice). Destroyed outside the world.
- `physRolling` (0x103F3040): walking without the AI: friction turns the velocity towards the acceleration, steps of
  0.1 s, a probe 16 down for the floor (slopes slide it), losing it: Falling and physFalling.
- `TraceActors` (0x1040DF50) lists every hit of MultiLineCheck by time, the level's LevelInfo included, **without a
  class filter** (SurrealEngine filtered by BaseClass and left the level out); BaseCam, Target and Tut1Gnome use it.

### Pawn rotation (APawn::physicsRotation 0x103E5950)

Yaw and pitch turn towards DesiredRotation at RotationRate (`fixedTurn` 0x103E57E0, bRotateToDesired on). Roll: none
without RotationRate.Roll; eased back to level when walking slower than 200 or accelerating less than 100; else banked
by the sideways acceleration (x28000, x4096 walking, over AccelRate) up to RotationRate.Roll, eased in at 5x the frame
time. The new rotation goes through MoveActor, so what stands on the pawn turns with it. Other actors
(`AActor::physicsRotation` 0x103E5FB0) turn with bRotateToDesired / bFixedRotationDir and raise EndedRotation on
arrival.

### Sight (APawn::LineOfSightTo 0x103DA070)

The `LineOfSightTo` native (514) asks with bMaySkipChecks off, `CanSee` (533) with it on; both only trace the level
(FastLineCheck), never actors. The pawn's Enemy is seen from the eyes (Location + BaseEyeHeight) or the feet, and then
LastSeeingPos / LastSeenPos are set. Range: with bMaySkipChecks, SightRadius (for a pawn scaled by its Visibility / 128,
capped at 4000 for a player pawn, ~3464 otherwise), and the target must be in front: Stimulus = (forward . direction -
PeripheralVision) times 0.8 when positive, else times 0.17, plus 0.2 (no sight at 0 or less); height counts less with
Skill (|dz| / (Skill + 1)), and the distance is divided by Stimulus. Without it: 5000 (player) or 4000 times
min((Visibility + 16) * 0.015, 1) for a pawn, 4000 / 3000 for anything else. Over 1000 away only the eye-to-centre line
counts, and a far pawn may be skipped: when bLOSflag is clear and it is beyond half the range, or for a non-player half
the time at random. Near, the eyes look at 0.8 of the target's height, and a pawn within 500 also at two of the four
corners of its cylinder (the two that are neither nearest to nor furthest from the world origin, as HP1 measures them).
bLOSflag flips on every CanSee call (not for the Enemy) and alternates the expensive checks.

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
  exported): walk the hit node's coplanar chain for the polygon containing the hit location. Each edge's normal is
  `cross(planeNormal, edge)`; a point fails an edge when it lies more than the tolerance (the pawn's largest extent)
  on the negative side times `side`, and failing the first edge only flips `side` (the winding). Getting the cross
  product backwards (Lev_Tut1b, 2026-10-05) rejects a hit inside the polygon whenever it is within the tolerance of
  the first edge: Harry couldn't grab the 64-unit step up to the optional room's challenge star.
  A mover's sides count too: its polygons go into the level's BSP with their flags, so Lev_Tut1b's pushable blocks
  (`GridMover`, `PF_SpecialPoly` on the four sides) can be grabbed. SurrealEngine keeps a mover's polygons in the brush's
  own BSP, whose surfaces have no flags, so `src/knowwonder/KWPawn.cpp` takes them from the polygon (`BspSurface.BrushPoly`).
- `execWaitForLanding` (0x103D6EB0) sets LatentFloat = 2.5 and is latent only while falling; the poll is done once the
  pawn stops falling and raises `LongFall` every tick after LatentFloat runs out.

## Pawn rotation

`APawn::performPhysics` (0x103E5520) keeps a running average of the physics step, and turns pawns towards
DesiredRotation after their movement via `physicsRotation` (0x103E5950), never in PHYS_Spider: the player always, any
other pawn when its Rotation isn't DesiredRotation or its RotationRate.Roll > 0 (until 2026-10-21 Flipendo had this the
other way round, the player only when not at DesiredRotation and every other pawn always; no difference seen in
Lev_Tut1's opening cutscene). Yaw and pitch
turn at RotationRate; roll is cleared, or with RotationRate.Roll > 0 eased back to level while walking. HP1's cutscene
movement (MoveSmooth + DesiredRotation) depends on it. The flying/swimming bank from lateral acceleration is not
ported.

HP1 and HP2: HP2's (0x103F2B80) turns no pawn in PHYS_Interpolating either (its InterpolationManager sets the rotation),
the player always, and another pawn when it has bRotateToDesired and isn't at DesiredRotation, or has bFixedRotationDir
without bRotateToDesired; RotationRate.Roll plays no part. Right after the physics switch it calls an unexported helper
(sub_10302CFC, HP2's [stair smoothing](../hp2/engine.md#stair-smoothing)) that HP1 lacks.

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

## Turning

`APawn::rotateToward` (0x103D9E90): DesiredRotation = the direction to the focal point, yaw masked to 0..65535, pitch
0 for a walking pawn unless its MoveTarget is a pawn; done when the yaw is within **100** units (about half a degree)
of the current yaw, either way round the circle. PHYS_Spider is always done. (SurrealEngine stopped at 2000 units,
11 degrees short, so HP1's cutscene turns ended early.) The MoveTo / MoveToward / StrafeTo polls call it as is.

`execTurnTo` (0x103D93F0) clears MoveTarget, sets Focus and starts the latent turn; `execTurnToward` (0x103D9130)
sets FaceTarget and Focus to its location. Both, and their polls (`execPollTurnTo` 0x103D9530, `execPollTurnToward`
0x103D9290, which keeps Focus on FaceTarget and ends when FaceTarget is gone), first give a flying or swimming pawn
that can't strafe `Acceleration = facing * AccelRate`, then call rotateToward; TurnTo/TurnToward do it once when they
start, so the pawn turns that same tick. Port: `KW::PawnRotateToward`, `KW::PawnTurnStep` (`src/knowwonder/KWPawn.cpp`).
HP2's execTurnTo also flattens Focus.Z for a walking pawn ([hp2/engine.md](../hp2/engine.md#pawn-turning)).

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
0, so doors were triggered but never moved. Ported whole in `KW::PhysMovingBrush` (`src/knowwonder/KWMover.cpp`).

HP2's `physMovingBrush` [HP2 0x104137A0] (read 2026-10-21) is HP1's (both clamp the key index at 8) with two
additions. `MoverGlideType` 2, `MV_SpringByTime`: into the first and the last key (moving towards a middle key it is
linear) the mover moves linearly over the first `(MoveTime - MoverSpringTime) / MoveTime` of the move (0.75 when that
isn't between 0 and 1), then makes `MoverFluctuations` (0 counts as 1) triangle dips back from the end, sharing the
rest of the move evenly, the first `MoverMaxAmplitude` deep (0.1 when not between 0 and 1), each next one half as deep.
A blocked move stores the hit's location and normal in the mover's `HitPosition` / `HitNormal` (no script reads them).
Flipendo: `IsHP2()` branches in `KW::PhysMovingBrush`. Which HP2 maps use the spring glide wasn't checked.
Checked in HP1. It first walks the actor's class chain and returns unless the actor is a Mover: any other actor with
`PHYS_MovingBrush` doesn't move (Lev2_Inc_A's `spellTrigger2`, Tag `Shelf2`, has `Physics=9` in the map). While
`bInterpolating`, each step:

- **Falling** (KnowWonder's addition, [original_bugs.md](../hp1/original_bugs.md#lumos-lesson-platform)): a mover
  with `bCollideWorld` (AActor+476 bit 1; bit 0 is `bCollideActors`) in a zone first moves by its velocity along
  `ZoneGravity` times dt plus ½·g·dt², and adds g·dt to Velocity. What it actually moved is added to the current key
  (`KeyPos[KeyNum]`, AMover+788) and to `OldPos` (+992), so the rest of the move happens at the new height; that step
  skips the end-of-move check below. If it couldn't move at all it calls `FindBase` (not ported).
- **The key move**: alpha = PhysAlpha + PhysRate·dt (any time past 1 is left for the next pass), smoothstepped for
  `MV_GlideByTime`, then `MoveActor` to OldPos + (BasePos + KeyPos[KeyNum] − OldPos)·alpha (BasePos +980) with the
  rotation the same way. PhysAlpha advances by the fraction moved. Moved all the way to alpha 1: `bInterpolating` off and
  `KeyFrameReached` (Mover's calls `InterpolateEnd(self)`). **Blocked part way: `bInterpolating` off, no event**;
  GridMover's `Tick` notices (`bDoingInterpolation && !bInterpolating`) and goes to `DoneMoving`. SurrealEngine
  instead retried the blocked move every tick.

HP2's Ch2Skurge `LumosFloor` (`Mover0`, `ME_IgnoreWhenEncroach`) stops 16 units into its move in Flipendo, blocked by
`GreenPurpleCheckerBean6`, which rests on the level floor next to it (2026-10-25). The bean is an `HProp` (an `HPawn`,
`bCollideWorld`) with `bBounce`; `HProp.BounceIntoPlace.HitWall` halves and mirrors its velocity and nothing ever stops
it, so it stays `PHYS_Falling` with no base, bouncing on the floor by a fraction of a unit. HP2's `MoveActor`
[HP2 0x103B3E80] sweeps a mover with `MoverEncroachType` < 4 against actors and blocks it on any with `bCollideWorld`
(its inlined IsBlockedBy, the same as HP1's), `physMovingBrush` passes no ignore flags, and the bean's cylinder line
check is HP1's, so by the code the original stops there too. An Android fork lets a mover through actors that block
neither actors nor players (HP2 only); that rule is not in HP2's code and isn't ported. Open: whether the original's
floor stops for the bean (or the bean is meant to be picked up first); check in the original game.

`ULevel::MoveActor` (0x103AA3A0) sweeps the world collision box of any actor with `bCollideActors` or `bCollideWorld`,
brushes included (for a brush the primitive's world bounding box, shrunk by 0.51 on each side), and checks the level
when `bCollideWorld` is set. Only GridMover sets `bCollideWorld` among HP1's movers (Mover leaves it off), so the
Flipendo blocks stop at walls and drop into holes. SurrealEngine's TryMove never traces brushes against the world.
Lev_Tut1b's optional room: `GridMover0` (MoveIncrement 96), pushed west twice from the moving pads, goes over a hole
96 deep at x −3184 and drops into it (z 1104 → 1008), low enough to climb from it to the ledge (checked 2026-10-05
with `@bump`).

## InterpolationManager

`Engine.InterpolationManager` (native) is spawned by `Actor.StartInterpolation` and by HP1's broom/Quidditch scripts
(BroomHarry, QuidPlayer, QuidditchPawn) to fly its Owner along a path of InterpolationPoints. The manager has
PHYS_Interpolating; its `performPhysics` (0x103F7BA0) moves the Owner (PHYS_None with bInterpolating) along cubic
Bezier segments and raises `UpdateCamera`, `InterpolationPoint.InterpolateEnd` and `FinishedInterpolation`.
KnowWonder's InterpolationPoint is not UT's: arrays per pause (`Pause[8]`, `ViewTargetTag[8]`, ...), Bezier control
points, DesiredSpeed/PathDist (HP2's has the same `Pause[8]` arrays).

### HP1 and HP2

HP2's `performPhysics` (0x10403E60) moves the Owner with the rotation it worked out only when the Owner's
`bInterpolating_IgnoreRot` is off (the bool after `bInterpolating` in HP2's `Actor.uc`, bit 0x10 of the byte whose 0x08
bit the loop tests as `bInterpolating`); with it on, the Owner moves along the path and keeps its own `Rotation`. The
turn smoothing before the move still runs and updates `TurnRateX/Z`. HP1's Actor has no such flag.
`Pawn.FollowSplinePath` sets it when called with `bDontAlignAlongPath` and never clears it (only BroomHarry,
QuidditchPlayer and QuidditchPawn clear it); the cutscene command `FollowSpline` passes `bDontAlignAlongPath` unless it
has the `Align` option. So PrivetDr's intro camera (`BaseCam FollowSpline CameraCrane EaseBetween ...`, no `Align`)
keeps the rotation BaseCam gives it (towards its target, which follows its own spline) instead of turning with the
crane path's points, which looked down at the map's edges. The rest of HP2's function is not compared with HP1's yet.


## AttachTag parent direction

HP2 Engine.dll UGameEngine::LoadMap (0x103A5430, attachment loop 0x103A7002..0x103A7016)
calls SetBase on the child whose AttachTag matches the parent's Tag, and stops at the
first match. InitBase had the direction reversed: it based the parent on its child.
In Ch2Skurge this leaves the lower Flipendo target at its old position when the SwingBar
rotates. InitBase now bases the child on the matching parent. This correction uses no
level or actor names. Existing saved transforms are not rewritten or inferred.
