# Input: mouse axes and the turn speed

Checked in HP1's `Engine.dll` and `WinDrv.dll`; the HP1 and HP2 part is at the end.

## From mouse counts to `aMouseX`

- `WinDrv.dll` `UWindowsViewport::UpdateInput` reads the DirectInput mouse (absolute axis mode, so it takes the
  difference from the last poll) and sends the raw counts as an axis event: `IK_MouseX` (228) with the X delta,
  `IK_MouseY` (229) with the negated Y delta. No scaling, no acceleration.
- `UInput::Exec` ("Axis", `[HP1 0x103A5200]`): an axis event (action 4) adds `Speed * delta * 0.01` to the
  property (`DefUser.ini`: `MouseX=Axis aMouseX Speed=6.0` in HP1, `Speed=4.0` in HP2); a held key (action 2) adds
  `Speed * DeltaTime`. `SpeedBase=` replaces the delta when given.
- `UInput::ReadInput` (`[HP1 0x103A5BA0]`) then multiplies every input property of the player by `20 / DeltaTime`.

So `aMouseX = Speed * counts * 0.2 / DeltaTime` before `PlayerPawn.PlayerInput` multiplies it by `MouseSensitivity`
and `DesiredFOV * 0.01111` and smooths it into `SmoothMouseX`.

SurrealEngine adds `Speed * counts * 16` and divided that by `150 * DeltaTime`, so the mouse turned 1.875 times slower
than the original at the same setting. For KnowWonder games `Engine::UpdateInput` divides by `80 * DeltaTime`
(`0100-knowwonder-core.patch`). On Wayland SurrealWidgets also truncated each relative motion event to whole counts;
libinput's unaccelerated deltas are normalised to 1000 dpi, so a slowly moving 1600 dpi mouse sent steps of 0.6 that
all became 0 (now carried over, `0003-cursor-focus.patch`).

## The mouse settings

`PlayerPawn`'s defaults are `MouseSensitivity=3`, `bMaxMouseSmoothing=true`, `MouseSmoothThreshold=0.07` in both
games, and neither game's `DefUser.ini` sets them. SurrealEngine's `UPlayerPawn::LoadProperties` fell back to its own
values (5, false, 0.16) and wrote them to `SE-User.ini`, which turned the original's mouse smoothing off. Missing
values now keep the class defaults (an `SE-User.ini` already written keeps what it has; the options menu's mouse
smoothing box sets it).

With `bMaxMouseSmoothing`, a frame with no mouse movement within `MouseSmoothThreshold` seconds of the last one keeps
turning by a decaying share of the last `SmoothMouseX` instead of stopping dead.

## HP1's turn cap

`Harry.UpdateRotation` clamps `aTurn` (already scaled by 0.24 in `PlayerWalking.PlayerMove`) to `turnRate`, 1000, and
turns by `32 * DeltaTime * aTurn` standing and `24 * DeltaTime * aTurn` moving: at most 32000 (about 176 degrees) a
second, whatever the sensitivity. At 60 fps and `MouseSensitivity` 5 the cap is reached at about 12 counts a frame.
Measured in Flipendo with `HP1_MOUSE` (`MouseSensitivity` 5): 1 count a frame turns Harry 2740 a second, 20 counts a frame 32000.

## HP1 and HP2

HP2's `Engine.dll` `UInput::ReadInput` (`[HP2 0x103AF600]`) and `UInput::Exec` (`[HP2 0x103AEC60]`) have the same
`20 / DeltaTime` and `0.01` factors. HP2's `harry` has `turnRate=1000` but no `UpdateRotation` of its own:
`PlayerPawn.UpdateRotation` turns by `32 * DeltaTime * aTurn` with no clamp, so in HP2 the turn speed follows the
mouse sensitivity all the way up.
