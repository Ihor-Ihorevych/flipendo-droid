# Collision

KnowWonder actors choose their collision primitive with `Actor.CollideType`, which stock UE1 doesn't have. CT_Box makes
the actor's primitive a `UBox`: an oriented box centered on the actor, rotated with it. HP1's Lev_Tut1 uses it for the
BlockAll walls along the Grand Hall stairs (250 x 10 x 200), BlockPlayers, Triggers and the CutScene trigger volumes.
Flipendo's port is `kw/KWCollision.cpp` (hooks in SurrealEngine's TraceTest/OverlapTest/CollisionSystem, see
[engine-hooks.md](../../engine-hooks.md)). Addresses are HP1's.

## HP1 and HP2

| CollideType | HP1 | HP2 |
|---|---|---|
| CT_AlignedCylinder | the default: world-aligned cylinder, CollisionWidth = vertical offset | same |
| CT_OrientedCylinder | cylinder rotated with the actor | same |
| CT_Box | oriented centered box, CollisionRadius/Width/Height | same |
| CT_Shape | the mesh/brush primitive's box | same |
| CT_AlignedOvalCylinder | - | **new**: world-aligned extruded ellipse, CollisionWidth = X radius, CollisionRadius = Y radius |
| CT_OrientedOvalCylinder | - | **new**: the same, rotated with the actor |

(From each game's `Actor.uc`.) The two oval types are probably why HP2's `AActor::GetPrimitive` grew from 82 to 117
bytes; not reversed yet. `UBox::LineCheck` / `PointCheck` and `UBoxPrim::GetCollisionBoundingBox` are identical in
HP2, `ToLocal` and `SetCollisionSize` differ only in offsets, the other `GetCollisionBoundingBox` overrides changed
(HP2 code not read yet; [hp2_compare.md](../reports/hp2_compare.md)).

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

## World bounding boxes: Actor.GetWorldCollisionBox(optional bool bVisual) (0x1040A950)

With bVisual, the actor's Mesh (else Brush) gives the box; otherwise its collision primitive (`AActor::GetPrimitive`,
0x1037A880: CT_Shape uses Mesh/Brush, CT_OrientedCylinder/CT_Box their primitives, anything else the cylinder). The
result is always in the world (`GetCollisionBoundingBox(Actor, bWorld=1)`, vtable +96 of UPrimitive):

| primitive | box |
|---|---|
| cylinder (UPrimitive 0x103FA2F0) | ±CollisionRadius in X/Y, ±CollisionHeight in Z around Location, **centred at Location.Z + CollisionWidth** |
| UOrientedCylinder (0x103FB190) | (±R, ±R, ±H) through `AActor::ToWorld` (rotation + Location) |
| UBox (0x103FE130) | (±R, ±W, ±H), W = CollisionWidth or R when 0, through ToWorld |
| UModel / UBoxPrim (0x103FE7A0) | the primitive's BoundingBox through ToWorld (no PrePivot) |
| UMesh (0x103B84D0) | (BoundingBox - Origin) * Scale * DrawScale, rotated by RotOrigin and Rotation, at Location + PrePivot |
| USkeletalMesh (0x1041B8B0) | BoundingBox through GetMeshCoords |

The IDA struct for AActor has the names right: CollisionRadius 0x1CC, CollisionWidth 0x1D0, CollisionHeight 0x1D4 (the
script order). SurrealEngine had this native as a stub returning an empty box, so HP1's lock-on FX and spell homing
aimed at the world origin ([spells.md](../hp1/spells.md)).

`Actor.GetRenderExtent()` (0x1040AA10) is Max - Min of the local box: for a skeletal mesh the average of the mesh's
per-frame bounding boxes (USkeletalMesh+504, computed in Serialize, raw mesh units), otherwise the local box of the
Mesh, Brush or primitive. In HP1 only `ActorShadow` uses it.
