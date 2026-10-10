<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="images/branding/flipendo-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="images/branding/flipendo-logo.png">
    <img src="images/branding/flipendo-logo.png" alt="Flipendo" width="560">
  </picture>
</p>

<p align="center">
  <b>Flipendo Droid: Harry Potter and the Sorcerer's Stone (2001) on Android, with touch controls.</b><br>
  <sub>A native arm64 port of <a href="https://github.com/kroplabeskidu/flipendo">Flipendo</a>, an open-source engine that runs the original PC game.</sub>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#install">Install</a> ·
  <a href="#settings">Settings</a> ·
  <a href="#news-reports-and-chat">News &amp; reports</a> ·
  <a href="#status">Status</a> ·
  <a href="#build-it-yourself">Build</a> ·
  <a href="docs/android.md">Android docs</a> ·
  <a href="https://t.me/flipendodroid">Telegram</a>
</p>

<p align="center">
  <img src="images/screenshots/grand-staircase.jpg" alt="The grand staircase in the first level" width="100%">
  <br><sub>The first level (a screenshot from the desktop version; the Android port renders the same scenes).</sub>
</p>

This fork runs the **2001 PC game** *Harry Potter and the Philosopher's / Sorcerer's Stone* (KnowWonder / EA) on an Android phone.
The levels, scripts, voices and music are the game's own files; what runs underneath is
[SurrealEngine](https://github.com/dpjudas/SurrealEngine), an open-source reimplementation of Unreal Engine 1,
extended by [Flipendo](https://github.com/kroplabeskidu/flipendo) with KnowWonder's engine code. This repository adds the
Android side: an SDL3 + Vulkan build for arm64, a touch interface and a lot of GPU and lifecycle work to make it behave on
real phones.

**You need your own copy of the game.** The app contains no game data.

## Features

**Made for touch**
- A floating **move stick** (or four always-visible **arrow buttons** you can slide between), drag to **look**, and **CAST**,
  **JUMP**, **SAVE**, **MENU** and **SKIP** buttons with their own icons.
- The menus take your finger as the mouse.
- **Spell lessons** are redrawn on a larger canvas and the wand follows your finger, so symbols are easy to draw.
- The gameplay controls disappear during **cutscenes**; a **SKIP** button shows up only there.
- **Save anywhere:** the book button writes a normal save into your slot; Start Game lists it.

**Settings panel** (gear button): movement style, render scale, subtitle / HUD size, field of view, Auto Jump,
Debug mode and a link to the Telegram channel. Everything is remembered. See [Settings](#settings).

**Made to survive phones**
- Works on **Mali** GPUs (BC1-3 textures decoded on the CPU, no `gl_ClipDistance`, a per-frame command-pool reset that fixes a
  GPU memory leak) and falls back to a **no-bindless** shader path on older GPUs.
- **Going to the background and coming back** keeps the game alive (the Vulkan surface is recreated, audio pauses).
- **Smart game-folder search** and clear first-run screens (missing game data, a crash on the previous run).
- **One-tap bug reports:** hold MENU to share a zip with the log, the crash backtrace, logcat and device info.
- Fixed in the engine for this port: the **menu music no longer plays under a level** after loading a save, saves show up with
  their thumbnails (UTF-16 conversion), and the touch cursor works in the menus.

## Install

1. Get the APK: [build it](#build-it-yourself).
2. Install it (`adb install -r android/dist/flipendo-droid.apk`, or copy the APK to the phone). Xiaomi / MIUI may need
   *Install via USB* on, or you install from the phone itself.
3. Copy your installed game folder (the one with `System/`, `Maps/`, `Textures/`) to `/sdcard/FlipendoHP/`,
   for example `adb push "C:\Games\HP\." /sdcard/FlipendoHP/`.
4. Open the app and allow **All files access** when it asks, then open it again. The game starts at the main menu.

Supported game files: the retail US and UK releases, a no-CD `HP.exe` and other language editions. If yours isn't
recognised, the log shows its SHA-1: [report it](CONTRIBUTING.md#other-game-versions). Requirements: Android 10+ (API 29),
an arm64 phone with a Vulkan-capable GPU.

## News, reports and chat

Builds, news, bug reports and questions live in the **Telegram group: [t.me/flipendodroid](https://t.me/flipendodroid)**.
When something breaks, hold MENU in the game to share the log zip and post it there.

## Settings

Tap the gear in the top row.

| Setting | What it does |
|---|---|
| Movement controls | floating stick (default) or four arrow buttons |
| Render scale (30-100%) | the game is drawn at this share of the screen resolution and scaled up; lower is faster (default 50%) |
| Subtitles and HUD size (60-180%) | scales the 2D UI; the menu book keeps its size |
| Field of view (-20 to +40) | added to the game's field of view |
| Auto jump | Harry jumps by himself at ledges (on by default) |
| Debug mode | Level Select in the main menu and the game's debug text (on by default) |
| Telegram channel | opens `t.me/flipendodroid` |

## Status

**All levels are playable.** The port runs the whole story, with the menus, cutscenes, saving and loading, spell lessons by
touch and music. It reaches about 60 fps on a POCO X6 Pro at the default render scale.

| | |
|---|---|
| ✅ Works | every level, menus, saving and loading, cutscenes, characters, spell lessons by touch, music, going to the background and back |
| 🟡 Partly | GPUs without bindless textures (older Mali) run through a fallback path: it works, how fast depends on the GPU |
| ❌ Not yet | spell casting in fights isn't adapted to touch; no autosave if the system kills the app in the background; the Options page still shows empty key-binding bars |

### Tested devices

| Device | GPU | Notes |
|---|---|---|
| Xiaomi POCO X6 Pro | Mali-G615 | the main test phone |
| Redmi Note 13 Pro | Mali-G57 | works |
| Redmi 13C | Mali-G52 | works through the no-bindless fallback |
| Samsung Galaxy S23 | Adreno | works |
| Samsung Galaxy S25 | Adreno | works |

Another phone? Hold MENU in the game and send the log zip to the [Telegram group](#news-reports-and-chat): which GPUs work
is the most useful report.

## Build it yourself

You need Windows with Git Bash, **JDK 21**, the **Android SDK** (platform 35, build-tools 35.0.0, NDK 27.2.12479018,
CMake 3.22.1) and `git`. Set `JAVA_HOME` and `ANDROID_HOME` if they are not in the default places.

```sh
git clone --recursive https://github.com/Ihor-Ihorevych/flipendo-droid
cd flipendo-droid
android/build-apk.sh                # builds android/dist/flipendo-droid.apk
android/build-apk.sh --install      # ... and installs it on the connected phone
```

The script fetches SDL3 once, applies the engine patches (including
[`0200-android-port.patch`](src/surreal-patches/0200-android-port.patch), the Android changes to the engine), builds the
native library and assembles a debug-signed APK without Gradle. The first build takes a few minutes. Details, controls and
troubleshooting: [docs/android.md](docs/android.md).

## How it relates to Flipendo

This repository is a fork of [kroplabeskidu/flipendo](https://github.com/kroplabeskidu/flipendo), which plays the game on
a PC and is where the engine work (KnowWonder's engine code, bug fixes, reverse engineering notes in `docs/re/`) happens.
The Android port tracks it: upstream is merged in regularly, and the Android-specific code lives in `android/`, in
`src/hp1/HP1Touch.cpp` and in the one patch above. For the desktop version, widescreen on Windows, the mods and the
community (Discord), see the upstream project and its [README](https://github.com/kroplabeskidu/flipendo#readme).
Developer docs for both: [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/](docs/README.md).

## Legal

Flipendo's own code is copyright 2026 the Flipendo authors and open source under the
[GNU General Public License, version 3](LICENSE); `src/engine/` is SurrealEngine (zlib licence). This is an independent
project, **not affiliated with or endorsed by SurrealEngine, EA, Warner Bros. or KnowWonder**; please report problems
here, not to SurrealEngine. It is a reimplementation written from studying how the original game behaves, so that the
game's own files run on a new engine, and it contains no Epic, EA or KnowWonder code or data.

Harry Potter is a trademark of Warner Bros. Entertainment. The game data is copyright EA / KnowWonder and is not
included.
