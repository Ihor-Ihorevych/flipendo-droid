<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="images/branding/flipendo-logo-dark.png">
    <source media="(prefers-color-scheme: light)" srcset="images/branding/flipendo-logo.png">
    <img src="images/branding/flipendo-logo.png" alt="Flipendo" width="560">
  </picture>
</p>

<p align="center">
  <b>Flipendo Droid: Harry Potter and the Sorcerer's Stone (2001) and the Chamber of Secrets (2002) on your Android phone.</b><br>
  <sub>The original PC games, played with touch controls or a gamepad. Based on <a href="https://github.com/kroplabeskidu/flipendo">Flipendo</a>.</sub>
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

Play the **2001 and 2002 PC games** *Harry Potter and the Philosopher's / Sorcerer's Stone* and *Harry Potter and the
Chamber of Secrets* on a phone: the same levels, voices, music and cutscenes. Nothing is remade; your own copy of a game
runs on a modern open-source engine, with controls made for a touch screen and support for gamepads. One app plays both:
pick the game on the start screen.

- **Harry Potter 1**: all levels are playable.
- **Harry Potter 2**: playable from the first scene on, with saves, duels and shops; still being polished (see
  [Status](#status)).

## What you need

- **Your own copy of the game(s)** (PC), installed on a computer: the folder with `System`, `Maps`, `Textures`, `Sounds`
  and `Music` in it. HP1 is `System/HP.exe`, HP2 is `System/Game.exe`. The app contains no game data. The US and UK
  releases work, and other language editions usually do; if yours is not recognised, tell us in the
  [Telegram group](https://t.me/flipendodroid).
- **An Android phone or tablet**: Android 10 or newer, 64-bit (almost every phone from the last few years).

## Install

1. **Download the app**: `flipendo-droid.apk` from the newest release at the top of the
   [Releases](https://github.com/Ihor-Ihorevych/flipendo-droid/releases) page (open the link on the phone, or
   download it on a computer and copy it over).
2. **Install it.** Open the APK on the phone and allow installing apps from that source when Android asks.
3. **Copy the game to the phone.** Connect the phone to the computer (file transfer mode) and copy the game folder to the
   phone, anywhere you like: internal storage or an SD card. The easiest places are a folder named **FlipendoHP**
   (Harry Potter 1) and **FlipendoHP2** (Harry Potter 2) in the internal storage, so that each holds its own `System`,
   `Maps` and so on: the app finds them there by itself.
4. **Start Flipendo.** Allow **All files access** when it asks (the app needs it to read the games), then open it again.
   On the start screen pick **Harry Potter 1** or **Harry Potter 2**. If that game isn't in its folder, tap **Choose folder**
   and pick the folder you copied (or the folder it is in); each game remembers its own folder.
5. **Pick what to play** (HP2): *New game*, or one of six save slots with the picture of the level you were in. Then
   *Play*.

If the folder you picked has no game in it, the app tells you and lets you pick another one.

## Controls

The first-time help is in the game: Settings, then **? How to play**.

### Touch

| | |
|---|---|
| **Move** | a floating stick: touch the left half of the screen and drag. Or four arrow buttons (Settings), and you can slide your finger between them |
| **Look around** | drag on the right half of the screen |
| **Wand** | the big wand button (hold to aim, release to cast) |
| **Jump** | the jump button. In an HP2 duel it also switches the spell |
| **Menus, shops** | tap what you want, your finger is the mouse |
| **Top right** | settings, save anywhere, menu (Esc) |
| **Cutscenes** | the controls hide; tap SKIP (top right) to skip (on a gamepad hold Y) |
| **Potion (HP2)** | tap the screen with two fingers at once: drinks a Wiggenweld potion |

You can move and resize the buttons: Settings, **Control layout**.

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
| Stick click (HP2) | drink the Wiggenweld potion | | |
| View | settings | settings | settings |

In the settings panel the d-pad moves between options, A changes them, left / right moves a slider, B closes it.

## Settings

Open them with the gear (top right) or the View button on a gamepad. Everything is remembered.

| Setting | What it does |
|---|---|
| **? How to play** | a card with what every control does |
| **Render scale** | how sharp the picture is. Lower is faster and saves battery; higher is sharper |
| **Subtitles and HUD size** | bigger or smaller subtitles and on-screen game info |
| **Field of view** | see more of the scene around Harry |
| **FPS counter** | shows frames per second next to the buttons |
| **Movement** | floating stick or arrow buttons |
| **Auto jump** | Harry jumps across gaps by himself (on by default) |
| **Invert wand** | flips the up/down aim of the wand button while the spell is held |
| **First person** | the camera in Harry's head while he walks about (cutscenes, lessons, duels and shops keep the normal camera) |
| **Control layout** | drag the wand, jump, arrow pad and toolbar where you want them and resize them (60-180%); Reset puts them back |
| **Debug mode / Debug menu** | HP1: adds Level Select to the main menu. HP2: a menu to go to any level, set the story state or get all spells |
| **Renderer** | OpenGL ES (the default) or Vulkan, also on the start screen before the game loads. Try the other one if the game is slow or looks wrong; from Vulkan it switches to OpenGL ES by itself if Vulkan does not start |
| **FPS limit** | 25 to 60 frames a second: lower saves battery and keeps the phone cooler |
| **Language** | HP1: English or Russian text; *Добавить озвучку из папки…* adds a dubbing from its Sounds folder (such as the Russian voices) |
| **Movement mod** | HP1: AdamJD's mod for better movement, camera and climbing: tap *Choose file* and pick its zip (download it from the mod's page), then switch it on or off here |
| **Play HP1 / Play HP2** | in the header: restarts straight into the other game (tap twice; unsaved progress is lost) |
| **Telegram** | opens the group |

## Tips

- **Save anywhere** with the book button (or Y on a gamepad). The save goes into your current slot and shows on the start
  screen (HP2: in the slot list).
- **Spell lessons**: trace the glowing symbol with your finger (or the right stick while holding RT). The drawing is judged
  more gently than on the PC: what counts is that you went over the whole symbol, not how neat the line is.
- **Slow phone?** Lower the render scale in Settings, 40-50% runs smoothly on most phones.
- **Leaving the app** and coming back keeps your game running. If Android closes it in the background, you continue from
  your last save.

## Status

**Harry Potter 1**: every level can be played through, with menus, cutscenes, saving and loading, spell lessons, music and
voices.

**Harry Potter 2**: the story runs from the first scene on, with saves (six slots), shops, duels, Russian text, subtitles and
gamepad support. Known rough spots: the light of Lumos is not exactly on the wand's tip, some translucent walls look red,
and things the game spawns (jelly beans) do not always follow Harry to the next level. Report what you find.

Not yet: the controls page of the game's own Options shows empty rows (the keys can't be changed on a phone anyway).

### Tested phones

| Phone | |
|---|---|
| Xiaomi POCO X6 Pro | runs at about 60 fps |
| Redmi Note 13 Pro | works |
| Redmi 13C | works |
| Samsung Galaxy S23, S25 | works |

Tried it on another phone? Tell us in the [Telegram group](https://t.me/flipendodroid), whether it works or not.

## News, help and bug reports

New versions are on the [Releases](https://github.com/Ihor-Ihorevych/flipendo-droid/releases) page; news and help are in the
**Telegram group: [t.me/flipendodroid](https://t.me/flipendodroid)**. To update, install the new APK over the old one: your
saves and settings stay.

**Something broke?** Hold the menu button (top right) for a second and a half: the app makes a report and lets you send it
with any app (Telegram, mail...). If the game crashed, the report is offered by itself the next time you start it. Post it
in the group with a few words about what happened.

## Build it yourself

Players don't need this: the [Releases](https://github.com/Ihor-Ihorevych/flipendo-droid/releases) page has the APK.
For developers: on Windows with Git Bash, JDK 21 and the Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018,
CMake 3.22.1):

```sh
git clone --recursive https://github.com/Ihor-Ihorevych/flipendo-droid
cd flipendo-droid
android/build-apk.sh                # android/dist/flipendo-droid.apk
```

How the port works, what it changes and every build option: [docs/android.md](docs/android.md).

## About

This is the Android port of [Flipendo](https://github.com/kroplabeskidu/flipendo), which plays the games on a PC (widescreen,
modern renderers, mods) on top of [SurrealEngine](https://github.com/dpjudas/SurrealEngine), an open-source
reimplementation of Unreal Engine 1. Improvements to the games themselves come from Flipendo and are merged in regularly.

## Legal

Flipendo's own code is open source under the [GNU General Public License, version 3](LICENSE); SurrealEngine is under the
zlib licence. This is an independent fan project, **not affiliated with or endorsed by SurrealEngine, EA, Warner Bros. or
KnowWonder**; please report problems here, not to SurrealEngine. It contains no Epic, EA or KnowWonder code or data.

Harry Potter is a trademark of Warner Bros. Entertainment. The game data is copyright EA / KnowWonder and is not included:
you need your own copy of the games.
