# PS1 Harry Potter (SwanStation): spike results

Design: `docs/superpowers/specs/2026-11-05-ps1-swanstation-design.md`. Checked 2026-11-05 on a 2311DRK48G
(MediaTek mt6897) with a throwaway headless libretro host (EGL pbuffer + GLES 3, no APK).

## What was verified

- **The core builds with the NDK, unchanged.** `libretro/swanstation` (GPLv3, last commit 2026-10-24) configures with
  the NDK 27.2 CMake toolchain (`-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29`, Ninja from the SDK's CMake
  3.22.1) and `--target swanstation_libretro` gives `swanstation_libretro_android.so` (27 MB, 25 `retro_*` exports,
  AArch64 recompiler on). Its `Makefile.libretro` has no Android platform; use CMake.
- **OpenGL ES works.** With `RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER` answering `RETRO_HW_CONTEXT_OPENGLES_VERSION`
  the core asks for a GLES 3.2 context (`context_type` 5), runs on the phone's GPU, builds its shader cache in the system
  directory (`swanstation/`) and renders correctly, also at 2x. Speed in the headless host (no vsync): about 800-1280 fps
  (13-20x real time) against about 245 fps for the software renderer.
- **OpenBIOS boots both games.** The core recognises it by the `OpenBIOS` magic at offset 0x78 and offers `openbios.bin`
  as a BIOS choice (`swanstation_BIOS_PathNTSCU` / `PathPAL` / `PathNTSCJ`). HP2 (SLUS-01503) reaches the EA logo and the
  language screen; HP1 (SLUS-01415) reaches the Warner Bros. screen and the language screen.
- **HP1's MDF/MDS converts.** The `.mds` (version 1.3) has one track of 2448-byte sectors (2352 data + 96 subchannel).
  Keeping the first 2352 bytes of each sector and a `MODE2/2352` CUE (`INDEX 01 00:00:00`) gives an image SwanStation
  boots (256,148 sectors; 627,050,304 / 2448 exactly).

## Host notes (for the real host)

- The core's `max_width x max_height` (1024x512) does not grow with the resolution scale, but frames do (1280x896 at
  2x). Size the framebuffer from the scaled geometry (or resize when `SET_GEOMETRY`/`SET_SYSTEM_AV_INFO` arrives).
- Needed environment calls: log interface, system and save directories, pixel format, `GET_PREFERRED_HW_RENDER`,
  `SET_HW_RENDER` (set `get_current_framebuffer` and `get_proc_address`), `GET_VARIABLE` (core option values). Everything
  else can return false. Call `context_reset` after `retro_load_game`, with the GL context current.
- Core options to set: `swanstation_GPU_Renderer`, `swanstation_GPU_ResolutionScale`, `swanstation_BIOS_Path*`.

## Not verified yet

- **Memory-card saves under OpenBIOS** (OpenBIOS seems to have no card handler). Needs a scripted run that starts a
  game, saves, restarts and loads.
- Audio and input on the real activity, and performance with vsync on the real display.

## OpenBIOS

`openbios.bin` (524,288 bytes, SHA-256 `fabe498fbf224e4721f12f31b6f5fe0659205e341dc4e5c5f91b9bd1a1011c57`), from
`github.com/RetroPortingToolKit/psxrecomp` (`bios/openbios.bin`), a copy of PCSX-Redux's OpenBIOS (MIT; source
`github.com/pcsx-redux/pcsx-redux`, `src/mips/openbios`, build `make -C src/mips/openbios`). It contains "PS-X Realtime
Kernel OpenBios" and "Copyright (C) 2019-2026 PCSX-Redux authors". Not yet rebuilt from source.

## In the app (built 2026-11-05)

The PS1 games are the third runtime of the APK, next to the PC engine and HP3, and use none of their code.

| Part | File | Job |
| --- | --- | --- |
| Core | `android/deps/swanstation` (cloned by `build-apk.sh` at the pinned commit `631f0fb`, git-ignored) | built with CMake into `libswanstation.so` |
| Host | `android/jni/ps1_host.cpp` (`libps1host.so`) | the libretro frontend on SDL3: GLES 3.x context, the core's framebuffer blitted letterboxed to its aspect ratio, SDL audio stream as the clock (the loop waits while more than about two frames are queued), gamepad and key input, save states |
| Activity | `Ps1Activity` (process `:ps1`, extends `SDLActivity`, `getLibraries()` = SDL3 + ps1host) | prepares the BIOS and folders, the pause menu (back key or the menu button: resume, save state, load state, quit) |
| Pad | `Ps1PadLayout` (geometry, tested), `Ps1PadView` (drawing, multi-touch) | on-screen PS1 pad; ids are libretro's joypad ids |
| Discs | `Ps1DiscConverter`, `Ps1Setup` (tested) | find a disc, MDF to BIN/CUE, picked files and folders, BIOS check |
| Launcher | `SetupActivity` | PC / PS1 tabs; the PS1 tab has its own game choice (`ps1.game`), so the PC choice is kept |

- **Where things are**: discs are looked for in `/sdcard/FlipendoPS1/HP1` and `HP2` (and one folder inside), unless the
  player picks a folder (*Choose folder*) or one image file (*Choose image file*; a picked `.mds` means its `.mdf`, a
  `.bin` its `.cue`). A picked file wins over a picked folder, which wins over the default folder. Converted images go to
  `<game folder>/disc`, memory cards and save states to `<game folder>/saves`.
- **BIOS**: the bundled OpenBIOS (`android/ps1/`, copied to the app's files on first start) unless a 512 KB file is picked.
- **Resolution** is the core's GPU scale, 1x to 4x (default 2x); it sets the size of the framebuffer.
- **Start from adb** (a development aid; the activity is exported):
  `adb shell am start -n io.github.flipendo.spike/.Ps1Activity --es ps1.disc /sdcard/FlipendoPS1/HP2/HARRY_POTTER_2.cue --es ps1.game hp2 --ei ps1.scale 2`
  and `adb logcat -s ps1host:* ps1core:*` (the host logs the speed every 600 frames).
- **Build**: `android/build-apk.sh` fetches and builds the core and the host with the rest; needs nothing more than the
  APK build already does.
- **Checked on the phone (2311DRK48G, Mali-G615)**: HP2 and HP1 run at 60 / 50 fps locked to the audio clock, the HP1
  MDF is converted in the app in about 8 seconds, the pad, the picture and the letterboxing look right.

### Not done yet

- In-game saving under OpenBIOS (memory card) is still unchecked; save states are the safe way meanwhile.
- No software-renderer fallback (the host answers only a GLES context request).
- The analogue stick and rumble: the core gets the digital pad only. L3/R3 exist on gamepads only.
- The pad's look (size, opacity, hiding it when a gamepad is connected) is not adjustable.
- Texts of the PS1 tab are English with the launcher's translation fallback (no translations added yet).

## Discs in the self pack (CHD)

`android/build-apk.sh --selfpack ... --ps1-hp1 HP1.chd --ps1-hp2 HP2.chd` puts the discs in the self pack
(`assets/ps1data/hp1`, `hp2`, each with a `.stamp`). The first time the PS1 tab's Play is pressed for a game that has
no disc picked by the player, the launcher unpacks it with `installBundle` into `/sdcard/FlipendoPS1/HP1` or `HP2`
(progress in the footer), then starts it; later starts skip the unpacking until the stamp changes. The PS1 discs have their own
stamps and are not part of the PC game's stamp, so adding them does not reinstall HP1.

**Why CHD**: SwanStation plays it directly (no BIN/CUE or MDF conversion), it is lossless, and it is much smaller than the
raw image, which neither deflate in the APK nor the phone's storage can match:

| Disc | Raw image | Deflate (sampled) | CHD (`chdman createcd`, measured) |
| --- | --- | --- | --- |
| HP1 (MDF as BIN, 2352-byte sectors) | 598 MB | about 79% | **381.7 MB** (64%) |
| HP2 | 674 MB | about 69% | **408.5 MB** (58%) |

Both CHDs pass `chdman verify` (SHA-1) and boot in the emulator with the same pictures as the BIN images. A CHD does not
shrink further in the APK, and the same size is what ends up on the phone (a BIN would need 1.27 GB there).

**Making them**: `chdman createcd -i game.cue -o game.chd` (HP1 first through the MDF converter: the cue and bin it writes,
or `Ps1DiscConverter.convertMdf`). `chdman` is part of MAME; it was taken from the official release
`mame0289b_x64.exe` (github.com/mamedev/mame, SHA-256 `a1aa7912168c9d1b05e611906bc21b8b9be3935822aead36d12a1da363150b7d`
from the release's `SHA256SUMS`), opened with 7-Zip, and only `chdman.exe` was kept. It is not part of the repository.
