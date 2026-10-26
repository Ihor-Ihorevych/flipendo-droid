# HP1 controls: moving, turning, aiming and the camera

From HP1's scripts (`HarryPotter.Harry`, `HPBase.BaseCam`, `HPBase.CamTarget`, `HPBase.Target`, `Engine.PlayerPawn`)
and `DefUser.ini`. What HP2 does instead: [hp2/gameplay.md](../hp2/gameplay.md#moving-and-looking-while-casting). The
Improved Movement extra (`src/hp1/mods/ImprovedMovement.cpp`) builds HP2's controls on top of what is described here.

## Turning and strafing

`DefUser.ini` binds Left and Right to two things at once: `RotLeft` / `RotRight` (`Axis aBaseX`, speed 150) and
`StrafeLeft` / `StrafeRight` (`Axis aStrafe`, speed 300). `MouseX` is `Axis aMouseX`. `PlayerPawn.PlayerInput` adds
`aBaseX * FOVScale + SmoothMouseX` to `aTurn` (with the Strafe button, `bStrafe`, it adds `aBaseX` to `aStrafe`
instead, and the mouse does nothing: KnowWonder commented the mouse out of that branch).

`Harry.PlayerWalking.PlayerMove` then picks one of the two:

- Plain walking: `aStrafe = 0`, `aTurn *= 0.24`. Left and Right turn, the strafe input is thrown away.
- Falling, `bLockedOnTarget` (bosses) or `bFixedFaceDirection`: `aStrafe *= 0.16`, `aTurn = 0`. Left and Right strafe,
  nothing turns; `ProcessAccel` points Harry at the boss.

Both keys at once cancel in the original engine (each held key adds its speed to the axis); SurrealEngine's
`Engine::InputCommand` keeps one key per axis, so in Flipendo the key pressed last wins. `aForward` is scaled by 0.08
in both. `Harry.UpdateRotation` returns at once while Harry falls, so nothing turns him
in the air (in any state). `bKeepStationary` zeroes `aForward` and `aStrafe` (Harry can still turn),
`bReverseInput` (the troll chase) flips them, `bScreenRelativeMovement` moves along the camera's axes. `TweenToRunning`
and `PlayRunning` already pick `runback`, `StrafeRight` or `StrafeLeft` when the acceleration points more than about 41
degrees away from Harry's facing (`Dir Dot X < 0.75`), which only the boss fights reach. `PlayRunning` also halves
`Velocity.X` (only X) every time it starts `runback`.

## Aiming (`playeraiming`)

`Harry.AltFire` goes to `playeraiming` (not while falling, frozen or in a cutscene). Its `begin` loops `wave` on the
whole body; releasing the button plays `cast` (rate 2, `AT_Replace`) and waits until its `AnimFrame` reaches 0.95, then
goes back to `PlayerWalking`. The spell leaves on the `cast` animation's notify (`Harry.Cast`).

`playeraiming.PlayerMove` only accelerates Harry while `fTimeToStop` isn't below zero: `BeginState` sets it to 0 and
`PlayerMove` counts it down by `DeltaTime` first, so Harry stops the frame aiming starts. Locked on a boss he keeps
moving (`aForward * X + aStrafe * Y`). Turning works as in walking (`aTurn * 0.24`, `UpdateRotation`).
`playeraiming.ProcessMove` sets `Acceleration` and returns before any animation code, and `AnimEnd` is empty, so
nothing changes the animation while aiming. Its jump (`if ( bPressedJump ) DoJump();`, as in `PlayerWalking`) is
commented out, and `PlayerWalking`'s `BeginState` clears `bPressedJump`, so a jump pressed while aiming is lost; `PlayerWalking.ProcessFalling`, which counts `fTimeInAir` (the landing sound's volume), isn't called either; the air
control is `aStrafe * 0.4` against `PlayerWalking`'s 0.16. Nothing in `playeraiming` stops a ledge grab (`Mount`) or an
auto jump (`DoJump`) either; outside the boss fights the original never meets them, since Harry stands still while he
aims.

`HPBase.Target` (the reticle, `rectarget`) pitches the aim with the mouse (`SmoothMouseY` past 256, by `/ 8`) and with
the forward and back keys (`aForMove`, the raw `aBaseY`, past 64: 256 a tick), between -8000 and 12000. The keys are
ignored while `bNoMove` is set, which is cleared once `aForMove` is 0. Left and right only turn the aim on a boss
(`TargetYaw`, by the mouse); otherwise the reticle follows Harry's rotation.

## Ledges

The physics raise `Harry.Mount(ledge - Location)` for a ledge at most `MaxMountHeight` (96.5) up, on the ground and in
the air ([physics.md](../engine/physics.md)). On the ground `Mounting` climbs at once with `climb32` (less than 48 up),
`climb64` (less than 80) or `climb96start` then `climb96end`. In the air (`bFallingMount`) `FallingMount` holds
`climb96end`'s first frame and waits until Harry has fallen to 82 below the ledge (moving him there at once if he is
lower) or has landed, then `Mounting` plays `climb96end` from there. A jump that catches a ledge's top half drops
Harry onto the hang point first. AdamJD's mod sends a mid-air grab straight to `Mounting`, which picks the climb by the
height the ground's way (with `climb96end` from 80 to 98).

## The camera and the mouse's Y axis

`Harry.UpdateRotation` zeroes `aLookUp` in `PlayerMove` ("make harry steady"), so the mouse never pitches Harry. The
camera does it instead, and only while Harry stands still:

- `BaseCam.GeneralStationaryModeCamera` (used by `Standardstate`, the normal camera) sets `p.bStationary` by comparing
  Harry's location with `HarrysPreviousPosition`. Stationary, `SmoothMouseY` past 64 adds `/ 32` to `campitch`
  (clamped to +-12000), which orbits the camera through `CameraOffset >> locRot`. Moving, `campitch = 0` at once.
- `CamTarget.setTarget` raises or lowers the camera's look-at point (`targetOffset.z`, 0 to 150, by 3 a tick) by
  `SmoothMouseY` while `p.bStationary`, and eases it back to 50 while Harry moves.
- `SmoothPitch` (when the mouse is still) is meant to ease `campitch` back to 0, but subtracts `10 * deltatime` from a
  negative pitch too, moving it away from 0 (by too little to see).

`FarState` and `Highstate` also compare with `HarrysPreviousPosition` (and update it), and use `GeneralMoveModeCamera`
while Harry moves (not backing up, `cameraLock` off); `BossState` and `TrollChaseState` set `p.bStationary = true`
themselves.

## Independent orbit on retail packages (optional free-cam mod)

The retail `BaseCam.Standardstate` follows Harry's facing and runs both a `Tick` and a latent
`TurnToward(StandardTarget)` loop. Wrapping Harry's movement alone cannot reproduce the independent camera
in AdamJD's [Movement and Camera Improvement](https://www.moddb.com/games/harry-potter-and-the-sorcerers-stone/downloads/hp1-movement-and-camera-improvement).
The previous local installation's embedded scripts credit AdamJD and replace the normal camera with a separate
orbit target, pitch/yaw smoothing and a traced camera arm. Its defaults are a 128-unit arm, a 55-unit vertical
look-at offset, rotation speed 4, rotation tightness 8 and distance recovery tightness 2.5.

`FreeCamera.cpp` supplies this orbit on the retail classes through script-function wrappers, disables the
retail latent turn loop while controlling the camera, and keeps orbit state outside the packages. With
`improved-movement` enabled, movement uses camera yaw, while Harry retains idle facing and turns towards
acceleration. Without it, the original keyboard turning and movement remain; horizontal mouse input controls
the camera independently. Aiming faces the camera and feeds
its pitch into the original reticle trace without applying mouse input twice. The camera traces its vertical
focus offset and arm against the world and blocking actors, pulls in immediately and eases back out. It delegates
to the original scripts for cutscenes, menus, flying, carrying, bosses and special movement modes, resetting its
orbit on camera state and level changes. It uses existing input actions and never changes key bindings.
The switches are independent: `--vanilla --mod=free-cam` enables only the camera;
`--vanilla --mod=free-cam,improved-movement` enables both. Neither mod changes the other’s enabled state.
