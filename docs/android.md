# Flipendo on Android (arm64, touch)

Plays both of KnowWonder's games, Harry Potter 1 (Philosopher's Stone) and Harry Potter 2 (Chamber of Secrets), from one app: the
start screen (`SetupActivity`) has a **HP1 | HP2** switch, and each game has its own folder, saves and folder picker. HP2 is
described in [Harry Potter 2](#harry-potter-2-030---032) below. A port of SurrealEngine + Flipendo's patches built for Android with SDL3 (window, input, Vulkan surface), played
with an on-screen touch overlay. Tested on a Xiaomi POCO X6 Pro (Mali-G615), Redmi Note 13 Pro, Redmi 13C (Mali-G52, no bindless) and Samsung Galaxy S23 and S25. You need your own copy of the
game: nothing from it is in the APK.

## Install

1. **Game data.** Copy the installed game folder (the one with `System/`, `Maps/`, `Textures/`, `Sounds/`, `Music/`,
   ...) to the phone as `/sdcard/FlipendoHP/` (HP1) or `/sdcard/FlipendoHP2/` (HP2, recognised by `System/Game.exe`). With the phone connected over USB debugging:
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
- HP2: the light of Lumos is not exactly on the wand's tip, some translucent walls come out red, `MUTESOUNDS` does not mute, the `Loop`
  flag of `PlaySound` is ignored, and spawned persistent actors (jelly beans) are not carried to the next level.
- HP1 was not played through again on a device after the shared changes of 0.3.0 (script VM, travel, canvas, audio, input).

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
message names each GPU and what it lacks. The descriptor indexing extension itself is optional: a Vulkan 1.1 driver without it
(the Mali-G52 MC2 of a Redmi 13, Helio G88) gets the path without bindless textures instead of being rejected.

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

Since 0.4.2B the mod is on by default and the Settings row that switched it is gone (the Devil's Snare prompt still turns it off once).

A community mod that improves HP1's movement, camera and climbing and lets Harry cast while moving. It is a set of replacement script packages (`HPBase.u`, `HarryPotter.u`, `Hub2-5.u`, `Tut1.u`), i.e. modified game scripts: they are **never committed** (`.gitignore` has `/movement*.zip` and `*.u`), and an APK that carries them is for your own phone only, like the game data itself. The mod's author is AdamJD; get the zip from the mod's own page and build with:

```sh
android/build-apk.sh --selfpack "C:/Games/HP" --mod movement=movement.zip
```

The packages go into the self pack as `assets/mods/movement/`. The settings panel then shows **Movement mod (AdamJD): ON/OFF**. A tap asks for a confirmation (the game restarts, unsaved progress is lost); the second tap writes `.selfpack-mod-want` and restarts through the launcher the same way the language switch does. The launcher copies the mod's packages over the originals in `System/` after saving the originals to `/sdcard/FlipendoHP/.mod-backup/movement/`, and copies them back when the mod is switched off (`.selfpack-mod` records which is installed). New game data from an updated APK brings the originals back and the mod is put in again. Saves made with one set of packages may not load with the other.

**Every APK can use the mod from a file.** The plain APK (the one on GitHub) never carries the packages: they are EA's scripts changed, so shipping them would mean sharing game data. Instead, without a mod in the APK the Movement mod row says **Choose file**: two taps open the system file picker, the player picks the mod's zip (or its `.u` files, several at once), and `SetupActivity.importMod` keeps the `.u` files in the app's private files (`<files>/mods/movement/`, `label.txt`). A package the game doesn't have (no file of that name in `System/`) is turned down, so a wrong zip changes nothing. Then the game restarts with the mod through the same wish file and launcher path as above; from then on the row is the usual ON/OFF switch. The launcher applies the mod in the game folder it found or the player picked (backups and `.selfpack-mod` live there) for every build, and if the game folder was copied over again, a size check notices the originals are back and puts the mod in again.

With the mod on, **the wand button is a stick**: holding it holds the cast (in the mod that is aiming, and the camera can move), and dragging the finger away from where it went down turns the camera in proportion to the offset; the button follows the finger and a ring shows its range. The button sits higher so the ring clears JUMP. Button clicks (JUMP, CAST) are sent without a cursor position: an absolute position at the screen centre made the mod's camera jump.

Devil's Snare (`Lev5_Snare`) fixes the camera and aims with its own cursor; the mod aims along the camera, so there nothing can be aimed at. With the mod on, entering such a level (`FIXED_CAMERA_LEVELS` in `FlipendoActivity`) shows a prompt: **Switch off and restart** saves the game (the SAVE button's save), switches the mod off, restarts, and the game loads that save by itself (`.selfpack-autoload`, passed to the engine as `--autoload=<slot>`, `HP1::TickAutoLoad`); **Keep the mod** closes it for that level.

## Look of the settings and the controls

The settings panel is a card with three pages (tabs under the header): **Display** (render scale, subtitles and HUD size, field of view, FPS limit | First person, FPS counter), **Controls** (movement style, Auto jump, Invert wand, Control layout | **No cutscene skip**, **No quick save**) and **Game** (Language, Movement mod | Debug mode), with the Telegram channel, Renderer and Done at the bottom of every page. *No cutscene skip* hides the SKIP button and ignores hold-Y and Space during a cutscene; *No quick save* hides the save button (the book) and the pad's Y save, leaving the game's own save points (`noSkip`, `noQuickSave` in the `flipendo` preferences). The on-screen controls use the same colours: the settings, save (or SKIP in cutscenes) and menu buttons sit in one toolbar at the top right (30% visible), the arrows on one round pad, the wand and jump on dark discs that turn gold while pressed.

The FPS counter (off by default) shows the frames the engine drew over the last half second next to the toolbar: the engine counts one per frame (`HP1::TickTouchDefaults`, `g_frameCount`) and the overlay reads it (`nativeFrameCount`).

## Gamepad (Xbox layout)

A gamepad works without setup: its events are taken in `FlipendoActivity.dispatchKeyEvent` / `dispatchGenericMotionEvent` before SDL (the engine has no gamepad input) and turned into what the touch controls send.

| Pad | In the game | In menus | In a spell lesson |
|---|---|---|---|
| Left stick / d-pad | walk (arrow keys) | | |
| Right stick | camera (relative mouse); aims with the movement mod | moves the cursor | moves the wand |
| A, LB or LT | jump; skips a cutscene | A: click | |
| X, RB or RT (held) | the wand | | draws |
| Y | save | | |
| B or Start | Esc (pause menu) | back | |

While the pad is in use the on-screen arrows, wand and jump are hidden; touching the screen brings them back. B also closes the settings panel. In menus the overlay draws a cursor (the game draws none on a touch screen) and A moves the game's cursor there before it presses, a frame later, as the menus need.

Touch and cursor positions go to the engine as view pixels times the render scale, so the render surface must have the view's shape. It is sized from the surface view's own size once laid out (before that, the display's real size), never from `getDisplayMetrics()`: that is the area the system bars leave, a navigation bar shorter on handhelds and tablets, whose bar sits below in landscape. The full-screen view stretched the shorter surface and the menu cursor clicked below where it was drawn (players aimed a little higher). The log report's `game view:` line shows the view and surface sizes.

### Spell lessons on Android

The drawing is judged more gently than on the PC (`KWGesture.cpp`, `ScoreGestureGentle`), for a finger and a stick alike: only coverage counts, the share of the symbol's template samples with a drawn point within twice the lesson's accuracy; strokes beside the symbol cost nothing, and a stroke of fewer than 10 points still scores 0. The lesson's pass mark is unchanged. With a gamepad the lesson also gives 35% more time (`DrawTime`, once per lesson, `HP1::TickLessonTouch`), the wand moves with the right stick through the lesson's own mouse input (slowly, fractions of a mouse count add up), and the left stick does nothing there.

## Where the game is (the folder picker)

`SetupActivity` looks for `System/HP.exe` in `/sdcard/FlipendoHP` (and one or two folders deeper) first, then in the folder the player picked. With no game anywhere it shows **Game data not found** with **Choose folder** (the system folder picker, `ACTION_OPEN_DOCUMENT_TREE`; with all files access the picked tree is read as a path: `primary:` is internal storage, other volumes are `/storage/<id>`), **Check again** and **Send logs**. A picked folder without the game shows **This isn't the game folder**; the choice is remembered either way (`setup` preferences, `gameDir`), so the next start says what is wrong instead of asking again. The folder found is passed to the game (`FlipendoActivity.getArguments`, `argv[1]` in `android_main.cpp`); logs, settings and the engine's own files stay in `/sdcard/FlipendoHP`. The log report lists the picked folder too.

## Builds on GitHub

`.github/workflows/android.yml` builds the light APK on a Windows runner with `android/build-apk.sh`. Push the release tag `android-v<VERSION>` (below) and the APK is published as a pre-release named after the version, with notes from the commits; a tag that doesn't match `android/VERSION` fails the run; *Actions > Android APK > Run workflow* builds it without a release (the APK is in the run's artifacts). Every build must be signed with the same key, or it won't install over an older one: the key is the repository secret `ANDROID_DEBUG_KEYSTORE`, the base64 of `android/debug.keystore` (*Settings > Secrets and variables > Actions*). The self pack is never built there: it carries the game.

## Versions

The Android port is numbered `0.MINOR.PATCH` until the whole game can be played through: new features raise MINOR, fixes
alone raise PATCH. The number is in `android/VERSION`, and its release is the tag `android-v<number>` (`android-v0.2.0`;
the `android-` prefix leaves room for desktop releases of Flipendo). To release: raise `android/VERSION`, commit, then
`git tag android-v0.2.1 && git push origin master android-v0.2.1`.

`build-apk.sh` writes it into the APK: `versionName` is the number for a build of exactly that tag (and no local changes),
else `<number>-dev+<commit>`; `versionCode` is the commit count of the branch, so a newer build always counts higher and
installs over an older one. The version shows in the settings header and on the problem cards ("Flipendo for Android
0.2.0"), in `device-info.txt` of a log report and in logcat when the game starts.

Releases before the numbering: `android-2026-10-11` is 0.1.0 (problem cards, gamepad, folder picker).

## OpenGL ES renderer

Since 0.2.5b **OpenGL ES is the default** and Vulkan is picked by hand. Qualcomm's stock drivers (Adreno 618 / 642L with driver 512.x, Vulkan 1.1) refuse one of the
engine's Vulkan pipelines (`Could not create graphics pipeline: vkResult -13`) and the game stopped before the menu. The port
therefore also renders with **OpenGL ES** (3.0 and newer): SurrealEngine's desktop OpenGL device (`RenderDevice/OpenGL/`) built against
GLES (patch 0200). It became the default when a POCO X3 Pro (Adreno 640, driver 512.502) showed the menu on Vulkan
but crashed inside the driver after the app came back from the background (`vkEndCommandBuffer`, after `dequeueBuffer failed`
and `AdrenoUtils: Unknown Format 0` on the new swapchain): a crash after frames were drawn, which the fallback below cannot
catch. OpenGL ES also ran faster than Vulkan on the developer's Mali phone.

- `GLESCompat.h` replaces the desktop loader on Android: `<GLES3/gl32.h>` (libGLESv3), shims for the calls ES spells
  differently (`glClearDepthf`, `glDepthRangef`, `glDrawBuffers`, `glTexStorage2DMultisample`, `glFramebufferTexture2D`) and
  `GLHasExtension`.
- SDL creates an ES 3.2 context (`sdl3_display_window.cpp`, attributes set before the window so the EGL config matches).
- Shaders get `#version 310 es`, highp defaults and `layout(std140) uniform;`; the scene shader's integer flags use `u`
  literals (ES has no implicit int/uint conversion).
- Textures: BC1-3 are decoded on the CPU with the Vulkan path's `BCDecodeBlock`, BGRA8 (our Cyrillic glyph atlases) is
  swizzled to RGBA8. The scene targets are RGBA16F when the driver can render to half float (`GL_EXT_color_buffer_half_float`),
  else RGBA8 (no overbright bloom). No MSAA, no depth clamp, no sampler LOD bias (ES lacks them); the dither texture is R16F.
- The engine logs `OpenGL renderer: <GPU>, <GL_VERSION>` and the scene buffer format.

**Choosing it.** `android_main.cpp` reads `/sdcard/FlipendoHP/.flipendo-renderer` (`opengl` or missing: OpenGL ES, `vulkan`, `opengl-auto`)
and passes `--renderer=opengl|vulkan` (GameApp, patch 0002, sets `LauncherSettings::RenderDevice.Type` before the window is
made). When a Vulkan run ends with a Vulkan `FATAL` line in `flipendo.log` (vkResult, VK_ERROR, graphics pipeline,
swapchain...), it writes `opengl-auto` and exits with code 3; `FlipendoActivity.onGameStopped(3)` shows a toast and restarts the
game without the problem card. Settings has a **Renderer** button in the footer (Vulkan / OpenGL ES, "(auto)" when the engine
switched); a change asks for a second tap and restarts. The launcher's start screen has the same choice (the launch card,
`SetupActivity.launchPanel`: OpenGL ES | Vulkan), and the game starts only on its **Play** button (a restart from inside the game, or the
fallback to OpenGL ES, starts by itself). **Remember my choice** (unticked by default, `setup` preference `keepRenderer`): ticked, the
renderer picked there stays; unticked, it is for that launch only and the next normal launch puts back the one from before
(`restoreRenderer`, the old value kept in `restoreRenderer`; a switch in the in-game settings is for good and drops it). The log report's `device-info.txt` has a `renderer:` line.
`/sdcard/FlipendoHP/fail_vulkan.txt` makes Vulkan fail on purpose to try the switch. The manifest no longer requires Vulkan,
only OpenGL ES 3.0.

### Black level geometry on older Mali (Helio G91, Mali-G52 r32)

The level's walls and floors were black while actors (Harry, Dumbledore, the armours, particles) drew fine: the surfaces are
the textures with full mip chains (and the lightmaps), the actors' skins and the HUD mostly have a single level. The
desktop device makes every texture mutable (`glTexImage2D` per level, nothing guaranteeing the chain reaches the sampler's
base level range); the Mali-G52 r32p1 driver sampled such textures black while Mali-G615 (r44) and Adreno were fine. On
Android a texture now gets immutable storage (`glTexStorage2D`) with `min(NumMips, levels down to 1x1)` levels and
`GL_TEXTURE_MAX_LEVEL` set to the last; `UploadData` stops at the allocated levels (`GL_TEXTURE_IMMUTABLE_LEVELS`).
The log has `OpenGL limits:` and `OpenGL extensions:` lines, and an incomplete scene framebuffer is reported by its status.
Test aid: `/sdcard/FlipendoHP/gl_nomip.txt` makes every texture single-level (if the geometry then shows, mip chains are
the culprit on that phone).

## FPS limit

Settings > Display > **FPS limit** (25-120, steps of 5; default 60; above 60 the app also asks a 120 Hz screen for that refresh rate, and cutscenes always run at 60 at most because the original's cutscene walks break at high frame rates) sets the frame limit mod's limit while the game runs
(`HP1::Mods::SetFrameLimit`, `src/hp1/mods/FrameLimit.cpp`; overrides `--fps`, `--vanilla` still turns it off). Lower limits
save battery and heat on slow phones. Kept in the app's preferences (`fpsLimit`).

## Dubbings (self pack)

`build-apk.sh --voice code=<Sounds folder>` adds an edition that is the base install with its `Sounds` replaced:
`--voice ru-softclub=Sounds-Rus-Softclub` (SoftClub's Russian voices; of its packages only `AllDialog.uax` differs from the
English one). Its langs/ folder holds its own Sounds files and the base's version of every other file an edition changes,
so switching from Fargus puts the base fonts and texts back. A `langs.txt` label may end in `|rutext`: the Russian text
translation is put in with that edition (`SetupActivity.languageHasText`). The folder is game data: `.gitignore` keeps
`/Sounds-Rus*/` out of the repository, and it only goes into the self pack.

## Flying the broom

When the player is a `BroomHarry` (the Remembrall chase, the flying lesson, Quidditch; `g_onBroom`, set in
`HP1::TickLessonTouch`), the overlay changes two buttons:
- **The wand button is a steering stick that presses nothing.** Its left mouse button is the broom's brake
  (`LeftMouse=AltFire | Button bBroomBrake`), and BroomHarry's script only boosts while `bBroomBrake == 0`, so with the
  movement mod (where the wand button is an aim stick holding the left mouse) every turn braked and dropped the boost.
- **JUMP toggles the boost** (`RightMouse=jump | Button bBroomBoost`): one tap keeps it on, so the right thumb is free to steer.
The toggle ends when the flight does.

## Vulkan surface after the background

When the Android surface is destroyed (the app goes to the background), SDL keeps a stale native window pointer in the
window's properties. The Vulkan renderer noticed the lost swapchain and made a new surface for that window, and the Vulkan
loader crashed (Redmi Note 10S, Mali-G76: `CreateVulkanSurface` -> `libvulkan.so`, fault addr 0x98). FlipendoActivity now
reports the surface's life (`SurfaceHolder.Callback` -> `nativeSetSurfaceAlive`), `SDL3DisplayWindow::CreateVulkanSurface`
returns no surface while it is gone, and `CommandBufferManager::SubmitCommands` skips presenting until it is back.

### A dubbing from a folder (every APK)

Settings > Language > **Добавить озвучку из папки…** opens the system folder picker. The player picks a dubbing's Sounds
folder (or a folder with a Sounds folder inside, such as a localized install). `SetupActivity.importVoice` keeps the
`.uax`/`.wav` files that the game has under the same name and that differ from it (for SoftClub's dubbing only
`AllDialog.uax`) in the app's private files (`<files>/voices/custom/`, paths relative to Sounds); a folder without any of the
game's sound files, or one identical to the game's, is turned down with a message. The language list then has
**Русский (своя озвучка)** (`voice`): those voices and the Russian text translation. At start the launcher puts the files
over the game folder's Sounds, the originals kept in `.voice-backup` (`.flipendo-voice` marks it installed); another
language puts the originals back. A game folder copied over again gets the dubbing once more (size check). In a self pack
the `voice` choice goes over the base edition.

## Android 10

The app supports Android 10 (minSdk 29), but called `Environment.isExternalStorageManager()` (Android 11) unguarded at the
very start, so on Android 10 (a POCO X3 still on its first system) it stopped before writing any log. On Android 10 there is
no "All files access": the manifest asks for `READ`/`WRITE_EXTERNAL_STORAGE` (maxSdkVersion 29) with
`requestLegacyExternalStorage`, and `SetupActivity.hasStorageAccess` asks for that permission instead of opening the
All files access page.

### OpenGL ES 3.0 is enough

The GL path asks for ES 3.0 (manifest `glEsVersion 0x00030000`, SDL context 3.0; drivers give their newest compatible
version). What it needed from 3.1/3.2 is replaced on Android:
- GLSL ES 3.00 instead of 3.10 (`ToGlslEs300` in GLRenderDevice.cpp rewrites the desktop shaders line by line): no
  `layout(binding)` (sampler units set by name after linking, `BindSamplerUnits`), no `layout(location)` on the varyings
  (they match by name), no `centroid` (no multisampling here), and the scene's unused `textureBinds` input is dropped.
- One blend state for all attachments (the indexed `glEnablei`/`glBlendFuncSeparatei`/`glColorMaski` are ES 3.2): the
  second scene attachment is the integer hit buffer, which ES never blends.
- `glFramebufferTexture` (3.2) is `glFramebufferTexture2D`; multisampled textures (3.1) are never made (no MSAA).
OpenGL ES 2.0 would be a new renderer (no integer attributes, uniform buffers, sampler objects or several outputs), and
64-bit Android 10 phones have ES 3.0 or newer anyway.

## Harry Potter 2 (0.3.0 - 0.3.2)

One app, two games. The launcher (`SetupActivity`) has a **HP1 | HP2** switch; each game has its own folder
(`/sdcard/FlipendoHP`, `/sdcard/FlipendoHP2`; preferences `gameDir`, `gameDir.hp2`), its own folder picker cards
(HP2 is recognised by `System/Game.exe`, `isHp2Folder`) and its own saves. Settings, logs and the FPS limit work in both.

- **Saved game row (HP2):** *New game* and six slots with the level's picture (`System/Thumbnails/<map>.bmp`) and the
  time; the launcher writes the choice to `/sdcard/FlipendoHP/hp2_start.txt` (`new N` / `load N`), `HP2::OpenStartMenu`
  reads it. Saves are `save/Save<N>.rus` + `Slot<N>.txt` in the HP2 folder (`HP2Save.cpp`); the SAVE button works anywhere.
- **Russian text:** HP2's texts are files of the Russian edition's `harry2` folder (`*.rus`, Windows-1251). Put the
  folder into the game folder (`harry2/`); the launcher copies it to `System/` and starts with `--cyrillic`.
- **Controls:** Left/Right strafe (HP2 has no turning on the sticks' keys); the SKIP button presses Space, which is turned
  into `HPConsole.StartFastForward` during a cutscene.
- **Debug menu:** Settings > *Debug menu* (HP2 only): go to any level, set the story state (`GSTATE000`..`GSTATE180`),
  give all spells.
- **Caches for the light APK:** zips with a `FlipendoHP` (HP1, English) and a `FlipendoHP2` folder inside, unpacked to the
  phone's storage root. The HP2 one is the retail install without the SafeDisc files, saves and logs, plus `harry2/`.
- **Self pack with HP2:** `android/build-apk.sh --selfpack "C:/Games/HP" --hp2 "<HP2 install>" --hp2-texts "<harry2 folder>"`
  also packs HP2 (`assets/hp2data`); `installHp2Data` unpacks it into `/sdcard/FlipendoHP2` once (saves are left alone).

## Control layout and the potion (HP2)

- **Control layout** (Settings, *Edit*): the wand button, the jump button, the arrow pad and the toolbar can be dragged and
  resized (60-180%); the offset (fractions of the screen) and the scale are kept in the `flipendo` preferences
  (`ctl_<group>_x/_y/_s`) and applied in `Overlay.applyLayout` after the default places. A group stays inside the safe area.
- **Wiggenweld potion (HP2):** `harry.bDrinkWiggenwell` is the Q key (DefUser.ini). A two-finger tap anywhere on the game
  view (both fingers short and still, not on a button) presses Q; on a gamepad a click of either stick does. The game
  ignores it at full health or in a cutscene.

### Vendor offers (0.3.2)

HP2's Yes/No offers (the beans for a duel or a broom, `VendorManager`) are drawn on the HUD and clicked with the console's mouse
while the game is in cutscene mode. While `harry.CurrVendorManager` is set the overlay treats the screen as a menu (the finger
or the pad's cursor is the mouse; `FlipendoActivity.menuActive`), and the touch position goes into HUD units instead of the
4:3 menu area (`HP1::MenuMousePosition`), the space the vendor's hit test works in. Duels use the existing controls: the wand
button charges and casts, the jump button (right mouse) cycles the three spells, left and right strafe.

## 0.3.3

- **HP2 controls:** the wand button also turns the camera (a stick with a small range, `Overlay.aimStick`; Y inverted against HP1's), and Harry
  can run while the wand is held (`NGetCurrentKeyState` answers "mouse button down" for keys 1, 2 and 4 while `bAltFire` is held:
  `harry.PlayerInput` allows moving while casting only then). The toolbar's SAVE button and the pad's Y save are off in HP2 (the game's
  save points still save); the Movement mod row is HP1's only. The launcher has a button that deletes the selected HP2 slot.
- **HP2 English text:** the launcher sets `Language=` in `default.ini` / `SE-Game.ini` (`rus` for the Russian choice, `usa` for English when
  `HpDialog.usa` is in System: the Russian edition's `.int` files are Russian), so English shows its subtitles.
- **The wand's tip (HP2):** the scripts put the tip at `WeaponLoc + (0, 0, 20)` rotated by `WeaponRot`. HP2's weapon bone has Z pointing away
  from the wand (opposite to it for Harry), so `KW::PawnWeaponFrame` takes WeaponRot's Z from the wand model (its origin to the far corner of
  its bounds, through the transform the wand is drawn with; 19.9 long): Lumos and the spell particles sit on the tip.
- **Adreno 735:** when the driver won't map the scene vertex or index buffer (`glMapBufferRange` returns null without a GL error), the frame is
  built in memory and copied in with `glBufferSubData` (`GLRenderDevice::MapVertices` / `UnmapVertices`).

## 0.3.4

- **Settings > Graphics fix** (OpenGL only; the choice is `/sdcard/FlipendoHP/.flipendo-gltest`, read by `GLRenderDevice.cpp` when the renderer
  starts, so the game restarts): for a GPU whose level walls and floors come out black while the characters are fine (a PowerVR Rogue GE8300 box;
  the lightmap is the layer only level surfaces have). Options: *without the lightmap* (`NOLIGHTMAP` in `Scene.frag`: unlit but visible),
  *without macro, detail and fog layers* (`NOEXTRA`), *lightmap with a plain bilinear filter* (a dedicated sampler without mip filtering).
  The first helped the PowerVR box; the cause is still being narrowed.
- **HP2 debug menu:** God mode (health topped up every frame), Fly, Ghost, Walk again. Fly and Ghost set `AirSpeed` and `AccelRate` first:
  HP2's Harry has `AirSpeed = 0`, which capped the flying speed at 0. `/sdcard/FlipendoHP/hp2_debug.txt` (one command per line: `level <map>`,
  `state GSTATE...`, `spells`, `god`, `fly`, `ghost`, `walk`) is taken once by the game, for testing without the dialogs.
- **Easter eggs:** one tap on the version text (at the very bottom of the settings, or the gold version beside the name in the launcher) shows a toast with a line of the game, and two seconds later opens its link; a direction code in play opens the same link.

## 0.3.5

- **HP2 editions (Russian voices and texts):** the language row of HP2 lists English, the Russian text (a `harry2` folder of `.rus` files) and every
  edition found in `<HP2>/editions/<code>/` (the files that differ from the base game: `Sounds/AllDialog.uax`, `System/*.rus`,
  `Textures/MenuArt.Rus_utx`, and `edition.txt` with its name). The choice is kept per game (`.flipendo-language` in the HP2 folder, the in-game
  wish is `.selfpack-want` there). `SetupActivity.applyHp2Edition` puts the edition's files over the game, keeping what they replace and the
  base `AllDialog.*_uax` in `.hp2-edition-backup`, and sets `Language=` in the ini files (`rus`; `usa` for English). A self pack carries editions
  (`build-apk.sh --hp2-edition "ru=C:/Games/HP2Rus|Name"`); a light install gets one from a cache zip unpacked into the storage root
  (`FlipendoHP2/editions/ru/...`).
- **`.nomedia`:** the launcher puts an empty `.nomedia` in the game folder, so the game's pictures stay out of the phone's gallery; the cache zips carry it too.

## 0.3.6

- **Merged with Flipendo** (upstream 62e5e27, SurrealEngine 8d61ae5): texture packs, HP2 music, sound and audio fixes, the widescreen canvas shared by both
  games (`src/knowwonder/KWCanvas.cpp`), a safer mover push. Where both sides fixed the same thing, the upstream's was kept (movers that push what they stand
  on), except the sprite orientation: ours stays HP2-only (the GL renderer draws HP1's sprites right side up). On Android the texts stay 8-bit
  (`IniFile` narrows the UTF-16 ones, `UFont::ByteText`) because the Cyrillic is drawn per byte.
- **Settings > Game folders** (start screen): the folder of each game, picked separately; the picked one wins over the default place
  (`/sdcard/FlipendoHP`, `/sdcard/FlipendoHP2`). Each game starts without the other's files and only unpacks its own self-pack data.
- **Play the other game from inside a game** (Settings header, tap twice): the launcher starts the other game by itself; it sits in its own process with its
  own copy of the preferences, so the choice travels in the intent (`game`), and HP2 gets its start choice written (a slot to load, or a new game).
- **Invert wand:** flips the up/down aim of the wand button while the spell is held.
- **Gamepad:** a cutscene is skipped by holding Y (0.7 s), not by A, LB or LT.
- **HP2:** the SAVE button and Y save are back (`HP2::TickSave`). Challenge stars (Ch1Rictusempra...) add their time and the last one ends the lesson:
  the star's `EndState` does it, and HP2 compiles some states' probe masks without EndState (`UObject::IsEventEnabled`).
- **First person (Settings):** the camera sits in Harry's head while he walks about (`KW::FirstPersonView`, state `PlayerWalking`, no cutscene); cutscenes,
  lessons, duels, the broom and shops keep the game's camera. Eye height is `BaseEyeHeight`, 18 units in front of the head so its mesh stays out of the picture.
- Script VM: struct out-parameters reach the caller again after the VM rework (the camera of HP2 stayed in Harry's torso: `ExpressionValue::Store` dropped
  Vector and Rotator values).
- `build-apk.sh` gives d8 the class list in a file (the command line had grown past what Windows allows).

## 0.3.7

- **Merged with Flipendo again** (upstream c564a7e, same SurrealEngine 8d61ae5): HP2's front end (desktop only, skipped here: the launcher is `SetupActivity`),
  several HP2 songs at once as ALAudio plays them (`src/hp2/HP2Music.cpp`), HP2 saves (`KW::SaveGame` now sends PreSaveGame / PostSaveGame itself), a script
  recursion guard, the bronze pickup particle fix. The engine patches were rebuilt by a three-way merge of the patched trees. Saves stay ours: the launcher's slot
  (`HP2::TickSave`, `LoadGame`), not upstream's Save0 in a slot folder.
- **Settings on a squarer screen** (a foldable opened: Galaxy Fold, Honor Magic V): the panel's height and its text unit follow its width (`lh`, `lu` in
  `onSizeChanged`), so the header, the sliders and the footer no longer overlap.
- **HP2 FPS limit:** the limiter ran twice a frame (upstream's `TickMods` now calls it for HP2 too, and `HP2::TickTouch` still did), so 60 gave 30 and 25 gave 12.
- **HP2 voices:** a package that exists only as another language's file (`AllDialog.USA_uax`, `.RUS_uax`) is found whatever `Language=` says (Android keeps `int`),
  USA first (`PackageManager::ScanFolder`).
- **HP2 music:** `USurrealAudioDevice::Update` ran the music update for HP1 only, so HP2's songs never started. The mixer is put back when a video drops it
  (`Engine::PlayVideo` sets the device's music and then clears it). While the game is paused (the menu open) the songs playing before wait and go on from where
  they were; the menu's own song plays meanwhile.

## 0.4.0

- **Merged with Flipendo** (upstream af1e267, same SurrealEngine 8d61ae5): HP2's sound as ALAudio plays it (inverse distance falloff, ambience, priorities, 32 channels, pause),
  lip sync, the new XA decoder, persistent actors across hub travel, the faithful story-state screening, Lumos surfaces, tick order, stair smoothing and the
  physics fixes. Saves stay per launcher slot (`HP2::TickQueuedSave` writes `Save<slot>`, `LOADGAME` reads it). The patches were rebuilt by a three-way merge of the patched trees.
- **Settings in three pages** (Display, Controls, Game) and two new switches: **No cutscene skip** and **No quick save**.
- **FPS limit up to 120**: the app asks the display for the rate; cutscenes run at 60 at most (the original's cutscene walks break at high frame rates).

## 0.4.1

Speed, a new look and a longer settings panel.

**Speed**
- **Vulkan runs a frame ahead.** `Unlock` used to wait for the GPU after every frame (21 ms of a 37 ms frame), so the CPU and the GPU took turns. Now the
  frame is submitted and the CPU goes on with the next one; the wait for the GPU (`CommandBufferManager::FinishFrame`) comes with the first thing of the next frame
  that needs what the GPU is using (the command pool, the vertex and staging buffers, the descriptor sets, the delete list), after the game logic and the
  visibility walk. 27 to 48 FPS on the Hogwarts grounds on a Mali-G615 (OpenGL ES: 52). OpenGL ES is still the default and still a little faster. Level changes,
  cutscenes and returning from the background on Vulkan have had little testing: if a picture breaks there, switch the renderer back.
- **Performance hint.** The game thread tells Android (APerformanceHint, found at run time, API 33+) how long its frames take, so the scheduler keeps it on a fast
  core and raises the clocks in time (53 FPS instead of 45 on the grounds on a Dimensity 8300; the thread was on a big core there already, other phones may differ).
- **The sky is cheaper.** Its frame walks only the BSP subtrees that reach the sky zone (the node `ZoneMask`; the sky is a sealed box), not the whole level a second
  time: 3.3 ms of a 22 ms frame down to 0.5-1 ms outdoors. A vertex mesh's vertices are positioned once per draw, not once per triangle that uses them.
- **Performance log** (Settings > Game). Frame phase timers (`src/knowwonder/KWProf.h`): every 2 s the log gets a `PROF` line with the milliseconds per frame of
  the game logic, the BSP walk, the sky and portals, the surfaces, the actors and the present, so a slow phone's log shows where its frame goes (the log report has it).
- **Where the CPU goes** (measured with those timers on the HP2 grounds, 45 FPS, render scale 100%; simpleperf is blocked on that Xiaomi build): everything but sound
  runs on the game thread (about one core, the other seven idle). Per frame: game logic 4.1 ms, BSP walk and occlusion clipping 3.8-4.5 ms, the sky 3.3 ms (now
  0.5-1), other meshes 2.5 ms, skeletal mesh lighting 1.7 ms, present 1.4 ms, BSP surfaces 1.4 ms; the render device calls are 2.9 ms of a 19 ms frame, so a
  render thread would gain 15% at best and was left out. Lower render scales do not help on OpenGL ES (the CPU is the limit), they do on Vulkan (the GPU takes
  20 ms there at 100%).

**Look**
- **New launcher.** A night-blue start screen; the two games are covers (the Philosopher's Stone red with a cut gem, the Chamber of Secrets green with a serpent),
  the chosen one lit with a gold edge; the saved games and the renderer on the right, and Play pinned under them so it never scrolls away. Play says what it does
  (Continue slot N, Start a new game).
- **Interface size** in the launcher (A-, A+: 100 to 250%, default 100%) scales the whole launcher (every size there is in dp / sp, so the density is scaled;
  `launcherScale` in the `setup` preferences). The "Remember my choice" box no longer overlaps the hint above it.
- **Larger settings.** The panel's text, rows and padding are bigger and the page scrolls; Done is a large button at the top right, the channel and the controls help
  are icons beside it, and the renderer, graphics fix and the other game moved into the Game page.

**Fixes**
- **First person stays on while a spell is held.** HP1's Harry goes to the `PlayerAiming` state for as long as the spell is held, and only `PlayerWalking` kept
  the camera in his head, so every cast jumped to the third person camera. The log names any other state that takes the camera back.

## 0.4.2A

Chamber of Secrets fixes, the Philosopher's Stone opening and loading screen, four languages, TV support, a sound on the version tap.

**Fixed**
- **Aragog's fight no longer breaks.** After a few casts the controls died (no turning, no casting, Harry running left on his own). The web spell moved a sticky web onto
  itself again and again until the game's script gave up ("Infinite script recursion"), and each failure left the fight half-set. SurrealEngine never skipped a
  *singular* script function while it already ran on the same object, as the original does; now it does (`Frame::Call`, `0010-engine-fixes.patch`, any game).
- **Aiming in the Slytherin common room (the room-turning puzzle).** Next to the iron gates the aim camera sits inside a gate, and every aim ray "hit" the gate
  at distance zero, so the cursor landed on the camera, behind Harry. The cursor's ray is no longer stopped by a mover it starts inside.
- **The secret floor of the second Skurge trial (the Lumos floor).** A jellybean lying in the floor's path stopped the sliding floor at 3 % of its move, so it stayed
  solid and the star below could not be reached. A mover is no longer stopped by an actor that blocks neither actors nor players. *Still to be confirmed in play.*
- **The Philosopher's Stone opening is back.** New Game plays the storybook again (the Android start always skipped it).
- **Loading screens.** The Philosopher's Stone loading screen stays up for at least 3.5 s, fills the whole screen (it was a 4:3 picture in the middle), and its text
  is centred and wraps inside the parchment instead of running off the right edge (long Russian titles were cut). Centred text in both games uses the same rule.
- **Quitting a game returns to the launcher** (it closed the whole app).
- **Skip** is an icon, not the word, so it fits in every language and on the gamepad (it also said "Hold Y").
- **Wand control in the Philosopher's Stone is the same as in the Chamber of Secrets**: the same size and place, the same small ring, and it turns the camera like a
  stick (with the movement mod it was higher with a big ring; that stays only on the broom).

**New**
- **Wand sensitivity** (Settings > Controls): 25 % to 300 % of how fast the wand stick turns the camera.
- **Languages for the launcher and the in-game settings**: English, Українська, Беларуская and Слобожанський (Russian), chosen with a chip in the launcher's top bar;
  without a choice the phone's language is used. About 200 texts (`Tr.java`); the game's own texts and voices have their own switch, as before.
- **Android TV**: the launcher is on the TV home screen with a banner, the touch screen is not required, every launcher button shows a white ring where the remote or
  gamepad is, and a TV without an "All files access" page gets the way to grant it with adb. Playing still needs a gamepad.
- **The version** is a gold chip beside the name in the launcher and sits at the very bottom of the settings. One tap on it shows a line of the game and, two seconds
  later, opens its easter egg link (the direction code in play still works); a sound (`res/raw/version_sound.mp3`, 2.6 s) plays with the toast.
- The launcher's default interface size is 100 % (it was 125 %).

**For bug reports** (log lines): `CursorRay` (the aim ray and what it hits), `SpellTouch` and `SpellHitWall` (what a spell touched or flew into), `PlayerBlocked`
(what stopped Harry, with its texture and bounds), `Mover ... starts moving` and `Mover ... blocked at key` (a mover's moves and what stops them).

**Not fixed yet**
- Chamber of Secrets, first Rictusempra room: the firecrab/platform puzzle still needs a play test with the new logs (`SpellTouch done` shows what the crab does).
- The Slytherin common room's gates are closed until the puzzle opens them: they block Harry and spells, as designed (seen from the debug menu's level jump).

## 0.4.2B

Merged with Flipendo again (upstream 81336c4, the same SurrealEngine; no conflicts in the engine patches).

- **Flipendo's "Improved movement" for the Philosopher's Stone came with the merge and is switched off** (`--classic-movement`, always): on the phone it was unusable. The
  movement mod (AdamJD's packages) is the one that is used: **it is on by default** (the first mod the APK carries, or one imported from a file; a plain APK without one
  plays the original controls) and **Settings > Game > Movement mod is gone**. The Devil's Snare prompt ("Switch off and restart", for the level where the mod can't
  aim) still turns it off once; the next start has it on again.
- Flipendo's CI, unit tests (`src/tests/`) and `tools/check_repo.py` / `tools/smoke_test.py` came with the merge; nothing in them runs on the phone.

## Merge of upstream 47801a2 (after 0.4.2B)

Merged with Flipendo again (upstream 47801a2: mod ids and `--mod` / `--no-mod` / `--list-mods`, singular functions, centred canvas text, HP2 bounce on movers, the loading screen hold mod).

- **The engine patches conflicted** (`0002`, `0010`, `0130`) and were rebuilt by a three-way merge of the patched trees (base, ours and upstream each applied to SurrealEngine 8d61ae5). Where upstream does what an earlier merge of ours did by hand, upstream's version won: the singular function skip (a side table instead of an object flag), the centred canvas text, and the loading screen hold (the `LoadingScreenHold` mod replaces our wait in `KWView.cpp`). Ours stayed in `--renderer`, the full-screen loading canvas and the Android patches.
- `FrameLimit.cpp` asked the mod's old `Enabled()`; it now asks `IsOn("frame-limit")`.
- Not played on a phone yet: the loading screen and the HP2 Aragog fight (singular skip) are the places to look.
