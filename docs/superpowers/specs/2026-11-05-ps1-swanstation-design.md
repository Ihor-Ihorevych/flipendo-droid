# PS1 Harry Potter in Flipendo (SwanStation) — design

## Intent

Flipendo's launcher (`SetupActivity`) runs the PC games HP1, HP2 and HP3. The owner also has the PlayStation 1 versions
of Harry Potter 1 and 2 (disc images in `C:\Users\Ihor\hpdroid\PS1`) and wants to play them inside Flipendo, not in
another app: "it must be still my flipendo-droid". The emulator therefore ships in the APK, as HP3's engine does.

## Goals

- The launcher has two tabs, **PC** and **PS1**. PC shows today's covers unchanged. PS1 shows two entries, **HP1** and
  **HP2** (PS1 editions). Choosing one and pressing Play starts the game in the emulator.
- **Emulator: SwanStation**, built as a libretro core (`libswanstation_libretro.so`, arm64-v8a) and packed in the APK.
  SwanStation is a DuckStation fork from before DuckStation's licence change, GPLv3 like Flipendo.
- **A libretro host inside Flipendo**: loads the core, runs its frame loop, and handles video, audio and input.
  It runs in its own activity and process (`:ps1`), the way HP3 does, and leaving it returns to the launcher.
- **BIOS: PCSX-Redux OpenBIOS (MIT)** is bundled as the default, so no Sony BIOS is needed. A setting lets the owner
  point at their own BIOS file instead (fallback if OpenBIOS fails on a game or on saves).
- **Disc images**: the launcher finds the PS1 folder the owner picks. HP2 is `BIN/CUE` and loads as it is. HP1 is
  `MDF/MDS` (Alcohol 120%), which SwanStation cannot read; Flipendo converts it to `BIN/CUE` on first use
  (spike result: HP1's MDF has 2448-byte sectors, 2352 data bytes plus 96 subchannel bytes, so conversion keeps the
  first 2352 bytes of each sector and writes a one-track `MODE2/2352` CUE with `INDEX 01 00:00:00`; it ran in the spike).
- **Touch controls**: on-screen PS1 pad (D-pad, face buttons, shoulders, Start, Select), modelled on
  `Hp3TouchOverlayView`. Physical gamepads work too.
- **Saves**: memory cards and save states live in the app's storage per game. In-game saving must work.

## Non-goals

- Cheats, netplay, shaders, rewind, other PS1 games (the folder scan may accept them, but nothing is tuned for them).
- Vulkan rendering.
- Changing HP1/HP2/HP3 PC behaviour in any way.

## Anti-goals (a failure even if a game runs)

- A PC game (HP1, HP2, HP3) launches, installs data, switches language or builds differently.
- Disc images, a Sony BIOS or any game data committed to the repository or put in the APK.
- Any emulator code that is not GPL-compatible shipped in the APK (the licence of every file is checked, see below).
- Saves that look like they work but do not survive a restart.

## Constraints and facts

- Flipendo builds with `android/build-apk.sh` (CMake + NDK 27.2, `javac` + `d8`, `aapt2`, `apksigner`, no Gradle),
  arm64-v8a only, `minSdk 29`, package `io.github.flipendo.spike`. It already ships `libSDL3.so` and `libopenal.so`.
  Activities live in `android/java/io/github/flipendo/spike/`, declared in `android/AndroidManifest.xml`.
- The launcher's game choice is the `setup` preference `game`. `SetupActivity.isHp2()` treats every non-`hp2` value
  as HP1, so a new value (or a separate PS1 preference) must be handled at every place that reads it.
- HP3 is the model for a second runtime: own activity, own process, shared APK, data found or unpacked by the launcher.
- Project rules (`CLAUDE.md`): ask before downloading binaries or installing tools; never commit game data;
  documentation goes in `docs/`; releases come from `chore(release)` commits. The `src/knowwonder` /
  `src/hp1` / `src/hp2` rules concern the PC engine and do not apply to the PS1 host.
- **OpenBIOS file**: `https://raw.githubusercontent.com/RetroPortingToolKit/psxrecomp/master/bios/openbios.bin`,
  524,288 bytes, SHA-256 `fabe498fbf224e4721f12f31b6f5fe0659205e341dc4e5c5f91b9bd1a1011c57`. Checked: it contains
  "PS-X Realtime Kernel OpenBios" and "Copyright (C) 2019-2026 PCSX-Redux authors". The owner accepted it as a
  third-party copy; the source (`github.com/pcsx-redux/pcsx-redux`, `src/mips/openbios`, build `make -C src/mips/openbios`)
  and the hash go in the docs so it can be rebuilt later.
- **OpenBIOS risks**: it has no memory-card save handler as far as found, and game compatibility is not verified for
  these two games. Both are tested before anything else is built on top (first plan task).
- The core's source (`github.com/libretro/swanstation`) must be fetched and built with the NDK. The owner's rules
  require asking at that point.

## Decisions

Owner's: PC/PS1 tabs with HP1 and HP2 under PS1; SwanStation; built into Flipendo (no other app); OpenBIOS is fine.

My calls (not agreed, change freely):
- Host written in C++ on **SDL3** (already shipped: GLES context, audio stream, gamepads), loading the core with
  `dlopen`; Java side only for the activity and the touch overlay.
- (Changed by the owner: "OpenGL from start".) The first version uses the core's **OpenGL ES hardware renderer**
  through libretro's hardware-render interface, on the GLES context SDL3 creates; resolution upscaling is a
  setting. The software renderer stays only as a fallback if the spike shows the GLES path does not work.
- Overlay and gamepad mapping follow the HP3 overlay's look.
- Automatic MDF→BIN/CUE conversion into the app's storage, deleted when the owner asks.
- Memory cards as `.mcd` files per game, plus one save state slot in the pause menu.

## Left to the builder

- Exact JNI/SDL3 split; whether `Ps1Activity` extends `SDLActivity` or only uses its native library.
- Core build flags for NDK 27.2 and whether SwanStation's licence headers are all GPL-compatible
  (reject any file that is not).
- Performance targets: should hold 50/60 fps on the owner's phone at the chosen upscale; the default factor is the
  builder's call. Whether SwanStation's libretro GLES renderer works on Android with an SDL3 context is unverified:
  the spike checks it first.
- Launcher layout details of the tabs and the PS1 covers.
- Whether to write docs under `docs/ps1.md` and a ROADMAP entry (expected: yes).

## Order of work

1. Spike: fetch and build the core; run HP2 headless with OpenBIOS; test memory-card saving and HP1's converted image.
   Throwaway; its result may change this design.
2. Host (video, audio, input) and `Ps1Activity`.
3. Launcher tabs, PS1 folder detection, settings (BIOS override).
4. Touch overlay, docs, tests, release.
