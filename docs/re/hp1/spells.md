# HP1 spells: targeting, casting and lessons

How HP1 picks a spell target, aims a cast and teaches a spell. All of it is HP1's UnrealScript (`HarryPotter.Harry`,
`HPBase.Target`, `HPBase.baseWand`, `HPBase.baseSpell`, `HPBase.SpellLearnTrigger`) on top of shared engine natives:
`Actor.TraceActors` (309, `src/knowwonder/KWTraceTexture.cpp`), `Actor.Trace`, `Actor.GetWorldCollisionBox` (286,
[collision.md](../engine/collision.md#world-bounding-boxes-actorgetworldcollisionboxoptional-bool-bvisual-0x1040a950)),
the Gesture natives ([gestures.md](../engine/gestures.md)) and ParticleFX ([particles.md](../engine/particles.md)).

HP2 rewrote this layer: its spells are cast through a `SpellCursor` (a ParticleFX) and taught by `SpellLessonTrigger`
(HP1's `Target` and `SpellLearnTrigger` don't exist there). Not studied yet ([hp2/gameplay.md](../hp2/gameplay.md#spells)).

## Flow

1. `Harry.AltFire` (LeftMouse / Alt / Slash) enters state `playeraiming` unless Harry is frozen, falling or in a cutscene.
   Its `begin:` calls `makeTarget()`, which spawns `HPBase.Target` 50 units in front of the view as `rectarget`.
2. `Target` starts in `auto state seeking`; every tick `setTarget` traces 512 units (1024 with `bExtendedTargetting`,
   the Devil's Snare level) along Harry's rotation with the target pitch/yaw offset that the mouse moves. The first
   `TraceActors` hit with `bProjTarget` or `bBlockActors` is the candidate; otherwise `Harry.ExtendTarget()`, then a
   plain `Trace`. A `bProjTarget` candidate becomes `victim`: `LockOn` reads `GetWorldCollisionBox(true)`, puts the
   target FX at the box centre + `CentreOffset` (sized by `SizeModifier`) and calls
   `baseWand.ChooseSpell(victim.eVulnerableToSpell)`, which selects the spell class (SPELL_Flipendo -> spellFlipendo, ...).
   It also draws the spell symbol on the target (`DrawSpellFX`: the spell's `GestureParticleEffectClass` spawned by the
   victim, with `Pattern` = the spell's gesture). A `baseChar` with `bGestureOnTargeting` false (doxies, gnomes, Peeves,
   the potion bottle) gets no symbol while aimed at: `Target.seeking.EndState` draws it when the cast destroys the
   `Target` (`ParticlesMax` 200), which relies on the engine calling `EndState` from `ULevel::DestroyActor` before the actor is marked deleted (SurrealEngine's `CallEvent` skips a deleted actor)
   ([script_events.md](../engine/script_events.md)).
3. Releasing the button casts: `baseWand.CastSpell(target)` spends mana, fires the `curSpell` projectile
   (`ProjectileFire`) with `target` set, and plays the incantation. Without a learned spell `curSpell` is `spellnone`
   (a fizzle that explodes after ~0.25 s).
4. `baseSpell.Timer` homes the projectile: it aims at the centre of `Target.GetWorldCollisionBox(true)` + `CentreOffset`
   and turns the velocity halfway towards it while the target is ahead.

`Harry.AdjustAim` (the plain-fire path) picks among `VisibleActors` with `bProjTarget` the one closest in yaw, or
`rectarget.victim` when locked.

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

Judge's Tick ends with `disable('Tick')`, which only lasts until the next `GotoState`
([scripting.md](../engine/scripting.md#disable-and-gotostate)); SurrealEngine kept it for good, so the second time
the lesson reached Judge its Tick never ran and `CountLoop` waited forever (fixed in `0010-engine-fixes.patch`).

## Spell lesson rendering

- The template is `SpellLearnFX` → a `SilverSparkle01` ParticleFX with `Pattern` = the spell's Gesture, grey (96,96,96)
  translucent particles. 30 units behind it the lesson spawns `SpellBlackboard`: a modulated sprite (Style 4) with the
  IceTexture `HP_FX.General.les_spellbackgrnd` (Glass `Les_SpellPan`, Source `Les_SpellBase`, MipZero 128 grey).
- Translucents must be drawn back to front, or the modulated blackboard tints the nearer spiral red.
- The IceTexture ([textures.md](../engine/textures.md)) takes the source's palette, so the blackboard really is a
  warm, darker zone (source average 81,54,54, modulated) with a slow shimmer, not a neutral grey.
