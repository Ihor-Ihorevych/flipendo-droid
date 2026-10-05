# Sound effects: PlaySound, hearing, Galaxy's mixing

How a sound effect gets from `Actor.PlaySound` to the speakers in HP1. Reversed from HP1's Engine.dll and Galaxy.dll
(HP1's audio driver). Music is in [music.md](music.md). Port: `src/knowwonder/KWSound.cpp`, the device side in
`USurrealAudioDevice` and `Audio/AudioDevice.cpp` ([engine-hooks.md](../../engine-hooks.md)).

## PlaySound (Engine.dll)

`AActor::execPlaySound` [HP1 0x1040B000] never calls the audio device itself. It:

1. reads the arguments with HP1's defaults: Slot `SLOT_Misc`, Volume `TransientSoundVolume`, Radius
   `TransientSoundRadius`, Pitch `TransientSoundPitch / 64` (HP1's TransientSoundPitch is a float, default 64). A None
   sound does nothing.
2. builds the Id: `Index * 16 + Slot * 2 + bNoOverride` (Index = the actor's object index).
3. packs `Parameters = (Volume * 100, Radius, Pitch * 100)`. The radius goes in unchanged; the hearing range uses
   `Radius != 0 ? Radius : 1600`, squared.
4. asks every pawn in `Level.PawnList` with `bIsPlayer` set to hear it (`CheckHearSound`). On a network client, or
   when the calling script function is `simulated`, it asks each viewport's actor instead. In a standalone game both
   reach the same player.

`execPlayOwnedSound` [HP1 0x1040B890] is the same without DemoPlaySound. Online it also skips the owner when the owner is
a remote player.

`AActor::CheckHearSound(Hearer, Id, Sound, Parameters, RadiusSquared)` [HP1 0x1040AB90]:

- the hearer listens from its `ViewTarget` (PlayerPawn+0x4F4: Harry's camera) when it has one, else from its own location;
- `range = RadiusSquared / 1.3`: no sound when the squared distance is at or past that (0.877 of the radius);
- `UModel::FastLineCheck(listener, sound)` clear: heard as it is;
- blocked by BSP: Volume (Parameters.X) × 0.35, and the range shrinks to `range × 0.6` (0.679 of the radius) unless
  the hearer is the sound's `Instigator`. Heard if the squared distance is at most that;
- heard = `ClientHearSound(Actor, Id, Sound, Actor.Location, Parameters)` event on the hearer.

`APawn::execClientHearSound` [HP1 0x10407F30] (`Pawn.ClientHearSound` is a native event; no HP1 script overrides it):
only a PlayerPawn whose `Player` is a viewport plays it. It calls the audio subsystem's `PlaySound` (vtable +120)
with Volume `Parameters.X * 0.01`, Radius `Parameters.Y` (0 → 1600), Pitch `Parameters.Z * 0.01`. If the actor is
being destroyed (`bDeleteMe`, Actor+0x28 bit 0x200), it passes None, so the sound stays where it was played.

Engine.dll's `WorldSoundRadius` is `(SoundRadius + 1) * 25`.

## Galaxy.dll's sound channels

`UGalaxyAudioSubsystem::PlaySoundW` [HP1 Galaxy 0x10607730] (exported as PlaySoundW):

- asserts `Radius != 0`; no viewport or no sound: nothing. `Sound == (USound*)-1` frees the slot without playing;
- `SLOT_None` gets a fresh Id (a counter going down, `* 16`);
- priority: `Volume` for a music sound (USound+100 set), else `Volume * (1 - dist / Radius)`, listener = viewport
  actor's ViewTarget or the viewport actor. Not clamped (far sounds go negative);
- channel: the one whose Id matches ignoring bit 0 (with bNoOverride, bit 0, nothing is played), else the channel
  with the lowest priority, if that is at most the new sound's priority;
- the channel (52 bytes at +128, count at +100) keeps Actor, Id, Sound, Location, **Volume as given**, Radius, Pitch,
  Priority. There is no volume rescaling.

`ModifySound` [HP1 Galaxy 0x10607AE0]: first channel whose Id matches ignoring bit 0 and whose sound is the given one
(any sound when None). Parameter 0/1/2 sets Volume/Radius/Pitch, stored as given. A negative volume is kept and plays
silent (Update clamps the integer volume at 0): `BroomHarry.UpdateBroomSound` fades the broom loop slightly below 0
at the end of Lev_Tut2's intro, which SurrealEngine's OpenAL device rejected with a fatal error.

`Update` [HP1 Galaxy 0x106081F0], for every playing channel (music channel aside):

- an attached actor moves the sound to the actor's location;
- `dist` from the listener (ViewTarget or viewport actor); priority again `(1 - dist / Radius) * Volume`;
- **volume** = `clamp(1 - dist / Radius, 0, 1) * Volume`, clamped to full scale (`* 32767`, at most 0x7FFF). A sound
  played at Volume 3.2 (101 calls in HP1's scripts) stays at full volume out to 0.69 of its radius;
- **pan**: the sound's offset in the view coordinates (the `FCoords` passed to Update), angle =
  `atan2(right, forward)`. Within a tenth of the radius the angle is scaled by `dist / (0.1 * Radius)`, so near
  sounds drift to the centre. `pan = angle * 9126.3 + 16383`, clamped 0..32767. The angle is folded to the front
  (`atan2(right, |forward|)`), so the pan stays within 2048..30718: a sound to the side is never fully in one ear.
  The mixer [HP1 Galaxy 0x10612C7C, not a defined function; the table is filled in Galaxy's init at 0x1060BAB9]
  moves the playing pan towards the new one by at most 0x200 per mix block, and plays with a constant-power square
  root law: `table[i] = sqrt(i / 32767) * 32767`, right gain `table[pan]`, left gain `table[32767 - pan]` (0.707
  each in the centre; 0.968 / 0.25 at the side). Stereo samples are centred
  (0x4000); a reverse-stereo option mirrors it, and a sound behind the listener goes to 49152 (rear) when that
  option is set;
- **Doppler** only for ambient sounds (`Id & 14 == SLOT_Ambient * 2`): `pitch × clamp(1 - dot(dirToSource,
  Actor.Velocity) / DopplerSpeed, 0.5, 2)`, `dirToSource` from the listener to the actor, `DopplerSpeed` from the
  ini (9000 on the disc). The listener's own velocity plays no part;
- frequency = sample rate × Pitch × Doppler. With 3D hardware enabled the position (× 0.0025) goes to the 3D voice
  instead of the pan.

Ambient sounds (in Update, when the viewport is realtime): every actor with an `AmbientSound` that has no channel
gets one through PlaySound with Volume `SoundVolume / 255 * AmbientFactor`, Radius `(SoundRadius + 1) * 25`, Pitch
`SoundPitch / 64`, Id `Index * 16 + SLOT_Ambient * 2`, out of range or not; priority decides. A playing ambient sound
is stopped when its actor is deleted or changes AmbientSound, else updated to Volume `2 * SoundVolume / 255 *
AmbientFactor` (zone-scaled by a render device call when the actor's byte +480 is set), same radius and pitch.

## Flipendo

Ported (`KWSound.cpp`): PlaySound/PlayOwnedSound, CheckHearSound and ClientHearSound as above. The Id keeps
SurrealEngine's actor-pointer packing (`SoundId`), which ModifySound/StopSound and the ambient sounds also use;
only equal Ids matter. The device (KnowWonder only) keeps volumes as given and uses OpenAL's linear clamped
distance model with reference distance 0 and rolloff 1, the max gain at full volume: `min(Volume * (1 - d/R), 1)`,
Galaxy's curve. SurrealEngine's own device rescaled every volume (`(V - 1) / 4 + 1`, 0.8 from 8 up), played at
full volume within a tenth of the radius and was silent from 0.92 of it, and played every sound regardless of
hearing range or BSP occlusion.

Still SurrealEngine's: OpenAL's panning instead of Galaxy's pan law and near-field centring; no Doppler on ambient
sounds; ambient sounds out of range are stopped instead of left to the priority; the priority is clamped at 0.

## HP1 and HP2

HP2's `PlaySound` adds `optional bool Disable3D, optional bool Loop`, and HP2 plays through ALAudio.dll (OpenAL)
instead of Galaxy. `KW::PlaySound` takes both parameters for HP2's adapter but ignores them so far. In
[hp2_compare.md](../reports/hp2_compare.md) HP2's `execPlaySound`, `execPlayOwnedSound` and `execClientHearSound` are
"changed" and `CheckHearSound` isn't exported (inlined or rewritten); none of it, nor ALAudio.dll's mixing, has been
read yet.
