# Collision

KnowWonder actors choose their collision primitive with `Actor.CollideType`, which stock UE1 doesn't have. CT_Box makes
the actor's primitive a `UBox`: an oriented box centered on the actor, rotated with it. HP1's Lev_Tut1 uses it for the
BlockAll walls along the Grand Hall stairs (250 x 10 x 200), BlockPlayers, Triggers and the CutScene trigger volumes.
Flipendo's port is `src/knowwonder/KWCollision.cpp` (hooks in SurrealEngine's TraceTest/OverlapTest/CollisionSystem, see
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

## BSP checks

KnowWonder's physics (`src/knowwonder/KWBspCheck.cpp`) tests boxes and lines against a BSP model (the level's, or a
mover's brush with its planes taken to world space) the way HP1 does; SurrealEngine's own traces are close but differ
at edges and corners.

- **Box sweep** (`UModel::LineCheck` 0x10429C80 with an extent; `sub_1042A480`): walks the BSP with the node planes
  pushed out by 1.1 times the box, tracking whether it is inside solid space (a node is solid with vertices and none of
  NodeFlags 0x21, `sub_1042CDA0`); only a solid leaf's convex hull (`UModel.LeafHulls` from the parent node's
  CollisionBound: the hull's node planes, then its bounding box) is tested. The segment is clipped (`sub_1042C050`)
  against each hull plane pushed out by the box, then (the level only) the hull's box planes (-X, -Y and both Z sides
  pushed out by 0.1, +X and +Y pulled in by 0.1), then bevel planes between two hull planes facing opposite ways along
  an axis (when the axis' crossings with them agree, dot > 0.001; through their intersection line, `sub_1042CAF0`). A
  box already inside a plane and moving into it counts as entering at once. The hit's Item is the hull plane's node
  (the surface lookup uses it). The time is pulled back by 0.5 / length.
- **Ray** (zero extent, `sub_104294C0`): the BSP walk with the inside/outside rule; a hit is the point where the line
  enters solid space after having been outside (unless the extra node flags have 0x10, starting inside counts too), the
  normal is the last node crossed, facing the start.
- **Point / box at a location** (`UModel::PointCheck` 0x104271D0; `sub_10427630` with an extent): the box is out of a
  hull when it is wholly in front of any of its planes; else the hit is the shallowest penetration, pushed out 1.02
  times as far.
- **FastLineCheck** (0x104291E0, `sub_10429300`): a line of sight; only NF_NotCsg (bit 0) makes a node non-solid here.

## Actor primitives and the level checks

`src/knowwonder/KWLevelCheck.cpp`. An actor's primitive (`AActor::GetPrimitive` 0x1037A880, a brush always its Brush):

- the cylinder (`UPrimitive::LineCheck` 0x103FA760 / `PointCheck` 0x103FA420) has its centre raised by
  CollisionWidth. Lines: through the caps (normal +/-Z) and the side (quadratic), time pulled back 0.001; a line
  starting inside only blocks when it moves towards the axis. Points: strictly inside; the hit location is odd but kept
  as in the original (a cap gives (x, y, Location.Z - Extent.Z), the side adds the point's Z onto Location.Z);
- the oriented cylinder (0x103FBC50 / 0x103FB7A0): the same in the actor's rotated frame;
- the box (CT_Box, above); meshes use the cylinder; brushes their BSP.

The actor hash (`FCollisionHash`, 256-unit cells by each actor's world collision box) is SurrealEngine's bucket grid,
filled with HP1's boxes; candidates are then tested with their own primitive.

- `ULevel::MultiLineCheck` (0x103AC620): the level first (when LevelInfo is passed), then actors only up to 5 units past
  the level's hit (their times rescaled); with a level hit closer than 0.01 of the line and 30 units only movers still
  count, and without actors asked for only movers; a mover hit on the level hit's plane within 2 units of it is put 2
  units before it. Sorted by time.
- `ULevel::SingleLineCheck` (0x103AC180): the first hit of MultiLineCheck the flags accept, skipping the tracer and its
  owners: 4 the level, 2 movers, 8 zone infos, **1 actors that block the tracer** (`IsBlockedBy`), 16 the others. Trace
  uses 23 (6 without actors); physics mostly 6 (level, movers) or 7.
- `ULevel::MultiPointCheck` / `SinglePointCheck` (0x103ABF70 / 0x103ABD70): the level's hit first, then actors'; the
  single check keeps the hit whose pushed-out location is nearest the point.

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
