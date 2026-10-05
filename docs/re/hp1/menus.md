# HP1 menu book and level titles

How HP1's front end (the menu book) says what the player is doing, and where the game keeps its level titles. All
from HP1's scripts (`HPMenu`: `FEBook`, `FESlotPage`, `HPConsole`) and `.int` files; HP2 not checked. Used by the
Discord presence mod (`src/hp1/mods/DiscordPresence.cpp`).

## The menu book (FEBook)

`HPConsole.menuBook` is one `FEBook` for the whole session: the title menu, the pause menu, the save slots and the
storybooks are all pages of it (`MainPage`, `ReportPage`, `SlotPage`, `StoryBookPage`, ... ; `curPage` is the one
shown).

- **`bIsOpen`**: the book is on screen (`OpenBook` sets it, `CloseBook` clears it). `HPConsole` stops drawing the world
  while it is (`bNoDrawWorld = menuBook.bIsOpen`).
- **`bGamePlaying`**: a game is running, so an open book is the pause menu, not the title menu. New Game
  (`FESlotPage.CreateSelectedSlot`, which also starts story 3 with `Lev_Tut1.unr`) and Load
  (`LoadSelectedSlot`) set it; changing to `MainPage` and confirming Quit clear it. A Quidditch League match sets
  `bPlayingQuidditch` instead.
- **Escape in game** runs `menuBook.EscFromConsole()`, which opens the book on the `REPORT` page.
- A console `open <map>` from the title menu loads the level behind the still-open book, with `bGamePlaying`
  false: the title menu stays up over the new level.

## Level titles

The loading screen (`HPConsole.DrawLevelInfo`, raised by LoadMap: [engine/rendering.md](../engine/rendering.md#the-loading-screen-ugameengineloadmap-hp1-0x1039c3d0))
turns a map name into a title and an objective in two steps:

1. `Localize("text", "n_" $ map, "Dobby")` gives the level's two-digit index (`Dobby.int`, e.g.
   `n_lev3_lumos=20`);
2. `Localize("text", "level_name_" $ index, "HPMenu")` and `"objective_" $ index` give the title and objective
   (`HPMenu.int`, `level_name_01` .. `level_name_37`).

The Quidditch League maps (`Quid_*`, indices 40-48) and `Snapes_Office` (38) have indices but no title; the map name
is `Level.LevelEnterText` (the map the level was travelled to, kept in saves: [savegames.md](../engine/savegames.md)).
