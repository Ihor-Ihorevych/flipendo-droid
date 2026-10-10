# Flipendo on Android (arm64, touch)

A spike port: SurrealEngine + Flipendo's patches built for Android with SDL3 (window, input, Vulkan surface), played
with an on-screen touch overlay. Tested on a Xiaomi POCO X6 Pro (Mali-G615), Redmi Note 13 Pro, Redmi 13C (Mali-G52, no bindless) and Samsung Galaxy S23 and S25. You need your own copy of the
game: nothing from it is in the APK.

## Install

1. **Game data.** Copy the installed game folder (the one with `System/`, `Maps/`, `Textures/`, `Sounds/`, `Music/`,
   ...) to the phone as `/sdcard/FlipendoHP/`. With the phone connected over USB debugging:
   `adb push "C:\Games\HP\." /sdcard/FlipendoHP/` (the US 1.0 disc version and the No-CD exe are recognised).
2. **APK.** `adb install -r android/dist/flipendo-droid.apk`, or copy the file to the phone and open it (allow
   "install unknown apps" for the file manager). On Xiaomi, turn on *Developer options -> Install via USB* for `adb`.
3. **Storage permission.** The first launch opens the *All files access* page: switch it on for Flipendo, then start
   the app again. (`adb shell appops set io.github.flipendo.spike MANAGE_EXTERNAL_STORAGE allow` does the same.)

The engine's UI resources (`SurrealEngine.pk3`) are inside the APK and are copied to `/sdcard/FlipendoHP/.surrealengine/`
at launch. The game's own saves and ini files are written into `/sdcard/FlipendoHP/` too; the engine log is
`/sdcard/FlipendoHP/flipendo.log`.

## Build

`android/build-apk.sh` (add `--install` to also install it). Needs JDK 21, Android SDK 35 with build-tools 35.0.0,
NDK 27.2.12479018 and CMake 3.22.1; set `JAVA_HOME` / `ANDROID_HOME` if they are not in the default places. It
fetches SDL3 (pinned release) once, applies `src/surreal-patches/` (including `0200-android-port.patch`), builds
`libmain.so`, and assembles and debug-signs the APK without Gradle. Output: `android/dist/flipendo-droid.apk`.

## Playing

- Starts at the main menu (splash skipped). Debug mode is off by default (Settings > Debug mode turns on Level Select in the main menu).
- For development, put a map name in `/sdcard/FlipendoHP/start_level.txt` (for example `Lev2_Inc_A`) to start that level
  with the story state a player carries in; delete the file to get the menu back.
- Left thumb: floating stick (arrow keys). Right thumb: drag to look. CAST = left mouse, JUMP = right mouse + Space
  (Space also skips cutscenes), MENU = Esc. In menus the finger is the mouse (tap, drag sliders).
- **Spell lessons:** the symbol is drawn 1.3x larger and the wand follows the finger directly
  (`src/hp1/HP1Touch.cpp`, `src/knowwonder/KWGesture.cpp`). Auto Jump is on and the Options page's Controls column is hidden
  (the bindings can't be changed on a phone).
- Rendering is at half the screen resolution (`SURFACE_SCALE` in `android/java/.../FlipendoActivity.java`) for ~60 fps on
  a 2712x1220 panel; the game's 60 fps cap stays.

## What the port had to change (the why of `0200-android-port.patch`)

- Mali drivers crash compiling `gl_ClipDistance` (the scene vertex shader's near clip): skipped on Android.
- No BC1-3 (S3TC) on mobile GPUs: those textures are decoded to RGBA8 on the CPU when uploaded.
- Mali keeps command-stream memory until the command pool is reset: the pool is reset every frame (without it GPU memory
  grew about 4.5 MB/s and the driver ended with "out of host memory" after ~3 minutes).
- The swapchain uses the identity pre-transform and lets the compositor rotate.
- The engine only uses the absolute (Windows) mouse position in windowed mode; the touch build always does, otherwise
  the HP menus got mouse deltas and the cursor drifted.
- `DrawText` with `Canvas.Font == None` (debug mode's HUD) crashed in `UFont::GetGlyph`: it returns now.
- No `backtrace()`, ALSA or GTK/fontconfig on Android: stubs and an Android resource loader (`android/jni/`).
- `KW::AnimBone` is both a struct and an accessor in namespace `KW`: Clang resolves the name to the function, so the
  struct is spelled `struct AnimBone` where both are in scope.

## Known problems

- The Options page still shows the empty bars of the hidden key-binding buttons (they are part of the page art).
- Combat spell casting is not touch-adapted yet (only the lesson drawing is).

## Logs: what a user sends when it breaks

- **If the game stops with an error or the last run crashed**, the next start of the app shows *The game stopped...* with
  **Send logs** and **Play anyway**. **Send logs** opens the share sheet with `flipendo-report.zip` (a copy is also left
  in `Download/`).
- **If the screen is black or the game hangs**, hold the on-screen **MENU** button for 1.5 s: the same share sheet opens.
- **What is in the report:** `device-info.txt` (model, SoC, Android, RAM, screen, Vulkan/GLES features, build), `exit-info.txt`
  (why the last runs ended, Android 11+; a native crash also brings Android's own `tombstone-*.pb`), `flipendo.log` and
  `flipendo.prev.log` (the engine log of this and the previous run; the `Vulkan device` lines name the GPU),
  `crash.txt` / `crash.prev.txt` (the backtrace of a fatal signal), `game-folder.txt` (what is in `/sdcard/FlipendoHP`, and
  whether `System/HP.exe` and the key packages are there), `logcat.txt` (this app's system log).
- **Reading `crash.txt`:** frames are `module + offset`. `android/build-apk.sh` keeps the matching unstripped library in
  `android/dist/symbols/libmain.unstripped.so` (with `build-info.txt`, the commit); keep it per release. Then:
  `llvm-addr2line -f -C -e libmain.unstripped.so 0x<offset>`.
- By hand, without the app: `adb logcat -d > logcat.txt` and the files in `/sdcard/FlipendoHP/` (`flipendo.log`, `crash.txt`).

## Two builds: plain and self pack

`android/build-apk.sh` makes `android/dist/flipendo-droid.apk` (the port only, ~8 MB: copy the game to the phone by hand,
see Install). `android/build-apk.sh --selfpack "C:/Games/HP"` also makes `android/dist/flipendo-droid-selfpack.apk`
(~270 MB): the same APK plus the game from that folder, unpacked into `/sdcard/FlipendoHP/` on the first launch
(`SetupActivity`, with a progress bar; files that exist are never overwritten, saves and ini files are left alone, the
SafeDisc files, uninstaller, logs and saves are not packed). Both start through `SetupActivity`, which also asks for
*All files access*.

**The self pack contains the game's copyrighted data: build it for your own phone only, and never publish or share it.**

## GPUs without bindless textures (older Mali)

The renderer was written for bindless textures (an unbounded `sampler2D textures[]` indexed per vertex). A GPU that lacks
descriptor indexing (the Mali-G52 of a Redmi 13C did) now takes a second path: `VulkanRenderDevice::Bindless` is false, the
scene fragment shader is compiled with `NO_BINDLESS` (four fixed samplers: texture, macro, detail, lightmap), a change of
textures ends the draw batch (`SetFixedTextures`), and `DescriptorSetManager` makes a descriptor set per distinct texture
quadruple each frame (cached, the pool is reset once the GPU is done). The log line `Bindless textures: yes|no (...)` says
which path a phone took. Creating `/sdcard/FlipendoHP/no_bindless.txt` forces the slow path for testing.

Device selection (`VulkanDeviceBuilder::FindDevices`) only insists on `independentBlend` (the renderer never draws indirectly,
never writes storage or atomics from a fragment shader and creates samplers without anisotropy), accepts the Vulkan 1.2
core versions of descriptor indexing and mirror clamp when the extension is not listed, and when no device fits, the fatal
message names each GPU and what it lacks.

## First-run problems

- **Game data not found:** the app checks for `System/HP.exe` in `/sdcard/FlipendoHP`, and one or two folders below it (a
  copy made one folder too deep still works), before it starts the engine; if the game is missing it says where to copy
  it, with **Check again** and **Send logs**.
- **Black or stuck game:** hold the on-screen MENU button for 1.5 s to send the logs.

## Going to the background and coming back

Android destroys the window when the app is minimised, which kills the Vulkan surface: the next `vkAcquireNextImageKHR`
returns `VK_ERROR_SURFACE_LOST_KHR`, which the renderer used to treat as a fatal error (the game closed and the state was
lost). `VulkanSwapChain` now tells a lost surface from an out-of-date swapchain; `CommandBufferManager` then releases the
swapchain, `VulkanRenderDevice::RecreateSurface` makes a new `VkSurfaceKHR` from the window and the swapchain is rebuilt
(the log line is `Vulkan surface recreated`). SDL holds the game thread in its event pump while the app is paused, so
nothing runs in the background; the OpenAL device is paused too (`android_main.cpp`, `ALC_SOFT_pause_device`).

What this does not do: if Android kills the process in the background (the game takes about 0.6-0.9 GB of a phone's RAM),
the state is gone, because HP1 only saves at save books and level starts.

## Button icons

The touch buttons are the PNGs in `images/android/` (`wand.png` = cast, `jump.png`, `menu.png` = Esc). `android/build-apk.sh`
copies them into the APK (`assets/icons/`) and the overlay draws them (`FlipendoActivity.java`, `Overlay.loadIcon`): the
black glyphs are tinted white (yellow while pressed) on a dark disc, the coloured wand sits on a light disc. To change the
look, replace the PNGs (square, transparent background) and rebuild; a missing icon falls back to the old text button.

(`aapt2 link -A` writes assets with backslashes in their names on Windows, which Android can't open, so the script adds
`assets/` with `jar` instead: this is also why the self pack's game data was not found until this was fixed.)

## Saving anywhere (the SAVE button)

The button under MENU calls the game's own save, `HPConsole.doLevelSave(slot)` (what the save books call): it writes
`save/Save<N>.usa` and `save/GameSaveInfo<N>` (beans, house points, level name), which the slot list in Start Game / Load Game is
built from. It runs on the game thread (`HP1::TickSaveRequest`, `src/hp1/HP1Touch.cpp`) and a toast says which slot. The slot is
the one New Game / Load Game picked (`FESlotPage.nSelectedSlot`, 0-5); any other value (none, or whatever a Level Select start
leaves there) takes the first slot without a `GameSaveInfo`, and that becomes the game's slot. The log line is
`SAVE button: FESlotPage.nSelectedSlot = N, saving to slot M`.

A save made in the middle of a cutscene is saved as it is: loading it resumes the scene. Put a `save.png` in `images/android/` for an icon.

Why Start Game showed no saves before: the slot data is written as UTF-16 (`SaveGameSaveInfo`), and `to_utf16` / `from_utf16`
were stubs on non-Windows (`Script error: to_utf16 not implemented on unix` in the log), so `GameSaveInfo<N>` was never written. They
are implemented now (`SurrealEngine/Utils/UTF16.cpp`, in `0200-android-port.patch`).

## Settings panel

The gear button (below SAVE, visible in game and in menus) opens a settings panel. Everything in it is remembered between launches (the `flipendo` shared preferences).

- **Movement controls**: *Floating stick* (the default: touch and drag on the left half) or *Arrow buttons* (four arrows always visible at the bottom left; the whole screen then looks around).
- **Render scale** (30%..100%, default 50%): the game is drawn at this fraction of the screen resolution and the compositor scales it up. Lower is faster on weak GPUs. Applied when the finger lifts (the surface is resized then).
- **Subtitles and HUD size** (60%..180%, default 100%): scales the 2D UI (subtitles, HUD). Changes live while dragging; the menu book keeps its size.

## Languages (self pack with several editions)

A self pack can carry more than one edition of the game, for example English and the Russian release with the Fargus voice-over:

```sh
android/build-apk.sh --selfpack "C:/Games/HP" --lang ru="C:/Program Files/HPFarg"
```

The script compares the installs and ships the files they share once, and the files that differ (voices, fonts, texts) once per language (`assets/langs/<code>/`, listed in `assets/langs/langs.txt`). The base install is `en`. On first launch the base language is installed; the settings panel then shows a **Language** button. Tapping it asks for a confirmation (the game is restarted, so unsaved progress is lost); the second tap writes the wish to `/sdcard/FlipendoHP/.selfpack-want`, starts the launcher (`SetupActivity`, which runs in its own process `:setup`), and kills the game's process. The launcher copies the chosen language's files over the game folder (`.selfpack-lang` records which one is installed) and starts the game again in a fresh process, which is what makes the restart clean (the engine can't be started twice in one process).

Unpacking replaces files that are already in the game folder when the APK's data changes (a stamp over the contents decides), except everything under `save/`.

## Movement mod (AdamJD)

A community mod that improves HP1's movement, camera and climbing and lets Harry cast while moving. It is a set of replacement script packages (`HPBase.u`, `HarryPotter.u`, `Hub2-5.u`, `Tut1.u`), i.e. modified game scripts: they are **never committed** (`.gitignore` has `/movement*.zip` and `*.u`), and an APK that carries them is for your own phone only, like the game data itself. The mod's author is AdamJD; get the zip from the mod's own page and build with:

```sh
android/build-apk.sh --selfpack "C:/Games/HP" --mod movement=movement.zip
```

The packages go into the self pack as `assets/mods/movement/`. The settings panel then shows **Movement mod (AdamJD): ON/OFF**. A tap asks for a confirmation (the game restarts, unsaved progress is lost); the second tap writes `.selfpack-mod-want` and restarts through the launcher the same way the language switch does. The launcher copies the mod's packages over the originals in `System/` after saving the originals to `/sdcard/FlipendoHP/.mod-backup/movement/`, and copies them back when the mod is switched off (`.selfpack-mod` records which is installed). New game data from an updated APK brings the originals back and the mod is put in again. Saves made with one set of packages may not load with the other.

With the mod on, **the wand button is a stick**: holding it holds the cast (in the mod that is aiming, and the camera can move), and dragging the finger away from where it went down turns the camera in proportion to the offset; the button follows the finger and a ring shows its range. The button sits higher so the ring clears JUMP. Button clicks (JUMP, CAST) are sent without a cursor position: an absolute position at the screen centre made the mod's camera jump.

Devil's Snare (`Lev5_Snare`) fixes the camera and aims with its own cursor; the mod aims along the camera, so there nothing can be aimed at. With the mod on, entering such a level (`FIXED_CAMERA_LEVELS` in `FlipendoActivity`) shows a prompt: **Switch off and restart** saves the game (the SAVE button's save), switches the mod off, restarts, and the game loads that save by itself (`.selfpack-autoload`, passed to the engine as `--autoload=<slot>`, `HP1::TickAutoLoad`); **Keep the mod** closes it for that level.
