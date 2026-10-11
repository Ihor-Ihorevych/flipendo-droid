<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="images/branding/flipendo-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="images/branding/flipendo-logo.png">
    <img src="images/branding/flipendo-logo.png" alt="Flipendo" width="560">
  </picture>
</p>

<p align="center">
  <b>Flipendo Droid: Harry Potter and the Sorcerer's Stone (2001) on your Android phone.</b><br>
  <sub>The original PC game, played with touch controls or a gamepad. Based on <a href="https://github.com/kroplabeskidu/flipendo">Flipendo</a>.</sub>
</p>

<p align="center">
  <a href="#what-you-need">What you need</a> ·
  <a href="#install">Install</a> ·
  <a href="#controls">Controls</a> ·
  <a href="#settings">Settings</a> ·
  <a href="#tips">Tips</a> ·
  <a href="#status">Status</a> ·
  <a href="#news-help-and-bug-reports">Help</a> ·
  <a href="https://t.me/flipendodroid">Telegram</a>
</p>

<p align="center">
  <img src="images/screenshots/grand-staircase.jpg" alt="The grand staircase in the first level" width="100%">
</p>

Play the **2001 PC game** *Harry Potter and the Philosopher's / Sorcerer's Stone* on a phone: the same levels, voices,
music and cutscenes, from the first day at Hogwarts to the final chamber. Nothing is remade; your own copy of the game
runs on a modern open-source engine, with controls made for a touch screen and support for gamepads.

**All levels are playable.**

## What you need

- **Your own copy of the game** (PC, 2001), installed on a computer: the folder with `System`, `Maps`, `Textures`, `Sounds`
  and `Music` in it. The app contains no game data. The US and UK releases work, and other language editions usually do;
  if yours is not recognised, tell us in the [Telegram group](https://t.me/flipendodroid).
- **An Android phone**: Android 10 or newer, 64-bit (almost every phone from the last few years).

## Install

1. **Get the app** (the APK) from the [Telegram group](https://t.me/flipendodroid), or [build it yourself](#build-it-yourself).
2. **Install it.** Open the APK on the phone and allow installing apps from that source when Android asks.
3. **Copy the game to the phone.** Connect the phone to the computer (file transfer mode) and copy the game folder to the
   phone, anywhere you like: internal storage or an SD card. The easiest place is a folder named **FlipendoHP** in the
   internal storage (so that it holds `FlipendoHP/System`, `FlipendoHP/Maps` and so on): the app finds it there by itself.
4. **Start Flipendo.** Allow **All files access** when it asks (the app needs it to read the game), then open it again.
   If the game isn't in FlipendoHP, tap **Choose folder** and pick the folder you copied (or the folder it is in).
   The game starts at the main menu, and the app remembers the folder.

If the folder you picked has no game in it, the app tells you and lets you pick another one.

## Controls

### Touch

| | |
|---|---|
| **Move** | a floating stick: touch the left half of the screen and drag. Or four arrow buttons (Settings), and you can slide your finger between them |
| **Look around** | drag on the right half of the screen |
| **Wand** | the big wand button (hold to aim, release to cast) |
| **Jump** | the jump button |
| **Menus** | tap what you want, your finger is the mouse |
| **Top right** | settings, save anywhere, menu (Esc) |
| **Cutscenes** | the controls hide; tap SKIP (top right) to skip |

### Gamepad (Xbox layout)

Just connect it (Bluetooth or USB). While you play with it, the on-screen buttons hide; touch the screen to get them back.

| Button | In the game | In menus | In a spell lesson |
|---|---|---|---|
| Left stick / d-pad | walk | move between items | |
| Right stick | camera | free cursor | moves the wand |
| A | jump, skip a cutscene | select | |
| LB / LT | jump | | |
| X / RB / RT (hold) | wand | | draw |
| Y | save anywhere | | |
| B / Start | menu | back | |
| View | settings | settings | settings |

In the settings panel the d-pad moves between options, A changes them, left / right moves a slider, B closes it.

## Settings

Open them with the gear (top right) or the View button on a gamepad. Everything is remembered.

| Setting | What it does |
|---|---|
| **Render scale** | how sharp the picture is. Lower is faster and saves battery; higher is sharper |
| **Subtitles and HUD size** | bigger or smaller subtitles and on-screen game info |
| **Field of view** | see more of the scene around Harry |
| **FPS counter** | shows frames per second next to the buttons |
| **Movement** | floating stick or arrow buttons |
| **Auto jump** | Harry jumps across gaps by himself (on by default) |
| **Debug mode** | adds Level Select to the main menu, to play any level |
| **Language / Movement mod** | shown only in builds that include other languages or the movement mod |
| **Telegram** | opens the group |

## Tips

- **Save anywhere** with the book button (or Y on a gamepad). The save goes into your current slot and shows in Start Game.
- **Spell lessons**: trace the glowing symbol with your finger (or the right stick while holding RT). The drawing is judged
  more gently than on the PC: what counts is that you went over the whole symbol, not how neat the line is.
- **Slow phone?** Lower the render scale in Settings, 40-50% runs smoothly on most phones.
- **Leaving the app** and coming back keeps your game running. If Android closes it in the background, you continue from
  your last save.

## Status

Every level can be played through, with menus, cutscenes, saving and loading, spell lessons, music and voices.

Not yet: casting spells in fights isn't specially adapted to touch (it works with the wand button), and the controls page of
the game's own Options shows empty rows (the keys can't be changed on a phone anyway).

### Tested phones

| Phone | |
|---|---|
| Xiaomi POCO X6 Pro | runs at about 60 fps |
| Redmi Note 13 Pro | works |
| Redmi 13C | works |
| Samsung Galaxy S23, S25 | works |

Tried it on another phone? Tell us in the [Telegram group](https://t.me/flipendodroid), whether it works or not.

## News, help and bug reports

New builds, news and help are in the **Telegram group: [t.me/flipendodroid](https://t.me/flipendodroid)**.

**Something broke?** Hold the menu button (top right) for a second and a half: the app makes a report and lets you send it
with any app (Telegram, mail...). If the game crashed, the report is offered by itself the next time you start it. Post it
in the group with a few words about what happened.

## Build it yourself

For developers: on Windows with Git Bash, JDK 21 and the Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018,
CMake 3.22.1):

```sh
git clone --recursive https://github.com/Ihor-Ihorevych/flipendo-droid
cd flipendo-droid
android/build-apk.sh                # android/dist/flipendo-droid.apk
```

How the port works, what it changes and every build option: [docs/android.md](docs/android.md).

## About

This is the Android port of [Flipendo](https://github.com/kroplabeskidu/flipendo), which plays the game on a PC (widescreen,
modern renderers, mods) on top of [SurrealEngine](https://github.com/dpjudas/SurrealEngine), an open-source
reimplementation of Unreal Engine 1. Improvements to the game itself come from Flipendo and are merged in regularly.

## Legal

Flipendo's own code is open source under the [GNU General Public License, version 3](LICENSE); SurrealEngine is under the
zlib licence. This is an independent fan project, **not affiliated with or endorsed by SurrealEngine, EA, Warner Bros. or
KnowWonder**; please report problems here, not to SurrealEngine. It contains no Epic, EA or KnowWonder code or data.

Harry Potter is a trademark of Warner Bros. Entertainment. The game data is copyright EA / KnowWonder and is not included:
you need your own copy of the game.
