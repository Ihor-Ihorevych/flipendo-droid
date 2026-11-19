# Roadmap

What's left to do on Flipendo: Philosopher's Stone (HP1) is finished and Chamber of Secrets (HP2) plays through to
the end; the work now is HP2's remaining differences from the original, plus optional extras and releases. Only open
work is listed: remove an item when it lands (what was done is in the git history, how it works in `docs/`, [docs/README.md](docs/README.md)).

Legend: [~] partly done / in progress · [ ] not started

## HP2: Chamber of Secrets
- [~] HP2 (UE1 build 433), on the same engine as HP1 (groundwork: [docs/one-engine.md](docs/one-engine.md)).
  Status (2026-11-19): playable from start to finish. Players finished the whole story on releases 1.3 to 1.7
  (issues #30, #35; playtest call #16), and between them played all wizard duels, all Quidditch matches and the Gold
  Card Challenge. Every blocker they hit is fixed (#19 to #26, #29, #33). Releases 1.8 and 1.9 fixed what they
  reported after that (#34, #35, #39, #41). What's left are differences from the original that don't stop play.
  Still ported but mostly not checked against the original in play: the HP2 sound system, voices, lip sync, movers,
  stair smoothing and tick order.
  1. Compare with the original
  - [ ] Check in play what was ported but not seen yet: dialogue lines play (`Sounds/AllDialog.USA_uax`); a mover's
        sound loops while it moves, Harry's own sounds and dialogue play centred, StopSound fades out; Dobby's mouth
        moves with his lines in PrivetDr's intro and closes after; an `MV_SpringByTime` mover (find a map with one)
  - [ ] The in-game book's pages, rollover sounds, the folio, the map; the book's menu items lack the original's
        warping haze when the mouse moves over them (#38)
  2. Known bugs
  - [ ] Saves in a game folder under Program Files: fixed in master (8099632, saves go to `%LOCALAPPDATA%\Flipendo\`),
        ships in the next release; confirm with the reporter (#37)
  - [ ] Gryffindor common room, before following Ron to Hagrid's hut: the mouse stops turning Harry until he leaves
        for the grounds (reported in #16 on 2026-11-17, version not given,
        Windows 11, log attached there)
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
- [~] Game folder picker: SurrealEngine's launcher has a Game Folders page (Add opens a folder dialog, the list is
      kept in its settings); check it finds HP1 and HP2 folders in the release zips, then make it the README's way in
      instead of dragging the folder onto the exe
