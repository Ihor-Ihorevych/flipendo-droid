# Sound effects: PlaySound, hearing, Galaxy's mixing

How a sound effect gets from `Actor.PlaySound` to the speakers in HP1. Reversed from HP1's Engine.dll and Galaxy.dll
(HP1's audio driver). Music is in [music.md](music.md). Port: `src/knowwonder/KWSound.cpp`, the device side in
`USurrealAudioDevice` and `Audio/OpenAL/` ([engine-hooks.md](../../engine-hooks.md)).

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
at the end of Lev_Tut2's intro, which SurrealEngine's OpenAL device rejected with a fatal error. It fades the pitch
the same way: in Lev3_Quid2 (Quidditch) the broom loop's pitch goes below 0 (down to -0.41, at volume 0) when Harry
slows down. What Galaxy does with the negative frequency that gives (`sample rate × Pitch`) is NOT yet verified in
IDA; it is inaudible at volume 0, and the OpenAL device clamps the pitch at 0 (OpenAL rejects a negative one).

OpenAL keeps one error state (`alGetError`) per context, shared by the game thread and SurrealEngine's music
thread. The unchecked `AL_PITCH` error above was read by whichever thread checked next and aborted the game, as
"alBufferData failed in UpdateMusicBuffer: AL_NO_ERROR" (an uncaught exception on the music thread) or "Failed to
play AL source" in PlaySound. The device now serialises its OpenAL calls with one mutex, and a music error only
restarts (or after five failures in a row, drops) the track.

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
distance model with reference distance 0 and rolloff 1, the max gain at the sample volume:
`min(Volume * (1 - d/R), 1) * SoundVolume / 255`, Galaxy's curve ([master volumes](#master-volumes-measured-against-the-original)). SurrealEngine's own device rescaled every volume (`(V - 1) / 4 + 1`, 0.8 from 8 up), played at
full volume within a tenth of the radius and was silent from 0.92 of it, and played every sound regardless of
hearing range or BSP occlusion.

Still SurrealEngine's: OpenAL's panning instead of Galaxy's pan law and near-field centring; no Doppler on ambient
sounds; ambient sounds out of range are stopped instead of left to the priority; the priority is clamped at 0.

The mix is limited, not clipped. Each sound's gain stays at most 1 (HP2's ALAudio `PlaySound` [HP2 ALAudio 0x100040F0]:
volume times SoundVolume, from 0.01 to 1), but sounds add up past full scale, and SurrealEngine created its OpenAL
context with no attributes, so OpenAL Soft's output limiter was off for float output and the device cut such peaks off.
The context (and a reopened device) now asks for `ALC_OUTPUT_LIMITER_SOFT`. The crack heard in Grandstaircase_hub's
follow-the-voice cutscene (issue #34) was a damaged song, not this ([music.md](music.md#damaged-songs-hp2)). Mixes are
checked with OpenAL Soft's wave writer (`ALSOFT_CONF` with `drivers=wave`), which records the exact output.

A sound's buffer holds exactly its samples. SurrealEngine's `USound::CheckSoundSamples` rounded the length up with
`ALIGN(x, 4)` (`(x & ~3) + 4`, so always 1-4 extra zero samples), and a looping sound played that silence at every
wrap. HP2's XA sounds carry their sample count (`water_turbulence`: 75793 samples, 2707 blocks of 28 decoded) and
ALAudio's stream ends there. Found 2026-11-15 looking into issue #34 (crackling in Adv3DungeonQuest), which turned out
to be the level's music itself: `sm_cda_dadaShaft-S_02.ogg` has a sharp percussion hit, panned right, every 2.93 s.

### Master volumes, measured against the original

`SetVolumes` [HP1 Galaxy 0x10606640] sets the sample volume (`glxSetSampleVolume`) to `SoundVolume` / 255 x 127 of 127,
with no other factor: a sound at full volume plays at `SoundVolume` / 255. SurrealEngine halves every sound
(`SoundVolume / 255 * 0.5`, its balance against the music), and Flipendo kept that for HP1 until 2026-11-22: every
sound effect and voice line 6 dB quieter than the original. The songs go through the same sample volume plus a
centre pan ([music.md](music.md)), which Flipendo didn't apply, so the music was also about 6 dB too loud. Together the
dialogue sat 12 dB lower against the music than in the original. HP1 now plays sounds at `SoundVolume` / 255 and the
song at its stream volume x `KW::MusicMixGain`; HP2 keeps its own path.

Measured on 2026-11-22 (issue #45): the original `HP.exe` in a Windows VM recorded with a WASAPI loopback capture,
Flipendo with OpenAL Soft's wave writer, both from `Lev_Tut1.unr` (Dumbledore's welcome over
`JS_Opening_Castle_Fly_Through_mx`). Each dialogue line and the song were decoded from the packages and located in
the recording by cross-correlation (1.5 s windows aligned one by one; the original's song drifts slightly in time),
which gives the gain each was played at regardless of what else is playing:

| | dialogue | song | dialogue / song |
|---|---|---|---|
| Original, English disc | 0.325 | 0.157 | 6.3 dB |
| Original, German disc | 0.307 | 0.152 | 6.1 dB |
| Flipendo before | 0.175 | ~0.33 | about -6 dB |
| Flipendo now, English | 0.355 | 0.185 | 5.6 dB |
| Flipendo now, German | 0.346 | 0.183 | 5.5 dB |

The German disc (English/French/German edition; `Core.u` SHA-1 91ca1260, `Engine.u` f8025d48, every DLL and script
package rebuilt and different from the English disc's) records its dialogue (`AllDialog.ger_uax`, MPEG-1 Layer II,
44100 Hz, 64 kbit/s, mono) about 11 dB quieter than the English lines (RMS ~1600 against ~5700 of 32768, peaks below
26000), while the Spanish dub (same format) is as loud as English. Its scripts play dialogue the same way (Volume 3.2,
capped at full scale), its Galaxy `Update` is the same, and the original plays German and English lines at the same
gain, so German voices are quieter in the original too; with the 6 dB Flipendo lost on top they were barely audible.

## HP1 and HP2

HP2's `PlaySound` adds `optional bool Disable3D, optional bool Loop`, and HP2 plays through ALAudio.dll (OpenAL)
instead of Galaxy. How HP2's `PlaySound`, `CheckHearSound`, `ClientHearSound` and `StopSound` differ, and how
`KW::PlaySound` follows them: [HP2's engine](../hp2/engine.md).

### HP2's sound objects

`USound::Serialize` [HP2 0x103854E0], after UObject's: the `Format` name, an int of flags, then five ints, some only in
later package versions (HP2's packages are version 79): one always, one from version 77, two from 78, one from 79.
For an XA sound they are a float (a duration-like value), the sample count, bits (16), channels (1) and rate (22050);
a wav sound has 0s. Then the data (a lazy array), and with flag 0x80 a second lazy array. Flag 4 makes ALAudio
stream the sound instead of loading it into one buffer (`UALAudioSubsystem::RegisterSound`, [HP2 ALAudio 0x10003400]);
a wav file is read with `FWaveModInfo::ReadWaveInfo` as in HP1. Flipendo reads the fields in `USound::Load`
(`0200-hp2.patch`).

Format `XA` (most of HP2's sound effects, ambience and dialogue) is EA-XA ADPCM, EA's own codec; HP2's folder even
ships EA's `sx.exe`. Engine.dll only has the encoder (`USound::CompressXA`). `UALAudioSubsystem::RegisterSound` streams
every sound with flag 4 that isn't `OGG` through `FFileStream::CreateStream` as stream type 3 (4 with flag 2, looping),
and `FFileStream::Read` [HP2 Core 0x10178390] decodes types 3 and 4 with two unexported functions: `sub_1017AD30` [HP2
Core 0x1017AD30] hands out whole blocks and keeps a partial last one, `sub_1017AB80` [HP2 Core 0x1017AB80] decodes
blocks. The decoder is mono: 15-byte blocks of 28 samples, the header byte holding the predictor (high nibble, a table
of 16-bit pairs at 0x101D44D8: 0/0, 240/0, 460/-208, 392/-220 in 1/256, then unrelated data) and the shift (low
nibble), two samples per byte, high nibble first. Each sample is
`clamp((c1 * h1 + c2 * h2 + ((nibble << 28) >> (shift + 8))) >> 8)`, one rounding for the prediction and the nibble
together (the common EA-XA decoders round them apart, up to 53 off on HP2's sounds); there is no 0xEE uncompressed
block. The OpenAL format is mono 16-bit when the sound has one channel and stereo 16-bit otherwise, so a stereo XA sound
would play the one decoded stream as interleaved stereo. Every one of HP2's 3114 XA sounds (AllDialog.USA_uax and every
.u and map) is mono, uses predictors 0-3 only and is exactly `ceil(samples / 28)` blocks long.
Flipendo decodes it the same way to a WAV in `src/hp2/HP2Sound.cpp`.
