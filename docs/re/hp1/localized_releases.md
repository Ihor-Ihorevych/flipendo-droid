# Localized releases

What differs in HP1's non-English releases, and what Flipendo needs for them. Checked: the Russian release.

## Russian (EA Hit-Parade)

The rutracker release of the EA Hit-Parade disc image (`HPPS_Russian.mdf`, files dated 2001-10-29, disc made
2005-11-08). Files are on the disc as they are installed (System/, Maps/, Textures/ ...; no installer archives).

- `System/HP.exe` SHA-1 `5d7ff458f6a9c3f6ff70725ecb3dada3b320998e` (SafeDisc 4.60), the No-CD exe shipped with it
  `7d5664c62634596fecf09013aa1aaf902a50cb27`. Both are in the game database.
- Every DLL and `.u` package differs from the English retail files: a separate build of the same engine (433,
  Unicode, packages version 76). The Russian Engine.dll's WrappedPrint and Core.dll's appLoadFileToString are the
  English code; no character conversion was added.
- `Default.ini` has `Language=rus`; the text is in UTF-16 `.rus` files (HP.rus, hpmenu.rus, hpdialog.rus ...) next
  to a few `.int` files. The English `.int` files are ASCII. SurrealEngine's ini reader didn't decode UTF-16, so every
  Russian string came back empty.
- Its HPMenu.u bytecode switches HPConsole.SetupLanguage on `RUS`, `RU1` .. `RU5` and sends them to the system font
  branch (Console.CreateNativeFont with SAPFont.rus: MS Sans Serif, 24/16/10/12). The script source embedded in
  the same package doesn't have these cases: the source text is older than the compiled code. The bitmap fonts
  (BigInkFont ...) are the English ones, Latin-1 only.
- Object.GetLanguage returns the Language setting (`int`, `rus`); SurrealEngine returned `en`. The English game
  needs `INT` too: HPConsole picks its INT fonts on it, and baseWand only plays Harry's spoken incantations for INT/ENG.

System fonts: [../engine/fonts.md](../engine/fonts.md).
