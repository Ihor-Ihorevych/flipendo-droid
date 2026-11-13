# Roadmap

What's left to do on Flipendo: Philosopher's Stone (HP1) is finished; the work now is Chamber of Secrets (HP2) on the
same engine, plus optional extras and releases. Only open work is listed: remove an item when it lands (what was done
is in the git history, how it works in `docs/`, [docs/README.md](docs/README.md)).

Legend: [~] partly done / in progress · [ ] not started

## HP2: Chamber of Secrets
- [~] HP2 (UE1 build 433), on the same engine as HP1 (groundwork: [docs/one-engine.md](docs/one-engine.md)).
  Status (2026-11-13): players are now playtesting it (issue #16, releases 1.3 and 1.4) and reporting bugs; four were
  fixed on 2026-11-11 (vendor trading crash #19, large fire crabs' fireballs crash #21, flipped crab on a lift #22,
  invisible wand #25). Nobody has played it through to the end yet, so how close it is to the original isn't known.
  Known to work: Game.exe's front end (splashes, New Game, Load Game, Options), the intro and its
  cutscenes with subtitles (Enter skips them), the in-game book and options at any resolution, saving and loading,
  hub persistence; contributors have played on into later levels (Peeves' Skurge fight, Aragog's web). The HP2 sound
  system, music, voices, lip sync, movers, walking, stair smoothing and tick order are ported from HP2's DLLs but
  mostly not checked in play.
  1. A full playtest
  - [ ] Play the 43 maps through, incl. the 10 duels and Quidditch, and list what differs from the original
  - [ ] Check in play what was ported but not seen yet: dialogue lines play (`Sounds/AllDialog.USA_uax`); a mover's
        sound loops while it moves, Harry's own sounds and dialogue play centred, StopSound fades out; Dobby's mouth
        moves with his lines in PrivetDr's intro and closes after; an `MV_SpringByTime` mover (find a map with one)
  - [ ] The in-game book's pages, rollover sounds, the folio, the map
  2. Known bugs
  - [ ] Bean bonus room after the house points ceremony: the game hangs if Harry moves and picks up a bean right
        after skipping the ceremony (issue #20, Windows 10, build 593bf9c). Not reproduced on Linux or Windows 11 with
        the reporter's save; waiting for a dump of the hung process
  - [ ] Can't enter the Slytherin common room as Goyle (issue #23). Not reproduced by jumping into the room; waiting
        for a save from just before the drop
  - [ ] Mirrors look wrong (playtest 2026-10-21, after PrivetDr's intro). PrivetDr's only mirror surface
        (`@surfs *mirror`) is an outside window, `extwindow_PD` (surface 991, PolyFlags 0x08000000, center -832,776,272):
        SurrealEngine draws the reflection and seals the surface invisible, so the window texture never shows. Check how
        HP2's Render.dll draws a mirror surface's own texture: its PF_Mirrored tests in `URender::OccludeBsp`
        (0x10B22F62, 0x10B23550) and `URender::DrawFrame` (0x10B2613B)
  - [ ] Lumos: after casting it the light beam on the wand stays for good (playtest 2026-10-17). The LumosLight
        itself goes off after its 30 s (light and LumosLightFX, checked 2026-10-20 with baseWand.LumosTurnOn), so the
        beam is another effect. Not the wand's charge particles: driven with `@console.Viewport.Actor.Weapon
        StartChargingSpell True False Class'hgame.spellLumos'` then `StopChargingSpell` (2026-10-21), `Lumos_fly`'s
        `Shutdown()` caps ParticlesMax at what it emitted and the system is gone within its 2 s Lifetime. Needs a real
        cast to see which effect stays (the flying spell's own `Lumos_fly`, `Lumos_hit`, the spell cursor)
  - [ ] Hermione gets stuck on other NPCs in the escort sequence (PR #12, after the inherited-state fix)
  - [ ] PrivetDr's intro flyover: the camera now keeps BaseCam's own rotation on the crane spline (no more map edges);
        compare the path and the shot's brightness with the original in play, and the sky through the backdrop surfaces
  - [ ] Cutscene commands that start before the previous one finished ("... hasnt finished yet"): the script timing in
        every case checked ([docs/re/hp2/gameplay.md](docs/re/hp2/gameplay.md#cutscenes)); left: compare Dobby's
        `Introduce` (3.5 s at rate .8 here) and Harry's `Look_Frantic` with the original, and Harry in Adv1Willow
        (seen only while skipping)
  - [ ] Skipping: "SKIP ERROR ... failed to advance after 16 loops" when fast-forwarding PrivetDr's intro (not
        seen 2026-10-20 in 4 runs skipping every cutscene from PrivetDr to Adv1Willow at different moments)
  3. Code still to read in HP2's DLLs
  - [ ] The remaining "changed" ports, for real behaviour differences (beyond the DebugInfo check)
  - [ ] The unverified HP2 engine differences ([docs/re/hp2/gameplay.md](docs/re/hp2/gameplay.md)): movers colliding
        by bounding box, the mover flickering fix
  - [ ] HP2's state label lookup: follows the scripts' state and label tables, Core.dll's routine not reversed
  - [ ] HP2-only engine features ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): oval collision cylinders

## Extras for HP1 (`src/hp1/mods/`)
- [ ] Per-mod on/off and settings in an ini section (`[Flipendo.Mods]`), not only command-line flags; an *Extras*
      page in the options book (FEBook) to toggle them in game
- [ ] Fixed 60 Hz game logic, drawing at any frame rate: the game ticks (scripts, physics, cutscenes) at a fixed
      1/60 s, zero or more ticks per drawn frame, so every frame-rate bug of the original behaves as at 60 fps, found
      or not; drawing runs at the monitor's rate (or uncapped) and interpolates between the last two ticks: actor
      locations and rotations, animation frames and tweens, the camera, particles. Needs a main-loop hook
      (`Engine::Run`: tick and render split) and the interpolation in the renderer. An option, the 60 FPS cap stays
      the default until it is tested level by level
- [ ] More built-in extras: FOV slider, controller support
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
- [~] Flipendo 1.5: draft release (tag v1.5, 2026-11-12), with the HP2 fixes made since 1.4; publish it.
      1.4 (2026-11-11) is the latest public one
- [~] Game folder picker: SurrealEngine's launcher has a Game Folders page (Add opens a folder dialog, the list is
      kept in its settings); check it finds HP1 and HP2 folders in the release zips, then make it the README's way in
      instead of dragging the folder onto the exe
