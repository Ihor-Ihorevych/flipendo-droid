# Harry Potter 3 in Flipendo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Flipendo's launcher gets a third game, Harry Potter and the Prisoner of Azkaban, run by the HP3 engine
(`flipendo-droid-2004`) in its own process, with its data in the self pack or a copied folder, like HP1 and HP2.

**Architecture:** The HP3 engine's native libraries and Java are built by Flipendo's `android/build-apk.sh` from the
sibling engine repository. The engine's SDL2 is renamed to the Java package `org.libsdl2.app` so it coexists with
Flipendo's SDL3. `SetupActivity` grows a third game; for HP3 it prepares `System/UT2004.ini` and starts
`com.ut2004.android.GameActivity` in process `:hp3` with the data root on the intent.

**Tech Stack:** Java 8 source level (javac + d8, no Gradle) for Flipendo; CMake + NDK for both native builds; bash.

**Spec:** `docs/superpowers/specs/2026-10-29-hp3-in-flipendo-design.md` (in `flipendo-droid`). Read it first.

## Global Constraints

- Two repositories: `C:\Users\Ihor\hpdroid\flipendo-droid` (Flipendo, branch as checked out) and
  `C:\Users\Ihor\hpdroid\flipendo-droid-2004` (engine, branch `hp3`). Each task names its repository.
- Never commit game data, `C:\Users\Ihor\hpdroid\hp3-headers` content or decompiled EA code. Never push.
- Engine repo: Conventional Commit subjects, no attribution lines, comments describe the code (its `CLAUDE.md`). The
  stock UT2004 build must keep building (`build-windows64`); every engine change is opt-in or behaviour-neutral for the
  engine's own Gradle app.
- Flipendo: its `CLAUDE.md`; HP1 and HP2 behaviour must not change.
- SDL2 Java package for HP3: `org.libsdl2.app`; JNI prefix `org_libsdl2_app`.
- HP3 data folder `/sdcard/FlipendoHP3`; picked-folder pref `gameDir.hp3`; game pref value `hp3`; recognised by
  `System/hppoa.exe` or `system/hppoa.exe`; self-pack asset folder `hp3data` with `.stamp`.
- HP3 ini keys in `[Engine.Engine]`: `RenderDevice=GL4ESDrv.GL4ESRenderDevice`, `ViewportManager=SDL2Drv.SDL2Client`.
- HP3 activity: `com.ut2004.android.GameActivity`, `android:process=":hp3"`, landscape.
- Intent extras (new, engine): `com.ut2004.android.DATA_ROOT` (String, absolute path),
  `com.ut2004.android.HP3` (boolean).
- Cover: year `2004`, title "Prisoner of Azkaban", colours `AZKABAN = 0xFF2B2766`, `AZKABAN_DEEP = 0xFF0C0B22`, drawing
  a crescent moon.
- Self pack HP3 content: `Animations`, `maps`, `music`, `sounds`, `StaticMeshes`, `textures`, `system` minus
  `*.exe`/`*.dll`; no `Movies`, `help`, `Support` or installer files.
- Phone: Redmi 2311DRK48G, adb serial `IBZXKZIJHABIOFBY`, arm64-v8a; adb at
  `C:/Users/Ihor/AppData/Local/Android/Sdk/platform-tools/adb.exe` (Git Bash: `export MSYS_NO_PATHCONV=1`).

## Review Focus

- **Second HP3 launch in one session** (quit HP3, press Play again): must start a fresh engine, not resume a dead
  one → Task 2's process-exit step and Task 6's acceptance check pin it.
- **A copied HP3 folder with lowercase `system/`** (the retail install's case): must be found and get its ini → Task 3's
  `findGameLowercaseSystem` and `prepareIniLowercaseSystem` tests.
- **An existing `UT2004.ini` from a PC copy** (names `D3DDrv`/`WinDrv`): must be fixed, while a phone ini with the
  player's settings is left alone → Task 3's `prepareIniFixesPcIni` and `prepareIniKeepsPhoneIni` tests.
- **Switching between HP1/HP2 and HP3 repeatedly**: the launcher's other games keep their folders, language and HP2
  start file → Task 4's on-device checklist.
- **Self pack reinstall with HP3 saves present**: unpacking again must not overwrite `Save/` → Task 4 reuses
  `installHp2Data`'s rule (saves stay) through the shared installer; Task 6 checks it.

---

### Task 1: Engine — SDL2 under another Java package (flipendo-droid-2004)

**Files:**
- Modify: `CMakeLists.txt` (the SDL2 FetchContent block, near line 525-545)
- Create: `cmake/SdlJavaPackage.cmake`
- Modify: `docs/hp3/android.md` (one paragraph: the option and why)

**Interfaces:**
- Produces: CMake cache option `UE_SDL2_JAVA_PACKAGE` (string, default empty = SDL2 unchanged). When set to
  `org.libsdl2.app`, `libSDL2.so` exports `Java_org_libsdl2_app_*` and registers its natives for
  `org/libsdl2/app/SDLActivity`, `SDLInputConnection`, `SDLAudioManager`, `SDLControllerManager`, and `HIDDeviceManager`'s
  natives under the same prefix.

- [ ] **Step 1: Write the check (fails now).** A script `tools/android/check_sdl_java_package.sh <libSDL2.so> <package>`
  that runs `llvm-nm -D` and `strings` and exits non-zero unless both
  `Java_org_libsdl2_app_SDLActivity_nativeSetupJNI` and `org/libsdl2/app/SDLActivity` are present and
  `org/libsdl/app/SDLActivity` is absent.
- [ ] **Step 2: Configure an Android build dir without the option and run the check**: expect FAIL (the stock names).
  Configure: `cmake -S . -B build-android-sdlpkg -G Ninja -DCMAKE_TOOLCHAIN_FILE=<NDK 28.2>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFETCHCONTENT_SOURCE_DIR_SDL2=<build-windows-hp3-layout/_deps/sdl2-src>`,
  build target `SDL2`.
- [ ] **Step 3: Implement `cmake/SdlJavaPackage.cmake`**: a function `ue_sdl2_java_package(<sdl target> <package>)` that
  reads SDL2's `src/core/android/SDL_android.c` and `src/hidapi/android/hid.cpp` from the SDL2 source dir, replaces
  `org_libsdl_app` with the package's JNI form and `org/libsdl/app/` with its path form, writes the copies under
  `${CMAKE_BINARY_DIR}/sdl2-java-package/`, adds them to the SDL2 target, marks the originals `HEADER_FILE_ONLY`
  (`set_source_files_properties(... DIRECTORY <SDL2 source dir>)`) and adds each original's directory to SDL2's private
  include dirs (the copies' relative `#include "../../SDL_internal.h"` must resolve). Never write into the SDL2 source
  dir: it is shared with the engine's Gradle build. Call it from `CMakeLists.txt` only when `ANDROID` and
  `UE_SDL2_JAVA_PACKAGE` is set.
- [ ] **Step 4: Reconfigure with `-DUE_SDL2_JAVA_PACKAGE=org.libsdl2.app`, build `SDL2`, run the check**: expect PASS.
- [ ] **Step 5: Regression**: the engine's Gradle HP3 APK (recipe in `docs/hp3/android.md`) still builds and its
  `libSDL2.so` passes the check with package `org.libsdl.app` (unchanged default).
- [ ] **Step 6: Commit** `feat(android): SDL2 can be built under another Java package (UE_SDL2_JAVA_PACKAGE)`.

### Task 2: Engine — GameActivity started by another app (flipendo-droid-2004)

**Files:**
- Modify: `Android/app/src/main/java/com/ut2004/android/GameActivity.java`

**Interfaces:**
- Consumes: nothing.
- Produces: `static final String EXTRA_DATA_ROOT = "com.ut2004.android.DATA_ROOT"`,
  `static final String EXTRA_HP3 = "com.ut2004.android.HP3"`. With `EXTRA_DATA_ROOT` set: that folder is the data
  root, `Storage.installSystemFiles` is not called, and when the activity is destroyed while finishing its process
  ends (`android.os.Process.killProcess(android.os.Process.myPid())`), so the next start gets a fresh engine.
  `isHp3()` is true when `EXTRA_HP3` is true or the package name ends with `.hp3`.

- [ ] **Step 1: Implement** the two extras in `resolveDataRoot()` (first check), `onCreate` (skip
  `installSystemFiles`), `isHp3()`, and an `onDestroy` override (call super, then end the process when `isFinishing()`
  and the activity was started with `EXTRA_DATA_ROOT`). Comments say what the code does.
- [ ] **Step 2: Verify the engine's own app is unchanged**: build its HP3 APK, install, start with
  `adb shell am start -n com.ut2004.android.hp3/com.ut2004.android.MainActivity --es com.ut2004.android.ARGS HP3_Adv1Express`;
  expect the first level as before (logcat `nativePrepareProcess(dataRoot=/storage/emulated/0/UT2004)`).
- [ ] **Step 3: Verify the host path**: `adb shell am start -n com.ut2004.android.hp3/com.ut2004.android.GameActivity --es com.ut2004.android.DATA_ROOT /sdcard/UT2004 --ez com.ut2004.android.HP3 true`
  (temporarily `android:exported="true"` locally if needed, not committed); expect `nativePrepareProcess(dataRoot=/sdcard/UT2004)`
  in logcat and no "installSystemFiles" write of `Default.ini` (its timestamp unchanged).
- [ ] **Step 4: Commit** `feat(android): GameActivity takes its data root and HP3 mode from the intent`.

### Task 3: Flipendo — HP3 folder and ini helpers with tests (flipendo-droid)

**Files:**
- Create: `android/java/io/github/flipendo/spike/Hp3Setup.java` (no Android imports: plain Java so it runs on the desktop)
- Create: `android/tests/Hp3SetupTest.java`
- Create: `android/tests/run.sh` (compiles both with the desktop JDK into `build-android-tests/` and runs the test;
  exit code = result)

**Interfaces:**
- Produces (all `static`, package `io.github.flipendo.spike`, class `Hp3Setup`):
  - `File findGame(File dir, int depth)`: `dir` or a descendant up to `depth` levels that has `System/hppoa.exe` or
    `system/hppoa.exe`; `null` if none. Same walk as `SetupActivity.findGameFor` (skips names starting with `.`).
  - `File systemDir(File game)`: `game/System` if it exists, else `game/system`.
  - `String patchIni(String ini)`: in section `[Engine.Engine]` only, the values of `RenderDevice` and
    `ViewportManager` become the Global Constraints' values; every other line, comment and line ending is kept.
  - `boolean needsPatch(String ini)`: true when `[Engine.Engine]` `RenderDevice` or `ViewportManager` differs from
    those values.
  - `void prepareIni(File game) throws IOException`: if `systemDir(game)/UT2004.ini` is missing, write
    `patchIni(Default.ini text)`; if it exists and `needsPatch`, rewrite it patched; otherwise leave it. Bytes are
    Windows-1252/ISO-8859-1 round-tripped unchanged (read and write as ISO-8859-1).

- [ ] **Step 1: Write the failing tests** in `Hp3SetupTest.main` (plain `java`, a tiny `check(boolean, String)` helper,
  non-zero exit on failure), each on a temp dir:
  - `findGameUpperSystem`: `x/System/hppoa.exe` → `findGame(x,2) == x`.
  - `findGameLowercaseSystem`: `x/hp3/system/hppoa.exe` → `findGame(x,2) == x/hp3`.
  - `findGameNone`: `x/System/HP.exe` only → `null`.
  - `patchIniReplacesDrivers`: input with `[Engine.Engine]\r\nRenderDevice=D3DDrv.D3DRenderDevice\r\nViewportManager=WinDrv.WindowsClient\r\nAudioDevice=ALAudio.ALAudioSubsystem\r\n[Other]\r\nRenderDevice=Keep\r\n`
    → the two Engine keys changed, `AudioDevice` and `[Other] RenderDevice=Keep` kept, `\r\n` kept.
  - `patchIniIdempotent`: `patchIni(patchIni(s)).equals(patchIni(s))`.
  - `prepareIniWritesFromDefault`: `System/Default.ini` present, no `UT2004.ini` → `UT2004.ini` = patched Default.
  - `prepareIniLowercaseSystem`: same with `system/`.
  - `prepareIniFixesPcIni`: existing `UT2004.ini` with `D3DDrv` → patched.
  - `prepareIniKeepsPhoneIni`: existing patched `UT2004.ini` with an extra line `FpsLogSeconds=5` → file unchanged
    (same bytes and timestamp).
- [ ] **Step 2: Run `android/tests/run.sh`**: expect FAIL (`Hp3Setup` missing → compile error).
- [ ] **Step 3: Implement `Hp3Setup`** with the signatures above.
- [ ] **Step 4: Run `android/tests/run.sh`**: expect PASS, all nine named.
- [ ] **Step 5: Commit** `android: HP3 folder detection and phone ini (Hp3Setup) with desktop tests`.

### Task 4: Flipendo — the third game in the launcher (flipendo-droid)

**Files:**
- Modify: `android/java/io/github/flipendo/spike/SetupActivity.java`
- Modify: `android/java/io/github/flipendo/spike/Tr.java` (Russian for the new strings)
- Modify: `android/AndroidManifest.xml`

**Interfaces:**
- Consumes: `Hp3Setup.findGame`, `Hp3Setup.prepareIni` (Task 3); `GameActivity.EXTRA_DATA_ROOT`, `EXTRA_HP3` (Task 2,
  referenced by their string values since the class compiles in the same build only after Task 5; use string
  constants `"com.ut2004.android.DATA_ROOT"`, `"com.ut2004.android.HP3"` in `SetupActivity`).
- Produces: `static final String HP3_DIR = "/sdcard/FlipendoHP3"`, `PREF_GAME_DIR_HP3 = "gameDir.hp3"`,
  `HP3_BUNDLE = "hp3data"`; `String chosenGame()` returning `"hp1" | "hp2" | "hp3"`; `boolean isHp3()`;
  `void installBundle(String bundle, File dir, String label)` (the body of today's `installHp2Data`, generalised;
  `installHp2Data()` becomes `installBundle(HP2_BUNDLE, new File(HP2_DIR), tr("Harry Potter 2"))` with the exact
  same messages as today).

- [ ] **Step 1: Game choice three-way.** `onCreate` accepts `"hp3"` in the `game` extra. Every use of `isHp2()` is
  reviewed: HP1-only paths (language wish, text translation, dubbing, mods, `installGameData`, renderer pills) run only
  for `hp1`; HP2-only paths for `hp2`; HP3 runs none of them. `gameDirKey()`, `locateGame`, the details panel and the
  Play label handle `hp3` (no slots, no renderer choice; Play label as for HP1).
- [ ] **Step 2: The cover.** `coverShelf` adds a third cover; `cover(int game)` / `CoverDrawable(int game, float px)`
  replace the boolean; HP3 uses `AZKABAN`/`AZKABAN_DEEP`, year `2004`, title `tr("Prisoner of Azkaban")`, description
  `tr("Harry Potter and the Prisoner of Azkaban")`, and draws a crescent moon (two arcs, stroke like HP2's serpent,
  colour `0x55D8D4F0`) at the same `cx, cy, s`. HP1 and HP2 covers draw exactly as before. `showGame`/`styleCover`
  light the chosen of three.
- [ ] **Step 3: Data.** For `hp3`: `installBundle(HP3_BUNDLE, new File(HP3_DIR), tr("Harry Potter 3"))`, then
  `locateGame` (pref `gameDir.hp3`, default `HP3_DIR`, detection `Hp3Setup.findGame`), then `Hp3Setup.prepareIni(found)`
  (logged on failure like the HP2 language errors).
- [ ] **Step 4: Start.** In `tryStart`, for `hp3`: no `writeStartFile`; start `com.ut2004.android.GameActivity`
  (by class name with `Intent.setClassName(this, "com.ut2004.android.GameActivity")`) with
  `com.ut2004.android.DATA_ROOT` = the game folder and `com.ut2004.android.HP3` = true, then `finish()` as today.
- [ ] **Step 5: Manifest.** Add the activity: `android:name="com.ut2004.android.GameActivity"`,
  `android:process=":hp3"`, `android:exported="false"`, `android:screenOrientation="sensorLandscape"`, the same
  `configChanges` and theme as `FlipendoActivity`.
- [ ] **Step 6: Tr.** Russian entries for "Prisoner of Azkaban" (Узник Азкабана), "Harry Potter and the Prisoner of
  Azkaban" (Гарри Поттер и узник Азкабана), "Harry Potter 3" (Гарри Поттер 3), in `Tr.java`'s existing format.
- [ ] **Step 7: Verify** `android/build-apk.sh` builds (the engine's classes come with Task 5; `setClassName` needs
  none of them to compile) and `android/tests/run.sh` still passes; on the phone, with the APK from this step, HP1 and
  HP2 behave as before and the HP3 cover appears and lights when chosen (Play for HP3 is verified after Task 5).
- [ ] **Step 8: Commit** `android: Harry Potter 3 as the launcher's third game`.

### Task 5: Flipendo — build-apk.sh builds and packs the HP3 engine (flipendo-droid)

**Files:**
- Modify: `android/build-apk.sh`
- Modify: `docs/android.md` (build requirements and options for HP3)

**Interfaces:**
- Consumes: `UE_SDL2_JAVA_PACKAGE` (Task 1); GameActivity extras (Task 2); `SetupActivity` HP3 code (Task 4).
- Produces: options `--hp3-engine <dir>` (default `../flipendo-droid-2004`), `--hp3-headers <dir>` (default
  `../hp3-headers`), `--hp3-deps <dir>` (default `<engine>/build-windows-hp3-layout/_deps`: `sdl2-src`, `gl4es-src`,
  `metoolkitkarmadecomp-src`), `--hp3 <HP3 folder>` (self pack); build dir `build-android-hp3/`.

- [ ] **Step 1: Native.** After Flipendo's native step: configure (once) and build the engine with NDK 28.2
  (`$ANDROID_HOME/ndk/28.2.13676358`, the engine's) and CMake 4.1.2 (`$ANDROID_HOME/cmake/4.1.2`): arguments
  `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUE_GAME_HP3=ON -DUE_HP3_HEADERS_DIR=<headers> -DUE_SDL2_JAVA_PACKAGE=org.libsdl2.app -DFETCHCONTENT_SOURCE_DIR_SDL2=<deps>/sdl2-src -DFETCHCONTENT_SOURCE_DIR_GL4ES=<deps>/gl4es-src -DFETCHCONTENT_SOURCE_DIR_METOOLKITKARMADECOMP=<deps>/metoolkitkarmadecomp-src`;
  targets `SDLLaunch ut2004_android_bridge`. Fail with a clear message when the engine or header folder is missing.
- [ ] **Step 2: Libraries.** Copy `libUT2004.so`, `libSDL2.so`, `libut2004_android_bridge.so` and the NDK 28.2
  `libc++_shared.so` (arm64) into `lib/arm64-v8a`; Flipendo's `libopenal.so` serves both (the engine binds
  `libopenal.so` by name). Run the Task 1 check script on the copied `libSDL2.so`. If Task 6 check 2 shows HP3's
  audio failing with Flipendo's OpenAL Soft (1.23; the engine's own is 1.25), ship the engine's build of the `OpenAL`
  target as `libopenal-ue.so` and make `AL_DLL` in `Source/ALAudio/Inc/ALAudioSubsystem.h` that name for Android builds
  with `UE_SDL2_JAVA_PACKAGE` set (engine commit `fix(android): …`).
- [ ] **Step 3: Java and assets.** Copy the engine's `GameActivity.java`, `Hp3TouchOverlayView.java`, `Storage.java`
  and `org/libsdl/app/*.java` to `$A/hp3java/`, rewriting `org.libsdl.app` to `org.libsdl2.app` (package, imports,
  qualified names) and placing the SDL files under `org/libsdl2/app/`; add them to the `javac` line. Copy the engine's
  `assets/touch_overlay/hp3/*.png` to `$A/assets/touch_overlay/hp3/`. None of the engine's `assets/system` files.
- [ ] **Step 4: Verify the plain APK.** `android/build-apk.sh`; expect it to build; `unzip -l android/dist/flipendo-droid.apk`
  lists the four HP3 libraries and `assets/touch_overlay/hp3/wand.png`; `"$BT/dexdump" classes.dex | grep -c "Lorg/libsdl2/app/SDLActivity;"`
  ≥ 1 and the SDL3 `Lorg/libsdl/app/SDLActivity;` is still there.
- [ ] **Step 5: Self pack.** `--hp3 <folder>` (validated by `system/hppoa.exe` or `System/hppoa.exe`) stages
  `assets/hp3data` for `flipendo-droid-selfpack.apk` with the Global Constraints' content (case-insensitive folder
  match) and a `.stamp` made like `hp2data`'s; print file count and MB like HP2. Verify:
  `unzip -l` shows `assets/hp3data/system/Core.u` (or the install's case) and no `.exe`/`.dll` and no `Movies/`.
- [ ] **Step 6: Commit** `android: build-apk.sh builds the HP3 engine into Flipendo and packs HP3 (--hp3)`.

### Task 6: On-device acceptance (both repos)

**Files:**
- Modify: `docs/android.md` (Flipendo; what the player sees for HP3, the HP3 folder) if anything learnt differs from Task 5's text.

- [ ] **Step 1: Build** the self pack the owner uses (memory recipe) plus `--hp3 "C:/Games/hp3"`; install over the
  current Flipendo; copy to `C:\Users\Ihor\Desktop\FlipendoHP`.
- [ ] **Step 2: Phone checks**, each recorded pass/fail with logcat evidence:
  1. HP1 and HP2 start and play as before (language, HP2 slots).
  2. HP3 cover → Play: unpack progress once ("Harry Potter 3"), then HP3's front end with music and Flipendo-style
     touch controls; `/sdcard/FlipendoHP3/System/UT2004.ini` has the two driver keys; the engine log shows
     `ALAudio: subsystem initialized`, and music and voices are audible.
  3. New Game reaches the train level; cast Alohomora at the lock, climb a crate, use the skip button in a cutscene.
  4. Quit HP3 from its menu → launcher; press Play again → HP3 starts fresh (no crash, new `:hp3` pid).
  5. A save made in HP3 survives a reinstall of the self pack (`/sdcard/FlipendoHP3/Save` untouched).
  6. Plain APK: with `/sdcard/FlipendoHP3` present, HP3 is found and starts.
- [ ] **Step 3: Commit** any doc fix: `docs: HP3 in the Android guide`.
