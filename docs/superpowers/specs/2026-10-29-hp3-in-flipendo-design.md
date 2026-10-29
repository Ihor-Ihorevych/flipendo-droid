# Harry Potter 3 in Flipendo — design

## Intent

Flipendo is the owner's Android app for Harry Potter 1 and 2: one launcher (`SetupActivity`) with a cover per game, a
self pack that carries the game data, and Flipendo's touch controls. Harry Potter and the Prisoner of Azkaban (HP3) now
runs on Android through a separate engine (the UT2004/Unreal Engine 2.5 port in `flipendo-droid-2004`, branch `hp3`),
packaged as its own app (`com.ut2004.android.hp3`) with UT2004's launcher. The owner does not want that app or its
launcher: HP3 becomes the third game inside Flipendo, handled "like HP1/HP2, everything for the 3rd game".

## Goals

- The launcher shows a third cover, **Prisoner of Azkaban (2004)**, beside Philosopher's Stone and Chamber of Secrets,
  styled like them with its own colour. Choosing it makes HP3 the launcher's game (the `game` preference becomes `hp3`).
- **Play runs the game directly** (owner's words: "skip the UT2004 menu, run the game"): no UT2004 launcher screen, no
  Start button, storage choice or countdown. The first thing on screen is HP3 itself — its own front end (New Game,
  Load, Options), exactly as the PC game starts. The launcher shows no HP3 save slots (owner agreed: HP3's own menu
  handles saves).
- **Game data, the HP2 way**:
  - `android/build-apk.sh --hp3 "<HP3 folder>"` puts HP3's data in the self pack (`assets/hp3data`, with a `.stamp` like
    `hp2data`); the launcher unpacks it once into `/sdcard/FlipendoHP3`.
  - Without the self pack, the launcher finds an HP3 folder the player copied, recognised by `System/hppoa.exe`, and
    remembers it (`gameDir.hp3`), the way it finds `HP.exe` and `Game.exe` today.
- **HP3 runs in its own activity and process** (`:hp3`), on the HP3 engine's SDL2, with the touch controls already built
  for it (`Hp3TouchOverlayView`: stick, wand, jump, map/skip, menu). Leaving the game returns to Flipendo's launcher.
- **One APK.** The plain and self-pack APKs that `build-apk.sh` makes both carry the HP3 engine; the separate
  `com.ut2004.android.hp3` app is no longer needed.

## Non-goals

- HP3 language or edition switching (the Russian retail install is the only HP3 data for now).
- HP3 save slots or other HP3 settings in the launcher.
- Porting the HP3 engine to SDL3 (one shared SDL). Possible later if two SDLs prove a problem.
- Bink movies (HP3's `Movies/` intro videos); the engine does not play them yet.
- Stock UT2004 inside Flipendo.
- Removing the engine repository's own Gradle app or its HP3 option: it stays for engine development.

## Anti-goals (failure even if HP3 runs)

- HP1 or HP2 behave differently: their launch, data install, settings, language switch or self pack.
- Game data, HP3-derived generated headers (`C:\Users\Ihor\hpdroid\hp3-headers`) or decompiled EA code committed to
  either repository.
- The two SDLs clash at runtime (wrong `SDLActivity`, crashes on start of either engine).

## Constraints and facts

- **Flipendo** (`flipendo-droid`): built by `android/build-apk.sh` without Gradle (CMake + NDK 27.2 for the native
  part, `javac` + `d8`, `aapt2`, `zipalign`, `apksigner`); arm64-v8a only; `minSdk 29`, `targetSdk 35`; package
  `io.github.flipendo.spike`. Libraries: `libmain.so`, `libSDL3.so`, `libopenal.so`, `libSurrealVideo.so`. Java:
  `SetupActivity` (launcher, process `:setup`), `FlipendoActivity` (extends SDL3's `org.libsdl.app.SDLActivity`, main
  process). Game choice: `setup` preference `game` = `hp1` | `hp2`; data folders `/sdcard/FlipendoHP`,
  `/sdcard/FlipendoHP2`; HP2 self-pack data `assets/hp2data`, unpacked by `SetupActivity.installHp2Data`.
- **HP3 engine** (`flipendo-droid-2004`, branch `hp3`): built today by Gradle (`Android/`, `assembleDebug` with
  `-Php3HeadersDir=… -Php3Abis=arm64-v8a -Psdl2Dir -Pgl4esDir -PkarmaDir`, recipe in `docs/hp3/android.md`), NDK 28.2,
  native build type RelWithDebInfo. Its APK holds `libUT2004.so`, `libSDL2.so`, `libopenal.so`,
  `libut2004_android_bridge.so`, `libc++_shared.so`; Java `com.ut2004.android.GameActivity` (extends SDL2's
  `org.libsdl.app.SDLActivity`), `Hp3TouchOverlayView`, `Storage`, SDL2's Java; assets `system/Default.ini`,
  `system/DefUser.ini` (UT2004's) and `touch_overlay/hp3/*.png`. `GameActivity` takes the data root and engine
  arguments from its intent (`com.ut2004.android.ARGS`); the bridge (`nativePrepareProcess`) sets the engine's base
  directory to `<data root>/System`. JNI names are `Java_com_ut2004_android_GameActivity_*`.
- **SDL clash**: both SDLs ship Java classes `org.libsdl.app.*`. SDL2 2.32 can take another JNI prefix
  (`SDL_JAVA_PREFIX`), but `SDL_android.c` also registers its natives under four hard-coded class names
  (`org/libsdl/app/SDLActivity`, `SDLInputConnection`, `SDLAudioManager`, `SDLControllerManager`, lines ~527-530).
- **Library name clash**: both engines ship a `libopenal.so` (both OpenAL Soft); the HP3 engine's `ALAudio` binds it by
  that name.
- **HP3's ini**: the engine reads `System/UT2004.ini` and `System/User.ini`. HP3's own `System/Default.ini` already
  names `AudioDevice=ALAudio.ALAudioSubsystem`; on the phone only two keys of `[Engine.Engine]` differ from it:
  `RenderDevice=GL4ESDrv.GL4ESRenderDevice` (instead of `D3DDrv.D3DRenderDevice`) and
  `ViewportManager=SDL2Drv.SDL2Client` (instead of `WinDrv.WindowsClient`); the engine writes everything else
  (`[SDL2Drv.SDL2Client]`, `FirstRun`) itself. So far a hand-made ini was pushed by adb.
- **UT2004's system files overwrite HP3's**: the engine app's `GameActivity.onCreate` calls
  `Storage.installSystemFiles`, which copies the app's `assets/system/Default.ini` and `DefUser.ini` (UT2004's) into
  `<data root>/System` on every start. On the test phone this already replaced HP3's `Default.ini`. For HP3 this copy
  must not happen.
- **The launcher's game choice is two-way today**: `SetupActivity.isHp2()` tests `"hp2"`, so every other value is
  treated as HP1 throughout the launcher (folder lookup, details panel, Play, language and HP2 start file). A third
  value has to be handled at each of those places.
- HP3's PC install (`C:\Games\hp3`, about 840 MB) also holds installer files (`ISSkin.dll`, `botva2.dll`,
  `innocallback.dll`, `unins000.*`, `*.png`/`*.jpg` installer art, `Support/`, `help/`) and Windows binaries
  (`System/*.exe`, `System/*.dll`) that the phone does not use.
- The repo rules of both repositories hold (conventional commits, comments describe the code, no game assets in git).

## Decided by the owner

- HP3 is added the same way as HP1/HP2: cover, self pack, folder detection, Play.
- Play goes straight into HP3 (its own front end), never through a UT2004 menu.
- No HP3 slots or language switching in the launcher.

## Decided by me (the owner can override)

- **Two SDLs, kept apart by package**: the HP3 engine's SDL2 is built with its Java package renamed to
  `org.libsdl2.app` (`SDL_JAVA_PREFIX=org_libsdl2_app` plus the four `register_methods` class names), and its Java
  sources moved to that package. `GameActivity` and the other `com.ut2004.android` classes keep their package, so the
  engine's own JNI names do not change.
- **Separate process** `:hp3` for HP3's activity: the two engines never share a process, and quitting HP3 ends its
  process and returns to the launcher (still alive in `:setup`).
- **Data root** `/sdcard/FlipendoHP3` (or the picked folder) passed to `GameActivity` by the launcher; HP3's
  `GameActivity` runs without UT2004's `MainActivity` and storage choice.
- **HP3 ini**: the launcher (or `GameActivity` for HP3) writes `System/UT2004.ini` once, as HP3's `System/Default.ini`
  with the two driver keys above replaced, and never overwrites an existing one. A `UT2004.ini` that still names
  `D3DDrv` or `WinDrv` (an ini copied from a PC) gets the two keys fixed. `GameActivity` started from Flipendo skips
  `Storage.installSystemFiles`, and Flipendo's APK carries none of UT2004's `assets/system` files.
- **Leaving HP3**: when the engine exits (HP3's Quit), `GameActivity` finishes and its `:hp3` process ends; the launcher
  in `:setup` is what the player sees next. The back button keeps the HP3 overlay's existing behaviour (the menu).
- **Self pack content**: `Animations`, `maps`, `music`, `sounds`, `StaticMeshes`, `system` (without `*.exe`/`*.dll`),
  `textures`; installer files, `Support`, `help` and `Movies` stay out.
- **HP3 cover colour**: a deep blue/purple pair (Azkaban night), in the same drawing code as the other two.

## Left to the builder

- How `build-apk.sh` obtains the HP3 engine's native libraries (calling the engine's CMake with the same arguments the
  Gradle build passes, or the Gradle build itself, then copying the `.so` files) and its Java (compiling the engine
  repo's sources with Flipendo's `javac`/`d8`), and how the engine location and the HP3 header folder are given
  (options or environment, defaulting to the sibling `../flipendo-droid-2004` and `../hp3-headers`). The NDK each side
  uses may differ; both libraries sets must load on the owner's phone.
- The `libopenal.so` clash: load Flipendo's OpenAL Soft in the HP3 process if it works, otherwise ship the HP3 engine's
  copy under another name and have `ALAudio` bind that name on Android.
- Where in `SetupActivity` the HP3 branch lives (the code is organised around `isHp2()`; a three-way game choice is
  expected), and the HP3 cover's exact look within the existing style.
- Whether the HP3 data needs the folder-case renames done by hand on the test phone (`Maps`, `Sounds`, `Textures`,
  `Music`); the engine's Unix file manager looks names up case-insensitively, so probably not.

## Done when

On the owner's phone, from a fresh install of the self pack built with `--selfpack … --hp2 … --hp3 "C:/Games/hp3"`:
HP1 and HP2 start and play as before; choosing the HP3 cover and Play unpacks HP3 once, then shows HP3's front end with
music and touch controls; New Game reaches the train level; casting, climbing, saving and loading work as in the
current HP3 app; quitting HP3 returns to the launcher; the plain APK finds a copied HP3 folder. The
`com.ut2004.android.hp3` app is no longer needed for any of it.
