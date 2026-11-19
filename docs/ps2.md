# PS2 on Android: Harry Potter and the Chamber of Secrets, recompiled on the phone

The PS2 tab runs the PlayStation 2 version of HP2 (PAL, SLES-51192) from the player's own disc. The APK carries **no game code**: the
runner translates the game's program to C++ and compiles it on the phone, once, then plays it.

## How it works

* **Runner**: `libps2main.so`, built from [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp) (MIT, a static
  recompiler of the PS2's main processor plus the rest of the console rebuilt in C++; `docs/` there explains it). It is built with no
  game linked in (`host/empty_game`), so everything it runs comes from the disc.
* **SDL**: the runner uses SDL2's API; the app has SDL3, so `libSDL2ps2.so` is sdl2-compat (zlib) over the app's `libSDL3.so`. It is
  renamed because the HP3 engine brings a real `libSDL2.so` of its own.
* **Preparing the game** (the first time, `Ps2Activity` with `ps2.prepare`): `libps2main.so --prebuild` reads the boot program from the
  disc, translates all of its code (about 1.05 million instructions for HP2) into 260 C++ files, compiles them with the phone's own clang
  (`libps2clang.so` + `libps2lld.so`, built by `android/ps2/build-toolchain.sh`, with the NDK's sysroot unpacked from
  `assets/ps2/sysroot.zip`) and links them into one library in `files/ps2/<serial>/ee-live`. A progress bar shows the compiled files
  (`files/ps2/<serial>/prebuild.progress`). On a 12-core PC this takes about 50 seconds.
* **Playing**: the runner loads that library like the live libraries a window run makes, and the game never stops for code of its own
  program. Vector-unit programs the game uploads are compiled in the background (the same compiler) and interpreted until they are ready.
* **Discs**: an `.iso`, a raw `.bin` (2352-byte sectors, mode 1 or 2) with its `.cue` (a sheet whose file name does not match the BIN is
  fine: the one image beside it is used), or a folder with the disc's files (`SYSTEM.CNF` in it; the file system is made in memory).
  A folder cannot serve a game that has sector numbers of the real disc in its code. Nothing is converted: a 650 MB BIN is read in place.

## Files

| What | Where |
|---|---|
| Disc | `/sdcard/FlipendoPS2/HP2` (an image or the unpacked files), or picked in the tab |
| Saves (memory card `mc.ps2`) | `/sdcard/FlipendoPS2/HP2/saves` |
| Translated game, logs | `files/ps2/SLES-51192/` (`ee-live`, `vu1-live`, `game/logs`) |
| Extra switches | `files/ps2/SLES-51192/game/env.txt`, one `KEY=VALUE` per line (`PS2_RENDERER=exact`, `PS2_GPU_BACKEND=vulkan`, `PS2_SPEED_LOG=1` ...) |

## Building

```sh
. /c/Users/Ihor/CFR2Recomp/env.sh                       # llvm-mingw, CMake, Ninja on the PATH
bash android/ps2/build-toolchain.sh                      # once: clang + lld for the phone, the sysroot (a long build)
PS2RECOMP=/c/Users/Ihor/CFR2Recomp/ps2-native-recomp bash android/ps2/build-ps2.sh
android/build-apk.sh --install                           # takes android/deps/ps2out; without it the APK has no PS2 support
```

## Tests

* `android/tests/run.sh` (Ps2SetupTest: serial from SYSTEM.CNF of ISO and raw BIN images, CUE sheets, disc folders, which disc to use).
* In ps2-native-recomp: `ninja -C build/ci ps2discinputtest && build/ci/ps2discinputtest` (raw BIN/CUE and plain-folder discs), and
  `ps2run --disc game.cue --prebuild` with `PS2_EE_LIVE=<folder> PS2_LIVE_CXX=<clang++>` on a PC.

## State

See the plan and the notes in the memory of the project. Known open point: the opening film of HP2 stops the game on the PC build
(the film loop waits for the image decoder's DMA); it is being fixed in the runtime, which every game profits from.
