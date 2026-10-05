# Music

How HP1 plays its songs, from Galaxy.dll (`UGalaxyAudioSubsystem::Update` [HP1 Galaxy 0x106081F0]; addresses are
HP1's Galaxy.dll). Ported in `src/knowwonder/KWSound.cpp` (`KW::MusicSource`, `KW::TickMusic`), hooked into SurrealEngine's
`USurrealAudioDevice::UpdateMusic` ([engine-hooks.md](../../engine-hooks.md)).

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
- So every song loops unless a script asked for it to play once. In Lev_Tut1b, `MusicEvent2` (tag `MU3`,
  `JS_HogwartsNeutral_14`, about 14 s) and `MusicEvent1` (`FlipBridge1Dis`) play once; the rest loop.

## What SurrealEngine did

It played every mp2 song once. When the stream ended, the music thread kept queueing its fixed buffers without
clearing the part the decoder didn't fill, so the last chunks of old audio replayed forever: a glitchy ~1 s loop at
the end of every song (reported near Nearly Headless Nick in Lev_Tut1b). Fixed for every game by filling the rest of
the buffer with silence (`Audio/AudioDevice.cpp`); for HP1, `KW::MusicSource` restarts the song at its end (a new mp3
decoder: SurrealEngine's mp3 source doesn't rewind after reaching the end) unless `bDontLoopSong`, and `KW::TickMusic`
sets `bSongFinished` and clears an ended song like Galaxy. Checked 2026-10-05: `MU3` ends after ~14 s with
`Song = None`, `bSongFinished = True`; `JS_GreenCauldron_24` (~25 s, looping) still plays at 70 s.

## Not ported

- `MusicFade` and the transition kinds (Galaxy fades the old song over time for MTRAN_Fade / FastFade / SlowFade);
  upstream switches songs at once. The original's menu song has a long fade-in at its
  start; check whether that is this fade or in the mp2 itself.
- `PercentMusicVolume` (MusicEvent's volume boost).
