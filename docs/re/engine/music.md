# Music

How HP1 plays its songs, from Galaxy.dll (`UGalaxyAudioSubsystem::Update` [HP1 Galaxy 0x106081F0]; addresses are
HP1's Galaxy.dll; `SetVolumes` [HP1 Galaxy 0x10606640]). Ported in `src/knowwonder/KWSound.cpp` (`KW::UpdateMusic`), which
replaces SurrealEngine's `USurrealAudioDevice::UpdateMusic` for HP1 ([engine-hooks.md](../../engine-hooks.md)).

**HP1 and HP2.** HP2 replaces Galaxy with OpenAL (`ALAudio.dll`) and Ogg Vorbis, not read yet. Its `PlayerPawn.uc` still
declares `bSongFinished` but not `bDontLoopSong`, so how HP2 decides whether a song loops is not checked. The port is
gated to HP1 (`IsKnowWonder()`, HP1 only for now). Not checked: stock HP2 may loop every song from start to end with no
way to play one once, `bDontLoopSong` being HP1-only. If so, HP2 needs no `bDontLoopSong` branch: always loop.

## Songs

- A song is `PlayerPawn.Song` (a `Music` object, format `mp2`), set with `ClientSetMusic(Song, Section, CdTrack,
  Transition, optional bDontLoop)`; KnowWonder added the last parameter, stored in `PlayerPawn.bDontLoopSong`.
  `MusicEvent` passes its `bPlayOnceOnly`; the menus and `GameInfo` pass false.
- Galaxy starts an mp2 song as a streamed `USound(Music, bLoop)` with `bLoop = !bDontLoopSong` (PlayerPawn +0x55C
  bit 0), played on the music channel.
- **Every update**, Galaxy finds the channel playing the music sound. None: `bSongFinished` (PlayerPawn +0x564) is set
  and nothing else happens. Otherwise `bSongFinished` is the stream's finished flag, and when that is set it also sets
  `Transition = MTRAN_Instant` and `Song = None`, so a song played once ends in silence and the next update stops the
  channel. `MusicEvent.WaitForSongToFinish` polls `bSongFinished` to fire `TriggerToSendWhenDone`.
- So every song loops unless a script asked for it to play once.
- Songs change through `MusicEvent`s (`SetMusic` -> `ClientSetMusic`), fired by `Trigger`s in doorways, by spell
  triggers or by cutscenes; besides those only `GameInfo` (the level's Song at login, `MTRAN_Fade`), the menu book
  (title and storybook songs) and the end of a song played once set it. In Lev_Tut1b, `MusicEvent2` (tag `MU3`,
  `JS_HogwartsNeutral_14`, about 14 s) and `MusicEvent1` (`FlipBridge1Dis`) play once; the rest loop.

## Song changes

Galaxy keeps its own copy of the current song (+0x708), CD track (+0x70C), section (+0x70D) and `MusicFade` (+0x780,
a float, 1 = full). Every update, while `PlayerPawn.Transition` (+0x55A) isn't `MTRAN_None`:

- `changed` = the current song isn't `PlayerPawn.Song` (and the `MusicVolume` setting isn't 0; at 0 nothing changes).
- If a song (or CD track) is current, it fades first: `MusicFade` goes down by the frame time (real time, at most 1 s)
  for `MTRAN_Fade`, x3 for `FastFade`, x0.2 for `SlowFade`, until it is below `-0.002 * Latency` (-0.0004, -0.006 for
  the others; `Latency` is the ini setting, ms). The other transitions, and a current song set with section 255,
  end the fade at once. Then the stream is stopped, **only if `changed`**, and the current song cleared.
- With nothing current: `MusicFade = 1`, the current song, CD track and section become the PlayerPawn's, the mp2 stream
  starts **only if `changed`**, and `Transition` goes back to `MTRAN_None`.

So a `MusicEvent` for the song already playing doesn't restart it: the song fades to nothing over the transition time
and comes back at full volume where it was. The hub doorways have retriggerable `Trigger`s for the room's song, a
silence and the corridor's song a few steps apart (Lev2_HogFront: `MU4`/`MU6`/`MU7` and more, Green_Cauldron vs
happy_hogwarts), and the `MusicEvent` default transition is `MTRAN_Fade` (1 s), so stepping back and forth there
fades instead of cutting and restarting.

The music stream's volume, set every update: `PlayerPawn.PercentMusicVolume` (+0x55B, default 100) x `MusicVolume` /
(255 x 100) x `MusicFade`, clamped to 0-1. A `MusicEvent` with `bDoBoost` sets `PercentMusicVolume` (e.g. 50 in most of
Lev_Tut1b, 255 = as loud as it goes) and fades it back over `BoostTime` in script.

## What SurrealEngine did

It played every mp2 song once. When the stream ended, the music thread kept queueing its fixed buffers without
clearing the part the decoder didn't fill, so the last chunks of old audio replayed forever: a glitchy ~1 s loop at
the end of every song (reported near Nearly Headless Nick in Lev_Tut1b). Fixed for every game by filling the rest of
the buffer with silence (`Audio/AudioDevice.cpp`); for HP1, `KW::UpdateMusic` restarts the song at its end (a new mp3
decoder: SurrealEngine's mp3 source doesn't rewind after reaching the end) unless `bDontLoopSong`, and sets
`bSongFinished` and clears an ended song like Galaxy.

It also restarted the song on every `Transition`, switched at once and ignored `PercentMusicVolume`: crossing a
doorway trigger of the song already playing started it again from the beginning. `KW::UpdateMusic` does the song
changes above. Checked 2026-10-05 in Lev_Tut1b with `@trigger`: `MU1` and `MU13` (Arg_SecretCauldron_loop, the
second with `MTRAN_SlowFade`) and `MU12` twice keep the stream (fade, no restart); `MU12` after `MU13` fades 1 s and
switches; `MU7` (silence) fades and stops. Checked 2026-10-05: `MU3` ends after ~14 s with
`Song = None`, `bSongFinished = True`; `JS_GreenCauldron_24` (~25 s, looping) still plays at 70 s.

## Not ported / not checked

- CD music and Galaxy's module (non-mp2) music path: HP1 has neither (`UseDigitalMusic=False`, every song is mp2).
- The original's menu song has a long fade-in at its start; Galaxy has no fade-in, so it is in the mp2 itself (not
  checked by ear).
- Loading a save: Galaxy doesn't force a song change (`LoadMap` calls `SetViewport` with the same viewport, which
  doesn't touch the music), so the song playing before the load goes on until the next `Transition`. Not checked:
  whether the old level's music objects being unloaded stops it (`UnregisterMusic`).
