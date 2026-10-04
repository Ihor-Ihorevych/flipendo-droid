# Spells: targeting and casting

How HP1 picks a spell target and aims a cast. All of it is UnrealScript (`HarryPotter.Harry`, `HPBase.Target`,
`HPBase.baseWand`, `HPBase.baseSpell`); the natives it depends on are `Actor.TraceActors` (309, `kw/KWTraceTexture.cpp`),
`Actor.Trace`, and `Actor.GetWorldCollisionBox` (286, `kw/KWCollision.cpp`).

## Flow

1. `Harry.AltFire` (LeftMouse / Alt / Slash) enters state `playeraiming` unless Harry is frozen, falling or in a cutscene.
   Its `begin:` calls `makeTarget()`, which spawns `HPBase.Target` 50 units in front of the view as `rectarget`.
2. `Target` starts in `auto state seeking`; every tick `setTarget` traces 512 units (1024 with `bExtendedTargetting`,
   the Devil's Snare level) along Harry's rotation with the target pitch/yaw offset that the mouse moves. The first
   `TraceActors` hit with `bProjTarget` or `bBlockActors` is the candidate; otherwise `Harry.ExtendTarget()`, then a
   plain `Trace`. A `bProjTarget` candidate becomes `victim`: `LockOn` reads `GetWorldCollisionBox(true)`, puts the
   target FX at the box centre + `CentreOffset` (sized by `SizeModifier`) and calls
   `baseWand.ChooseSpell(victim.eVulnerableToSpell)`, which selects the spell class (SPELL_Flipendo -> spellFlipendo, ...).
3. Releasing the button casts: `baseWand.CastSpell(target)` spends mana, fires the `curSpell` projectile
   (`ProjectileFire`) with `target` set, and plays the incantation. Without a learned spell `curSpell` is `spellnone`
   (a fizzle that explodes after ~0.25 s).
4. `baseSpell.Timer` homes the projectile: it aims at the centre of `Target.GetWorldCollisionBox(true)` + `CentreOffset`
   and turns the velocity halfway towards it while the target is ahead.

`Harry.AdjustAim` (the plain-fire path) picks among `VisibleActors` with `bProjTarget` the one closest in yaw, or
`rectarget.victim` when locked.

## Actor.GetWorldCollisionBox(optional bool bVisual) (0x1040A950)

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
script order). SurrealEngine had this native as a stub returning an empty box, so lock-on FX and spell homing aimed at
the world origin.

`Actor.GetRenderExtent()` (0x1040AA10) is Max - Min of the local box: for a skeletal mesh the average of the mesh's
per-frame bounding boxes (USkeletalMesh+504, computed in Serialize, raw mesh units), otherwise the local box of the
Mesh, Brush or primitive. Only `ActorShadow` uses it.

## Not checked yet

The lock-on path with a real victim (`eVulnerableToSpell` choosing the spell) and the spell hit reactions need the first
spell lesson (Lev_Tut1's Flipendo challenge, `CUTFLIPBEGIN`), which the autopilot doesn't reach yet. Tested: a cast in
Fred & George's room (`HP1_EXEC="135:AltFire"` on the Lev_Tut1 route) spawns the Target, fires `spellnone` and homes
on the Target's box.

## Spell lesson flow

`SpellLearnTrigger` (HPBase) runs the lesson as states: `Template` (Quirrell talks, the template is drawn) → `Draw`
(Tick moves the wand from the mouse, AltFire held stores up to 500 points over DrawTime) → `Judge` (CompareGesture
scores, then Tick replays the drawing at double speed and sets `bCountedUp`; the state code waits for it in
`CountLoop`). A pass goes back to `Template` for the next level (4 levels), a fail at level 0 repeats it, otherwise it
`Destroy()`s itself; `Destroyed` restores Harry and the camera and triggers its Event (`CUTFLIPBEGIN` in Lev_Tut1).

Judge's Tick ends with `disable('Tick')`. In Core.dll `execDisable` (0x10141F30) only clears the bit in the state
frame's ProbeMask, and `UObject::GotoState` (0x10131BB0) rebuilds that mask on every call, also into the same state:
`(State.ProbeMask | Class.ProbeMask) & State.IgnoreMask`. SurrealEngine kept disabled events per state name for good,
so the second time the lesson reached Judge its Tick never ran and `CountLoop` waited forever (fixed in
`0010-engine-fixes.patch`).

## Spell lesson rendering

- The template is `SpellLearnFX` → a `SilverSparkle01` ParticleFX with `Pattern` = the spell's Gesture, grey (96,96,96)
  translucent particles. 30 units behind it the lesson spawns `SpellBlackboard`: a modulated sprite (Style 4) with the
  IceTexture `HP_FX.General.les_spellbackgrnd` (Glass `Les_SpellPan`, Source `Les_SpellBase`, MipZero 128 grey).
- Translucents must be drawn back to front, or the modulated blackboard tints the nearer spiral red.
- IceTexture (Fire.dll, `kw/KWIceTexture.cpp`): each output pixel is a source pixel from the same row shifted by
  the glass value under it, `dest[y][x] = source[y][(glass[y+V][x+U] + x) & UMask]` with MoveIce (the glass pans),
  `source[y+V][(glass[y][x] + x + U) & UMask]` without. It takes the source's palette, so the blackboard really is a
  warm, darker zone (source average 81,54,54, modulated) with a slow shimmer, not a neutral grey.
