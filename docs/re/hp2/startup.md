# Starting HP2: Game.exe's front end and the Startup map

Checked 2026-10-17 in HP2's retail `system/` (strings of `Game.exe`, `Startup.int`, `default.ini`) and its scripts.

HP2's main menu is not in the engine. `Game.exe` (SafeDisc-wrapped like HP1's `HP.exe`) has its own Windows dialog
(`WFrontEndDialog` / `WFrontEndPage`): the EA splash (`EALogo1.bmp`, `[FrontEnd] EASplashWaitTime=3`), then pages drawn
with the bitmaps in `../Help/` (`Background.bmp`, buttons, sliders, check boxes). Its text is in `Startup.int`:

- Main Menu: New Game, Load Game, Options
- New Game / Load Game: save slots ("Empty" / "Used", `Save0.usa`, `Save0.bmp` ...)
- Options: Video (Direct3D or software renderer, window, resolution, no PIII/3DNow), Sound (disable all, disable 3D
  hardware, sound and music volume), Joystick

Only then does it start the engine, with `PrivetDr.unr` for a new game or `-LOADGAME=<slot>` to load one (HP2's
Engine.dll reads `LOADGAME`). `[FrontEnd] LaunchCode` / `UseSaveSlot` in the ini are its other settings.

Started without a URL, the engine loads `LocalMap=Startup.unr`: a test map (a blue grid with numbers, two idle
cutscenes) where Harry just stands. `HPConsole.RenderUWindow` creates the menu book on the first frame, `FEBook.Created`
opens it on `MAIN`, and RenderUWindow closes it again right away (`bGamePlaying = true; CloseBook()`). Nothing in
`hgame.u` calls `FEBook.OnLevelLoadDone`, the only code that shows `MainPage`. The in-game book (Esc) is the menu HP2's
engine has: load, save, options, quit. The scripts also return to `startup.unr` and open the book on `Main` when the
player quits a game (`FEBook.WindowDone`, `harry` travelling to a map named Startup).

Flipendo has no front end for HP2 yet: a run without a URL starts a new game (`HP2::NewGameURL`, Engine.cpp), as
New Game would.
