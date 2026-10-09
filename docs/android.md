# Flipendo on Android (arm64, touch)

A spike port: SurrealEngine + Flipendo's patches built for Android with SDL3 (window, input, Vulkan surface), played
with an on-screen touch overlay. Tested on a Xiaomi POCO X6 Pro (Mali-G615, Android 16). You need your own copy of the
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

- Starts at the main menu (splash skipped). **Debug mode is on**: the main menu has Level Select.
- For development, put a map name in `/sdcard/FlipendoHP/start_level.txt` (for example `Lev2_Inc_A`) to start that level
  with the story state a player carries in; delete the file to get the menu back.
- Left thumb: floating stick (arrow keys). Right thumb: drag to look. CAST = left mouse, JUMP = right mouse + Space
  (Space also skips cutscenes), MENU = Esc. In menus the finger is the mouse (tap, drag sliders).
- **Spell lessons:** the symbol is drawn 1.6x larger and the wand follows the finger directly
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
- No `backtrace()`, ALSA or GTK/fontconfig on Android: stubs and an Android resource loader (`android/jni/`). Music is
  silent for now (the sound effects use OpenAL).
- `KW::AnimBone` is both a struct and an accessor in namespace `KW`: Clang resolves the name to the function, so the
  struct is spelled `struct AnimBone` where both are in scope.

## Known problems

- `Lev5_FlyKeys` (the broom level) renders black: Harry starts in a zone with no lights and flies a loop on rails.
  Not looked into further; the flying lesson `Lev2_Quid1` is the broom test level.
- The Options page still shows the empty bars of the hidden key-binding buttons (they are part of the page art).
- Combat spell casting is not touch-adapted yet (only the lesson drawing is).
