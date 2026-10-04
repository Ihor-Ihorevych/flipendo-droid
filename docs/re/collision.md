# Collision: CT_Box

HP1 actors choose their collision primitive with `Actor.CollideType`. CT_Box makes the actor's primitive a `UBox`:
an oriented box centered on the actor, rotated with it. Lev_Tut1 uses it for the BlockAll walls along the Grand Hall
stairs (250 x 10 x 200), BlockPlayers, Triggers and the CutScene trigger volumes. Flipendo's port is
`kw/KWCollision.cpp` (hooks in SurrealEngine's TraceTest/OverlapTest/CollisionSystem, see
[../engine-hooks.md](../engine-hooks.md)).

## The box

`UBox::GetCollisionBoundingBox` (0x103FE130) with bWorld false: half extents CollisionRadius (X), CollisionWidth (Y,
or CollisionRadius when CollisionWidth is 0) and CollisionHeight (Z). With bWorld true it is the bounding box of the
eight corners transformed by the actor's coords.

## LineCheck and PointCheck

`UBox::LineCheck` (0x103FE620) and `UBox::PointCheck` (0x103FE590) build the local box and hand the check to two
helpers, `sub_103FC980` (line, via thunk `sub_10303166`) and `sub_103FC1D0` (point, via thunk `sub_10302D6F`). Both
take {End, Start, Extent, Actor}; with an actor they:

1. get `AActor::ToLocal` (0x1031BAC0: Location and Rotation),
2. transform the check's extent box (−Extent..Extent, axis aligned in the world) by it and keep half its size: the
   bounding box of the rotated corners, so a cylinder of radius r against a box turned 45° counts as r·√2 wide,
3. transform Start/End into the box's frame and run the axis aligned version (actor None),
4. transform the hit location and normal back to the world.

Axis aligned **point check**: the box of half size Extent at Start overlaps the target box only with more than 0.003 of
penetration on every axis (strict). On overlap the result is the shallowest push-out axis: candidates +Z, −Z, +Y, −Y,
+X, −X in that order, a later one wins only when strictly smaller; Time is that depth, Location is Start clamped to the
box.

Axis aligned **line check**:

- Start not overlapping: the target box is grown by Extent (square corners). Each face (−X, +X, −Y, +Y, −Z, +Z) the
  segment approaches (distance > −0.003) and crosses before the best time so far is tested: the crossing point, moved
  0.001 inside along the face normal, must lie strictly inside the grown box. A hit sets Time = t − 0.001 (at least 0)
  and that face's normal. Time still 1 means no hit.
- Start overlapping: the End is point-checked too. Blocked (Time 0, the Start's push-out normal) only when the End also
  overlaps and is deeper than the Start; otherwise the check passes, so an actor caught inside can move out or along
  the box.

Flipendo had approximated this with a swept cylinder (rounded corners, grown by the plain radius) and blocked any
movement into the nearest face while inside; since 2026-10-04 it is the original's box-against-box test. Driven check:
Harry still runs up the Lev_Tut1 stairs along the BlockAll banister, and the intro cutscene's kids and Dumbledore make
it up without timeout teleports.
