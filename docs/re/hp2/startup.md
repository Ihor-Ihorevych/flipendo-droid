# Starting HP2: Game.exe's front end and the Startup map

Checked 2026-10-17 and 2026-10-20: HP2's `Game.exe`, the front-end classes and dialog templates in its `Window.dll`,
`Core.dll`'s `appInit`, `Engine.dll`'s `UGameEngine::Init`, `Startup.int`, `HPMenu.int` and `default.ini`. Flipendo's
version: `src/hp2/HP2FrontEnd.cpp`.

## Which Game.exe to read

The retail `Game.exe` is SafeDisc-wrapped like HP1's `HP.exe`, and its `.text` and `.data` are encrypted (entropy 7.4):
almost nothing of the front end can be read in it. The community No-CD `Game.exe` (SHA-1 `28b0e54a…`, listed in
`UE1GameDatabase.h`) has the same code decrypted; everything below about Game.exe comes from it (`[HP2 Game 0x...]`
addresses in the IDA tags; its image base is 0x10900000). `Window.dll` isn't wrapped.

## WinMain

Without `-SAVESLOT`, `-NOFRONTEND` (and `-LOG`, a server or a renderer test), `WinMain` [HP2 Game 0x1090B700]:

1. Shows `..\Help\EALogo<Language>.bmp` (`[Engine.Engine] Language`; there is none, so `EALogo1.bmp`) for
   `[FrontEnd] EASplashWaitTime` seconds, then `..\Help\WBLegal<Language>.bmp` (else `WBLegal.bmp`, the WB shield and
   the trademark notice) for `WBSplashWaitTime` (both 3 in `default.ini`). Both 640x480, 8-bit.
2. Starts the engine, whose init (`sub_1090EA00`) opens the front-end dialog when there's no `SAVESLOT`, `NOFRONTEND` or
   `LOADGAME` on the command line (`sub_10904040`). The dialog sets `[FrontEnd] LaunchCode=0` first.
3. After the run, reads `LaunchCode` and `UseSaveSlot`, sets `LaunchCode=0` again and starts **itself** again with
   `ShellExecute`: `1` → `PrivetDr.unr -SAVESLOT=<slot>` (`sub_10902BA0`), `2` → ` -LOADGAME=0 -SAVESLOT=<slot>`
   (`sub_10903290`). So the front end and the game are two processes; Flipendo goes on in the same one.

## The dialog (`WFrontEnd`, a `WFrontEndDialog`)

- `WFrontEnd::OnInitDialog` [HP2 Game 0x10911320] loads every bitmap from `..\Help` (`ButtonUp`, `ButtonDown`,
  `ButtonThumbUp`, `ButtonThumbDown`, `HP2Slider`, `HP2SliderKnob`, `CheckBoxOn/Off`, `RadioOn/Off`, `Background`;
  `ButtonDown.bmp` is never used) and checks the six **slots, numbered 1 to 6**: folder `SavePath * "Slot" + n`
  (`..\Save\Slot1`…), created if missing; empty when `Save0.usa` has no size. An empty slot's folder is emptied of
  every file (`sub_109124F0`); a used one's picture is its `Save0.bmp`.
- Template 152 (320x342 dialog units in MS Sans Serif 8): the page area (control 1061, 319x252), Back (3007) and OK
  (3000) under it, an Instructions label (3022) at the bottom. The Main Menu page sizes the window to 486x566 with its
  frame and centres it.
- Each page's OnCurrent puts the page title in the **window title** and its `IDC_Instructions` in the dialog's label,
  and sets the dialog's colour to COLORREF `0x090B0C` (RGB 12, 11, 9).
- Pages stack (`WFrontEndDialog::Advance` / `RefreshPage` / `OnBack`): Back shows only with a page under the current
  one, OK only when the page has an OK text (a button with no text is hidden, `WButton::SetVisibleText`). Default
  texts from `HPMenu.int` `[All]`: OK `Shared_Menu_0008` ("OK"), Back `Back_Button_0000` ("Back").
  `WFrontEnd::OnBack` plays a button sound when there is a page to go back to.

## The pages

| Page | Template | What it does |
| --- | --- | --- |
| Main Menu | 162 | New Game, Load Game, Options (40,124 / 166 / 208, 85x38) over `ButtonUp.bmp`; title label hidden. OK is `InGameMenu_0025` ("Quit"): a rollover sound played to the end, then the dialog closes |
| New Game | 163 | six slot buttons, text `"%d - %s"` (`1 - Empty` / `1 - Used`), over `ButtonThumbUp.bmp` or the slot's `Save0.bmp`; no OK. A click: rollover sound; a used slot asks `Select_Game_0001` "Are you sure you want to replace this game?" (title `Select_Game_0002` "Replace Game", Yes/No); the slot folder is emptied, button sound, `LaunchCode=1`, `UseSaveSlot=n`, end |
| Load Game | 163 | the same buttons; an empty slot is disabled over `ButtonThumbDown.bmp`. A used one: rollover sound, `LaunchCode=2`, `UseSaveSlot=n`, end |
| Options | 165 | Video, Sound, Joystick; title hidden, no OK. Joystick runs `control.exe joy.cpl` (Windows' game controllers) |
| Video | 161 | Direct3D / Software radios (`[Engine.Engine] GameRenderDevice`; Direct3D disabled when not detected; a click plays a button sound), the resolution list (640x480, 800x600, 1024x768, and 1280x1024 with `[Engine.Engine.Console] bDebugMode`; the current entry is the window size in a window, else the full screen size), "Run the game in a window" (`[WinDrv.WindowsClient] StartupFullscreen`); "Disable Pentium III/3DNow" shows only with bDebugMode. OK writes them |
| Sound | 159 | "Disable all sound" = `[Engine.GameEngine] UseSound=False`; Sound and Music Volume trackbars 0-100 (`[ALAudio.ALAudioSubsystem] SoundVolume` / `MusicVolume` × 100) with `HPMenu` `Options_0005` "Low" and `Options_0002` "High" under them; "Disable 3D sound hardware" hidden. OK writes them and plays a rollover sound |

## Window.dll's controls

- `WBitmapButton` (a `WCoolButton`): its bitmap centred, the text centred over it in white, also when disabled
  (`DT_CENTER | DT_VCENTER | DT_SINGLELINE`); pressed, both move down and right by one pixel. No hover image.
- `WBitmapCheckBox` / `WBitmapRadioButton`: the on or off bitmap at the left, the text 5 pixels after it.
- `WColorTrackBar`: a trackbar drawn with `HP2Slider.bmp` (200x24) and `HP2SliderKnob.bmp` (11x21).
- `LoadFileToBitmap` reads 8-bit BMPs only; `WinPlaySound` is `PlaySoundW(name, 0, flags | SND_FILENAME)`.

## Sounds

`..\Help\Button0-2.wav` at random (`sub_10903990`) when a page opens, on Back and on a renderer radio;
`Rollover0-1.wav` at random (`sub_10903A10`) when a slot is picked, on Quit and on the Video and Sound pages' OK. They
play with `SND_ASYNC | SND_NOSTOP` (nothing new starts while one plays), Quit's with neither (it waits for the end).

## Save slots: -SAVESLOT and -LOADGAME

- `Core.dll` `appInit` [HP2 Core 0x10156850]: `SavePath` (`USystem` offset 52, `..\Save` by default) is created; with
  `SAVESLOT=<n>` the save folder (offset 64) becomes `SavePath * ("Slot" + n)`, else it is `SavePath`. Logged as
  `Save Slot Path: %s`, created, and written back as `[Core.System] SavePath` (so `*.usa` in it is found as a package).
- `UGameEngine::Init` [HP2 0x103A0FA0]: after the first map, `LOADGAME=<n>` loads `<save folder>\Save<n>.usa` with
  `ServerTravel` ("Loading game file %s").
- `UGameEngine::SetThumbnailForSaveSlot` [HP2 0x103ABB00], when saving: copies `Thumbnails\<map>.bmp` (or
  `Thumbnails\Default.bmp`) to `<save folder>\Save0.bmp`.
- `UEngine::InitAudio` [HP2 0x1039A990]: no audio subsystem when `UseSound` is off or with `-NOSOUND`.
- The scripts only ever save and load save 0 ("the save game number that the front end will always look for is
  zero", PlayerPawn), so a slot is a folder, not a file number.

## The Startup map

Started without a URL, the engine loads `LocalMap=Startup.unr`: a test map (a blue grid with numbers, two idle
cutscenes) where Harry just stands. `HPConsole.RenderUWindow` creates the menu book on the first frame, `FEBook.Created`
opens it on `MAIN`, and RenderUWindow closes it again right away (`bGamePlaying = true; CloseBook()`). Nothing in
`hgame.u` calls `FEBook.OnLevelLoadDone`, the only code that shows `MainPage`. The in-game book (Esc) is the menu HP2's
engine has: load, save, options, quit. The scripts also return to `startup.unr` and open the book on `Main` when the
player quits a game (`FEBook.WindowDone`, `harry` travelling to a map named Startup). Load Game passes through it: the
map loads, then `-LOADGAME` travels to the save.

## In Flipendo

`HP2::RunFrontEnd` runs from `Engine::Run` when HP2 starts without a URL (`--url` and `--level` skip it). The splashes are
skipped by default (the splash skip in `src/hp1/mods/LaunchSkips.cpp`; `--vanilla` shows them). It draws the dialog itself with SurrealWidgets from the bitmaps and templates above, plays the
sounds through its own OpenAL device (the engine's isn't open yet), and on New Game / Load Game points the save folder
at `Save/Slot<n>` and sets the URL or `HP2::LoadGameSave()` in the same run. Differences from Game.exe:

- The resolution list is the common modes up to the desktop (`KW::ResolutionModes`), as Flipendo's in-game options
  page has; the chosen one is used for both the full screen and the window.
- Flipendo has one renderer: Direct3D stays selected, Software is disabled.
- Joystick is disabled (no `joy.cpl` outside Windows; SDL finds controllers by itself).
- "Disable all sound" plays everything at volume 0 instead of running without an audio device.
- A click or a key ends a splash early.
