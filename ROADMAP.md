# Roadmap

What's left to do on Flipendo: Philosopher's Stone (HP1) is finished; the work now is Chamber of Secrets (HP2) on the
same engine, plus optional extras and releases. Only open work is listed: remove an item when it lands (what was done
is in the git history, how it works in `docs/`, [docs/README.md](docs/README.md)).

Legend: [~] partly done / in progress · [ ] not started

## HP2: Chamber of Secrets
- [~] HP2 (UE1 build 433), on the same engine as HP1 (groundwork: [docs/one-engine.md](docs/one-engine.md)).
  Estimate (2026-10-17): about 3 weeks at HP1's pace (10-19 working days). The engine side is ~85-90% reused (180 of
  ~183 natives covered; of 205 IDA-tagged ports 64 carry over as is, 56 are exec wrappers that differ only by the
  DebugInfo check, 66 need reading in HP2); the 43-map playthrough is new work.
  Status (2026-10-17): boots on Linux straight into a new game; the intro and its cutscenes play (with Enter to skip)
  through the flying car to the Whomping Willow, with subtitles; the in-game book and the options page work at any
  resolution. Not tried past that; no saves, no start menu, no voices. In order:
  1. Start and save (2-4 days)
  - [ ] Recreate HP2's front end: `Game.exe`'s own Windows menu, which the engine doesn't have
        ([docs/re/hp2/startup.md](docs/re/hp2/startup.md)). The EA splash (`EALogo1.bmp`), then Main Menu (New Game,
        Load Game, Options), the save slot pages, and Options (video, sound, joystick), drawn with HP2's `Help/*.bmp`
        art and `Startup.int` text. New Game starts `PrivetDr.unr`, Load Game `-LOADGAME=<slot>`. Until then a run
        without a URL starts a new game.
  - [ ] `-LOADGAME=<slot>` (HP2's Engine.dll reads it) and HP2's save flow: queued saves, a directory per slot, `LoadGame 0`
        after death ([docs/re/engine/savegames.md](docs/re/engine/savegames.md))
  - [ ] Persistent actors across hub travel ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): `bPersistent` actors keep
        their state when Harry leaves a hub map and comes back (the `SavePActors` console command, logged as unknown on
        every level change, and the persistent actor cache)
  2. The intro and first levels as the original (2-3 days)
  - [ ] Voices: every dialogue line fails with "could not load 'AllDialog.PC_...': Could not find package 'AllDialog'".
        The package is `Sounds/AllDialog.USA_uax`: HP2 names language-specific packages `<name>.<language>_<ext>`;
        find how its Core.dll looks them up and do the same
  - [ ] Adv1Willow: SpottedJellyBean3/6/7 fall out of the world as the map starts (at Z -482): check where they
        start and what they should land on
  - [ ] Cutscene commands that start before the previous one finished ("Trying to issue command to: ... when previous
        CutCommand hasnt finished yet": Dobby and Harry in PrivetDr, `FordFlying0`, Harry in Adv1Willow): a move or
        animation that doesn't report done. Compare with the original before calling it a bug
  - [ ] Skipping: "SKIP ERROR ... failed to advance after 16 loops" when fast-forwarding PrivetDr's intro
  - [ ] PrivetDr's intro flyover: the camera doesn't follow the original's path and shows the map's edges (playtest
        2026-10-17); the shot is also much darker. Check BaseCam's `FollowSpline ... EaseBetween` and FlyTo against HP2's
        code, and the sky through the backdrop surfaces
  - [ ] Dobby's and Filch's eye blink channels fail (`eye_close` anim / `eye_root` bone not found): check HP2's mesh
        and animation data against `CreateEyeBlinkAnimChannel`
  - [ ] HP2 music: check `src/hp2/HP2Music.cpp` (written from the scripts) against ALAudio.dll: crossfades between
        songs, looping, volumes; `StopSound`'s fade-out, `PlaySound`'s Disable3D and Loop
  - [ ] XA sounds: check the decoder by ear (nibble order), and the stereo XA case
  - [ ] Lumos: after casting it the light beam on the wand stays for good (playtest 2026-10-17); check how HP2's
        scripts turn the wand's Lumos effect off
  - [ ] Check the remaining "changed" ports for real behaviour differences (beyond the DebugInfo check)
  - [ ] Engine behaviour HP2's scripts may need, worth checking: state labels inherited from a parent state, class
        references in travel data, dialogue with `Disable3D` playing at full volume wherever the camera is
  3. Menus and options (1 day)
  - [ ] The in-game book's pages, rollover sounds, the folio, the map
  - [ ] `get ini:` / `set ini:` with a class and a key fails with "Could not find class 'None'": `set ini:HGame.Harry
        Difficulty ...` and `ObjectDetail` (the options page's difficulty and detail aren't kept), `get ini:Engine.Engine
        GameRenderDevice`
  4. Play the 43 maps through, incl. the 10 duels and Quidditch (4-8 days)
  5. Polish (1-2 days)
  - [ ] Check the unverified HP2 engine differences in HP2's code ([docs/re/hp2/gameplay.md](docs/re/hp2/gameplay.md)):
        music always loops, save loading, lip sync, movers colliding by bounding box
  - [ ] HP2-only engine features ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): the `GAMESTATE=` command line
        override, Lumos surfaces, TurnTo's flattened focus, particle Opacity, I3DL2 reverb, oval collision cylinders
  - [ ] Linux: `HP1_SHOTS` screenshots crash the game after the first one (also HP2), which limits automated testing
        (segfault in radv's `vkCmdEndRenderPass`, called from `VulkanRenderDevice::Unlock` in `RenderSubsystem::DrawGame`
        right after the shot is saved)

## Extras for HP1 (`src/hp1/mods/`)
- [ ] Per-mod on/off and settings in an ini section (`[Flipendo.Mods]`), not only command-line flags; an *Extras*
      page in the options book (FEBook) to toggle them in game
- [ ] Fixed 60 Hz game logic, drawing at any frame rate: the game ticks (scripts, physics, cutscenes) at a fixed
      1/60 s, zero or more ticks per drawn frame, so every frame-rate bug of the original behaves as at 60 fps, found
      or not; drawing runs at the monitor's rate (or uncapped) and interpolates between the last two ticks: actor
      locations and rotations, animation frames and tweens, the camera, particles. Needs a main-loop hook
      (`Engine::Run`: tick and render split) and the interpolation in the renderer. An option, the 60 FPS cap stays
      the default until it is tested level by level
- [ ] More built-in extras: FOV slider, controller support, free camera
- [ ] Texture packs: check masked and translucent overrides (a DXT1 file's 1-bit alpha against the game's masking)
- [ ] Twitch chaos mod (opt-in, `--chaos`): stream viewers vote in chat on effects every N seconds (low gravity,
      giant or tiny Harry, a random spell cast, camera upside down, slow motion, Harry runs backwards, beans
      rain). Each effect is a timed `@set` on live actors / `Level.TimeDilation`, undone when it ends; a vote
      bar drawn in `PostRenderMods`. Reads chat over Twitch's anonymous IRC (no login, no keys); an offline
      mode picks effects at random. Effects must never break a save (all undone before saving)
- [ ] Restored content mod (opt-in), from [docs/re/hp1/cut_content.md](docs/re/hp1/cut_content.md). First check
      whether the cut actors still work when spawned (`Hub2.h2barron` Bloody Baron fight, `mirrortarget` shrinking
      Harry, `InvisibilityCloak` pickup, `FloatingSpellBook`); do the mod only if most do. Then add the unused audio:
      Lee Jordan's alternate takes as commentator variants, wizard card voice lines in the Folio, student chatter,
      extra final-battle taunts. Cut spells are out (no level content uses them)

## Releases
- [ ] Game folder picker in the launcher remembered between runs, so players never touch the command line
- [ ] Version number in the window title and the log, for bug reports
- [ ] Linux download (prebuilt) and a Steam Deck build tested with Flipendo
