# Roadmap

What's left to do on the HP1 port, and what's next. Only open work is listed: remove an item when it lands (what
was done is in the git history, how it works in `docs/`, [docs/README.md](docs/README.md)). Per-level play status is
[docs/playtest.md](docs/playtest.md), whose code is still SurrealEngine's [docs/surrealengine-coverage.md](docs/surrealengine-coverage.md),
the native-level checklist [docs/re/reports/native_audit_hp1.md](docs/re/reports/native_audit_hp1.md) (`python tools/native_audit.py`).

Legend: [~] partly done / in progress · [ ] not started

## Next up (in order)
1. **Play the game through in story order** and fix what breaks ([docs/playtest.md](docs/playtest.md)). Next:
   Lev2_HogFront_2.
2. **SurrealEngine's guesses, reversed in order** (phase 5 below): next Galaxy's pan law and ambient Doppler, then
   vertex mesh lighting.
3. **Dark levels**: compare Lev5_FlyKeys' lighting with the original (`tools/orig_shots.ps1`).
4. **First prebuilt release** (see "Releases"): players can't try Flipendo without building it.

Visual parity checks against the original, side by side, wait until the end (after gameplay works): mesh lighting
(brightness, specular highlights, light fades), animations (walk/run/breathe, tween blends, aux channels) and
particle effects.

## 1. Characters move (skeletal animation)
- [~] Channel blending in the pose (AuxAnims per bone subtree) — implemented, not yet verified in game
- [ ] Verify animations visually against the original (walk/run/breathe, tween blends)

## 2. Playable tutorial (Lev_Tut1)
- [ ] Some cutscene kids reportedly look like they walk while running (all play `run` at rate 1.5 with
      finished tweens; needs a closer look at which ones)
- [ ] Kids spawned on the same patrol point can overlap (UE1 Spawn fails when the spot is occupied?)
- [ ] Windowed mode: menu mouse mapping (`WindowsMouseX/Y` into the 4:3 area) not yet tested in game
- [ ] TraceTexture: decals not traced yet
- [ ] `APawn::physicsRotation`: flying/swimming roll banking not ported yet
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
- [ ] Save/LoadObjectAsFile, CreateTextureFromScreenShot, `Snap 3`: no script uses them (low priority)

## 5. Remaining native classes and polish
- [~] SurrealEngine's guesses replaced with HP1's code, in this order
      ([docs/surrealengine-coverage.md](docs/surrealengine-coverage.md); move each row there from "Guess" to "HP1"):
  - [~] Sound: Galaxy's pan law and ambient Doppler ([docs/re/engine/sound.md](docs/re/engine/sound.md))
  - [ ] Vertex mesh lighting (props, non-skeletal meshes)
  - [ ] Sprites and decals (spell sprites, actor shadows)
  - [ ] `Spawn` and `Destroy` (placement when blocked, event order)
  - [~] BSP drawing rules: masked/translucent/modulated, two-sided, panning, sky zone, mirrors, fog
  - [ ] Canvas text and tiles (HUD, menus, storybook)
  - [ ] Level load and travel (`LoadMap`, `ClientTravel`, event order)
  - [ ] The remaining light effects (~60 lights), the Waver flicker, the factor 2 behind the lights' 0.5
  - [ ] The rest of the coverage list's "Guess" rows, as they come up
- [ ] Bugs of the original to fix, not reproduce: [docs/re/hp1/original_bugs.md](docs/re/hp1/original_bugs.md)
- [ ] Console.CreateNativeFont for the Asian languages (WinDrv.dll's GDI font rasterizer)
- [ ] `Actor.Fatness`: not ported; only matters if a map or script changes it from 128
      ([docs/re/engine/animation.md](docs/re/engine/animation.md))
- [ ] US vs UK HP1: compare `HPBase.u` from both releases for a shifted token table
      ([docs/re/engine/scripting.md](docs/re/engine/scripting.md))
- [ ] Menu song fade-in at its start: MusicFade/transitions or the mp2 itself ([docs/re/engine/music.md](docs/re/engine/music.md))
- [ ] Next SurrealEngine update: upstream's HP1 boot work (July 2026: exe detection, HP natives and tokens, mp2,
      4:3 `GetRes`) is already in our submodule; drop any of our patches that it now duplicates
- [ ] Editor-only natives (BrushBuilders) — low priority

## 6. Modding (`src/hp1/mods/` and beyond)
- [ ] Per-mod on/off and settings in an ini section (`[Flipendo.Mods]`), not only command-line flags; an *Extras*
      page in the options book (FEBook) to toggle them in game
- [ ] Drop-in content mods: `Mods/<name>/` folders added to the package search path ahead of the originals
      (`PackageManager::ScanPaths` already reads `Core.System` Paths from the ini, and `ScanFolder` keeps the first
      folder that has a package of that name, so mod folders must be scanned before the originals). Define load
      order between mods, and test a texture pack and a custom map
- [ ] Script mods: replace a game class with a mod subclass at spawn time (e.g. `HarryPotter.harry` → `MyMod.MyHarry`),
      configured in the ini, without touching the original packages
- [ ] Run custom levels made by the HP1 modding community (collect a few test maps, list what breaks)
- [ ] Fixed 60 Hz game logic, drawing at any frame rate: the game ticks (scripts, physics, cutscenes) at a fixed
      1/60 s, zero or more ticks per drawn frame, so every frame-rate bug of the original behaves as at 60 fps, found
      or not; drawing runs at the monitor's rate (or uncapped) and interpolates between the last two ticks: actor
      locations and rotations, animation frames and tweens, the camera, particles. Needs a main-loop hook
      (`Engine::Run`: tick and render split) and the interpolation in the renderer. An option, the 60 FPS cap stays
      the default until it is tested level by level
- [ ] More built-in extras: FOV slider, controller support, free camera
- [ ] In-game modding tools: Dear ImGui overlay with actor list, live property inspector, console and mod manager,
      built from the `HP1_DUMP`/`HP1_TRACE`/`HP1_EXEC` debug tools ([docs/modding.md](docs/modding.md))
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
- [~] Modder docs: [docs/modding.md](docs/modding.md) (writing a mod, hooks, tools); still missing a worked
      "first mod" walkthrough

## 7. Editor (grows out of the §6 ImGui overlay)
The editor runs inside the game (no separate UnrealEd-style program): play-in-editor for free, edits visible while
the game runs. Steps 1–2 can go alongside the first playable release (they double as debugging tools); the rest
after it.

Ground rules:
- Read and write the original formats (`.unr`, `.u`), so existing community maps open in Flipendo and Flipendo maps
  are ordinary UE1 packages. No private map format.
- UnrealScript stays the game's language. The new scripting layers sit on top and never replace it, so existing
  mods keep working.
- Same editor for HP1 and HP2, gated like the rest (`IsKnowWonder()`), and it runs on Linux too.
- First check what SurrealEngine's own `SurrealEditor` (builds alongside the game) already has: package loading,
  viewports, anything reusable. Building on it means less of our code in `src/engine/`.

1. Core
   - [ ] Object tree: every actor in the level, filterable (class, tag, event, name, gamestate) and sortable, click to
         select and focus the camera
   - [ ] 3D previews of meshes, textures and prefabs in the browsers
   - [ ] Property inspector with edit (the §6 overlay one), showing which values differ from the defaults
   - [ ] ImGuizmo for move / rotate / scale, grid and angle snapping, multi-select
   - [ ] Load maps and packages from the editor (file picker, recent list, `Mods/` folders included)
   - [ ] Save to `.unr`: package writer (name / import / export tables). **Biggest risk:** BSP / CSG rebuild after
         moving brushes; check early whether anything usable exists (SurrealEngine has no builder) before relying on it
   - [ ] Undo / redo, copy / paste (also across maps), autosave and crash recovery
2. Clarity
   - [ ] Trigger graph: Event → Tag links drawn as lines in the viewport, highlight what a trigger fires
   - [ ] Validator: broken references, missing textures, triggers that fire nothing, unreachable path nodes; click a
         result to jump to it
   - [ ] Gamestate preview: switch GState000/GState010/… and see which actors exist
   - [ ] Search across all maps and packages ("where is this class used?")
   - [ ] Prefab browser
3. Scripting
   - [ ] Sequence editor (timeline + nodes) for cutscenes and scripted moments; it outputs the game's existing cutscene
         commands, so the result runs like an original cutscene
   - [ ] Lua (or similar) for mod logic, with hot reload; bindings to actors, events and the cutscene system
4. Assets and external tools
   - [ ] Blender addon: import / export of the KnowWonder skeletal mesh and animation format (custom characters)
   - [ ] "Edit in external app" for textures (Photoshop / Krita / GIMP): watch the file, reimport live on save. Small
         built-in editor only for quick fixes (alpha, resize, palette)
   - [ ] Text map format (T3D-like) for diffs and teamwork in git
   - [ ] Sound browser with preview, dialogue and lipsync editor, localization strings
5. HP tools
   - [ ] Spell editor: draw new gesture shapes for the recognizer (`Gesture`) and attach effects
   - [ ] ParticleFX editor with live preview (`src/knowwonder/KWParticleFX.cpp`)
   - [ ] Lighting: rebuild lightmaps, per-surface light scale preview
   - [ ] Path-node building and visualization for AI navigation
6. Sharing
   - [ ] "Package mod": writes `Mods/<name>/` with a manifest (version, dependencies, game)
   - [ ] In-game mod browser: enable / disable, load order; later a community mod index

## Speedrun practice and research tools
What runners already have for the original and why these add to it: [docs/speedrunning.md](docs/speedrunning.md).
All of it is mods (`src/hp1/mods/`, off with `--vanilla`) that never change the game's rules; worth showing to the HP1 PC
community once the levels they run play like the original.

1. Practice
   - [ ] Save state anywhere and restore it with one key (exact actor state, not only at save points)
   - [ ] Warp: store and return to a position (and the camera with it, as `@teleport` now does)
   - [ ] Trick reset loop: one key back to the start of a trick, attempt counter
   - [ ] Input recording and replay (from `HP1_KEYS` / `HP1_MOUSE`); first check that replays are deterministic
   - [ ] Jump planning view from `HP1_HEIGHTMAP` (floor heights around Harry)
2. Show what is hidden
   - [ ] Trigger and cutscene volumes drawn in the world, with their Tag and Event
   - [ ] Tag → Event links drawn as lines (shared with §7 "Trigger graph")
   - [ ] Collision shapes (cylinder, CT_Box, movers) and grabbable ledges (PF_SpecialPoly)
   - [ ] Auto-jump landing prediction drawn
   - [ ] Live actor inspector (state, velocity, physics; the §6 ImGui overlay)
3. Timer in the engine
   - [ ] In-game timer with load time removed exactly (the engine knows when it loads)
   - [ ] Splits from engine events: level change, cutscene start/end, lesson passed, save point (the community's
         autosplitter only splits on map entry)
   - [ ] LiveSplit Server link, so runners keep their splits
   - [~] 60 FPS cap with a visible FPS counter, like the community's required mod (the cap is in, §6; no counter yet)

## Releases
- [ ] Prebuilt Windows download on the release page (zip with `SurrealEngine.exe` and its DLLs; no game data), with a
      changelog and a "point it at your game folder" first-run
- [ ] Game folder picker in the launcher remembered between runs, so players never touch the command line
- [ ] Version number in the window title and the log, for bug reports
- [ ] Linux / Steam Deck build tested with Flipendo

## Later: the other KnowWonder games
- [~] HP2 (Chamber of Secrets, UE1 build 433): port after HP1 (groundwork: [docs/one-engine.md](docs/one-engine.md), [docs/re/reports/hp2_compare.md](docs/re/reports/hp2_compare.md)).
  - [ ] Hook HP2's token table into SurrealEngine's `BytecodeStream` (skip DebugInfo, `HP2::ToStockToken`)
  - [ ] Check the remaining "changed" ports for real behaviour differences (beyond the DebugInfo check)
  - [ ] HP2 music natives: `PlayMusic` / `StopMusic` / `StopAllMusic` (ALAudio.dll), `StopSound` fade-out
  - [ ] Enable `IsKnowWonder()` for HP2, OpenAL/Ogg audio (ALAudio.dll), first level
  - [ ] Check the unverified HP2 engine differences in HP2's code ([docs/re/hp2/gameplay.md](docs/re/hp2/gameplay.md)):
        music always loops, XA ADPCM sounds, save loading, lip sync, movers colliding by bounding box
  - [ ] HP2-only engine features ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): GameState screening
        (`OnResolveGameState`), Lumos surfaces, TurnTo's flattened focus, particle Opacity, I3DL2 reverb
  - [ ] Persistent actors across hub travel ([docs/re/hp2/engine.md](docs/re/hp2/engine.md)): `bPersistent` actors keep
        their state when Harry leaves a hub map and comes back (`SavePActors`, the persistent actor cache)
- [ ] HP3 (Prisoner of Azkaban, UE2 build 2226, packages 129): no UE2 counterpart of SurrealEngine exists. First check,
      with UELib: how many native classes/functions its gameplay packages have (HP1's have none). Mostly script =
      extending SurrealEngine towards UE2 is worth a look; otherwise fixes for the original exe are the better route
