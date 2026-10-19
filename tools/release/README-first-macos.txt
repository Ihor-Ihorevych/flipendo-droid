Flipendo @TAG@ - Harry Potter and the Philosopher's / Sorcerer's Stone (PC, 2001) on a modern PC,
and an early beta of Chamber of Secrets (PC, 2002)
https://github.com/kroplabeskidu/flipendo

You need your own installed copy of the game. Flipendo contains no game data.

  Philosopher's Stone (HP1)  stable: the whole story, menus, saving and loading
  Chamber of Secrets (HP2)   beta: starts a new game and plays the opening up to the Whomping Willow;
                             no start menu, saving or voices yet

HOW TO START

Apple Silicon Macs (M1 and later).

1. Unzip, and move Flipendo.app wherever you like.
2. Copy the game folder from a Windows install or the disc (the one with System, Maps, Textures, ...).
3. The app isn't notarised by Apple, so the first start needs one extra step. In Terminal:
     xattr -dr com.apple.quarantine /path/to/Flipendo.app
   (or try to open it once, then System Settings > Privacy & Security > Open Anyway).
4. Start it from Terminal with the game folder:
     /path/to/Flipendo.app/Contents/MacOS/SurrealEngine ~/Games/HarryPotter

The game renders at the display's native (Retina) resolution.

If it doesn't start, the error message says which file or folder is missing. More help:
https://github.com/kroplabeskidu/flipendo/blob/master/docs/troubleshooting.md

LAUNCH OPTIONS (after the game folder)

  --skip-splash              skip the logo screens (HP1)
  --skip-intro               New Game goes straight to the first level (HP1)
  --vanilla                  turn off every extra the original doesn't have
  --logfile=flipendo.log     write a log file (attach it to bug reports)

Texture packs: unpack a pack made for the DirectX 11 renderer (Textures/<package>/<group>/<name>.dds,
e.g. Akven's ESRGAN Upscale Pack) into the game folder. Vulkan renderer only; off with --vanilla.

Bug reports: https://github.com/kroplabeskidu/flipendo/issues

LICENSES AND SOURCE

Flipendo is licensed under the GNU GPL v3 (LICENSE.txt). Full source code of this build:
https://github.com/kroplabeskidu/flipendo/tree/@TAG@

SurrealEngine is a MODIFIED build of SurrealEngine (https://github.com/dpjudas/SurrealEngine,
zlib license) with Flipendo's patches applied; it is not an official SurrealEngine release.
Flipendo.app/Contents/Frameworks holds libSurrealVideo (based on FFmpeg's libavcodec) and OpenAL Soft
(https://github.com/kcat/openal-soft), both under the GNU LGPL and replaceable with your own builds, and
MoltenVK (Apache 2.0).
Notices for all third-party code are in the licenses folder.
Sample rate converter designed by Aleksey Vaneev of Voxengo.

Not affiliated with or endorsed by EA, Warner Bros. or KnowWonder.
