# Roadmap

What's left to do on the HP1 port, and what's next. Only open work is listed: remove an item when it lands (what
was done is in the git history, how it works in `docs/`, [docs/README.md](docs/README.md)).

Legend: [~] partly done / in progress · [ ] not started

## Next up (in order)
1. **Dark levels**: compare Lev5_FlyKeys' lighting with the original ([docs/debug-tools.md](docs/debug-tools.md#comparing-with-the-original)).

Visual parity checks against the original, side by side, wait until the end (after gameplay works): mesh lighting
(brightness, specular highlights, light fades), animations (walk/run/breathe, tween blends, aux channels) and
particle effects.

## 1. Characters move (skeletal animation)
- [~] Channel blending in the pose (AuxAnims per bone subtree) — implemented, not yet verified in game
- [ ] Verify animations visually against the original (walk/run/breathe, tween blends)

## 2. Engine gaps found in the first levels
- [ ] Windowed mode: menu mouse mapping (`WindowsMouseX/Y` into the 4:3 area) not yet tested in game
- [ ] TraceTexture: decals not traced yet
- [ ] `Actor.Opacity`: back-to-front face sort inside a translucent mesh not ported

## 3. Spells
- [ ] Verify particle effects against the original side by side (spell trails, fires). Checked in ours only: Lev_Tut1 torch
      fires, the Lev2_HogFront fountain (liquid streaks), Lev2_fire1 water drips (hanging drops), a spell cast's puff

## 4. Save games and front end
- [ ] Play the whole New Game → save point → quit → Load Game loop by hand through the menus (only driven with
      `HP1_EXEC` so far), and a save made after a level change (autosave on the first tick)
- [~] After loading, the camera sits a little further back than when saved: not reproduced with a standstill save in
      Lev_Tut1 (camera and screenshots identical, [docs/re/engine/savegames.md](docs/re/engine/savegames.md)); try a save taken
      while the camera moves

## 5. Remaining native classes and polish
- [ ] Bugs of the original to fix, not reproduce: [docs/re/hp1/original_bugs.md](docs/re/hp1/original_bugs.md)
- [ ] Console.CreateNativeFont glyphs for the Asian languages (only Latin and Cyrillic are rasterized), and system
      font text drawn at the screen's resolution instead of scaled with the menu canvas
- [ ] `Actor.Fatness`: not ported; only matters if a map or script changes it from 128
      ([docs/re/engine/animation.md](docs/re/engine/animation.md))
- [ ] US vs UK HP1: compare `HPBase.u` from both releases for a shifted token table
      ([docs/re/engine/scripting.md](docs/re/engine/scripting.md))
- [ ] Linux: a debug screenshot (`HP1_SHOTS`) crashes the game in the Radeon Vulkan driver right after the first image is
      saved (`VulkanRenderDevice::Unlock` -> `vkCmdEndRenderPass`), even on the main menu
- [ ] Menu song fade-in at its start: MusicFade/transitions or the mp2 itself ([docs/re/engine/music.md](docs/re/engine/music.md))
- [ ] Next SurrealEngine update: upstream's HP1 boot work (July 2026: exe detection, HP natives and tokens, mp2,
      4:3 `GetRes`) is already in our submodule; drop any of our patches that it now duplicates

## 6. Extras (`src/hp1/mods/`)
- [ ] Per-mod on/off and settings in an ini section (`[Flipendo.Mods]`), not only command-line flags; an *Extras*
      page in the options book (FEBook) to toggle them in game
- [ ] Fixed 60 Hz game logic, drawing at any frame rate: the game ticks (scripts, physics, cutscenes) at a fixed
      1/60 s, zero or more ticks per drawn frame, so every frame-rate bug of the original behaves as at 60 fps, found
      or not; drawing runs at the monitor's rate (or uncapped) and interpolates between the last two ticks: actor
      locations and rotations, animation frames and tweens, the camera, particles. Needs a main-loop hook
      (`Engine::Run`: tick and render split) and the interpolation in the renderer. An option, the 60 FPS cap stays
      the default until it is tested level by level
- [ ] More built-in extras: FOV slider, controller support, free camera
- [ ] HD texture packs (issue #4): load replacement textures from the game folder, so packs made for the community
      DX11 renderer work, e.g. Akven's ESRGAN Upscale Pack v3 (2,537 `Textures/<Package>/<Group>/<Name>.dds`, DXT1
      and DXT5 only, 4x upscales; its `System/tessellation.cfg` is for that renderer). SurrealEngine renders BC1/BC3
      already, so a texture load looks the file up (case-insensitive), reads the DDS mips and swaps `UsedMipmaps` /
      `UsedFormat` while `USize`/`VSize` stay the original's (BSP and canvas UVs use them). Mesh UVs scale by the
      first mip's width (`VisibleMesh.cpp`, `KWSkeletal`), so they need the original width too. Check masked and
      translucent textures (DXT1 alpha vs the game's masking). On when the folder exists, off with `--vanilla`
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

## Later: the other KnowWonder games
- [~] HP2 (Chamber of Secrets, UE1 build 433): port after HP1 (groundwork: [docs/one-engine.md](docs/one-engine.md)).
  Estimate (2026-10-17): about 3 weeks at HP1's pace (10-19 working days). The engine side is ~85-90% reused (180 of
  ~183 natives covered; of 205 IDA-tagged ports 64 carry over as is, 56 are exec wrappers that differ only by the
  DebugInfo check, 66 need reading in HP2); the 43-map playthrough is new work. In order:
  Status (2026-10-17): boots on Linux straight into a new game; the intro and its cutscenes play (with Enter to skip)
  through the flying car to the Whomping Willow. Not tried past that; no saves, no start menu. In order:
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
  - [ ] Cutscene commands that start before the previous one finished ("Trying to issue command to: ... when previous
        CutCommand hasnt finished yet": Dobby and Harry in PrivetDr, `FordFlying0`, Harry in Adv1Willow): a move or
        animation that doesn't report done. Compare with the original before calling it a bug
  - [ ] Skipping: "SKIP ERROR ... failed to advance after 16 loops" when fast-forwarding PrivetDr's intro
  - [ ] Dobby's and Filch's eye blink channels fail (`eye_close` anim / `eye_root` bone not found): check HP2's mesh
        and animation data against `CreateEyeBlinkAnimChannel`
  - [ ] Accessed None in scripts that the original may not hit: `harry.CutCommand`, `HPConsole.Tick`,
        `harry.PlayerWalking.Landed`, `harry.TravelPostAccept`, `harry.PlayerTick`, `baseWand.StopChargingSpell`
        (`fxChargeParticles`), `FESoundVideoPage.Created` (volume labels)
  - [ ] HP2 music: check `src/hp2/HP2Music.cpp` (written from the scripts) against ALAudio.dll: crossfades between
        songs, looping, volumes; `StopSound`'s fade-out, `PlaySound`'s Disable3D and Loop
  - [ ] XA sounds: check the decoder by ear (nibble order), and the stereo XA case
  - [ ] Check the remaining "changed" ports for real behaviour differences (beyond the DebugInfo check)
  3. Menus and options (1-2 days)
  - [ ] The in-game book (Esc): pages, rollover sounds, the folio, the map; HP2's menu canvas (HP1's 4:3 menu canvas
        and UI scaling in `src/hp1/HP1Canvas.cpp` are HP1 only)
  - [ ] Console commands the options page sends: `SETVOLUMES SOUND=` / `MUSIC=`, `MLOD`, `IsFullscreen`, and the
        `[Engine.Engine] GameRenderDevice` ini read
  - [ ] Resolution and full screen for HP2 (`system/SE-Game.ini`, made by hand for the 2026-10-17 playtest)
  4. Play the 43 maps through, incl. the 10 duels and Quidditch (4-8 days)
  5. Polish (1-2 days)
  - [ ] Check the unverified HP2 engine differences in HP2's code ([docs/re/hp2/gameplay.md](docs/re/hp2/gameplay.md)):
        music always loops, save loading, lip sync, movers colliding by bounding box
  - [ ] HP2-only engine features ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): the `GAMESTATE=` command line
        override, Lumos surfaces, TurnTo's flattened focus, particle Opacity, I3DL2 reverb, oval collision cylinders
  - [ ] Linux: `HP1_SHOTS` screenshots crash the game after the first one (also HP2), which limits automated testing
- [ ] HP3 (Prisoner of Azkaban, UE2 build 2226, packages 129): no UE2 counterpart of SurrealEngine exists. First check,
      with UELib: how many native classes/functions its gameplay packages have (HP1's have none). Mostly script =
      extending SurrealEngine towards UE2 is worth a look; otherwise fixes for the original exe are the better route
