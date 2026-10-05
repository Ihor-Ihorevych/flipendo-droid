# Collectibles: wizard cards, beans, chests (HP1)

From HP1's scripts (`HProps`, `HPBase`, `HPMenu`) and the actors in HP1's 41 maps (dumped with
`tools/uelib_dump props <map>.unr <out>`). HP2 has its own collectibles in `hgame`; not compared yet.

## Wizard cards

- Each card is a subclass of `HProps.WizzardCardIcon` (`WCBott`, `WCMerlin`, ...). The subclass only sets
  `WizardName` and `ID` in `PostBeginPlay` and the card face in its default `Skin`. Touching a card in state `Wait`
  calls `baseHarry.addcard(ID)`, which sets `bHasCard` on the matching entry of Harry's `travel`
  `WizardCards[25]`.
- 25 cards. 24 are collected in the levels; Harry's own card (`WCPotter`, ID 100) is added by
  `FEStoryBookPage.GotoNextPage` on the final storybook's second page (dialog `StoryBook31`) when Harry has all 24
  and at least 250 beans; otherwise that storybook skips its next pages.
- IDs: Merlin 1, Agrippa 2, Shimpling 8, Muldoon 10, Herpo 11, Fay 17, Oddball 18, Scamander 19, Waffling 24,
  Toke 28, Wright 35, Vablatsky 37, Gryffindor 41, Stroulger 47, Slytherin 48, Ketteridge 49, Ollerton 57,
  Wildsmith 62, Bott 69, Hufflepuff 72, Ravenclaw 82, Plumpton 83, Woodcroft 96, Dumbledore 101, Potter 100.
- Where the 24 come from:

  | How | Cards (map) |
  |---|---|
  | placed in the map | Merlin (Lev_Tut2), Toke (Lev_Tut3), Fay (Lev2_fire1), Ketteridge (Lev3_Dungeon), Woodcroft (Lev3_DungeonB), Vablatsky (Lev3_Lumos) |
  | `SpawnThingy.SpawnClass`, spawned when triggered (cutscenes, rewards) | Dumbledore (Lev_Tut1, the `WizardCardCut` cutscene), Oddball (Lev_Tut3b), Slytherin (Lev2_fire1), Gryffindor and Agrippa (Lev2_HogFront), Scamander (Lev2_Inc_A), Bott (Lev3_Intro), Wildsmith (Lev3_PreDungeon), Ravenclaw (Lev4_Sneak2) |
  | first object of a chest (`EjectedObjects[0]`) | Muldoon (Lev_Tut3), Waffling and Shimpling (Lev_Tut3b), Wright and Herpo (Lev2_Fire2), Stroulger (Lev2_Inc_B), Ollerton (Lev3_PreDungeon), Hufflepuff (Lev4_Sneak), Plumpton (Lev5_fluffy) |

- No script looks for a particular card: only `addcard`, `getNumCards`, the folio page (which lists the
  `WizardCards` entries Harry has) and `Tut2.BroomPracticeReferee` (hides every `WizzardCardIcon` when the broom
  lesson is replayed from the menu).

## Chests and beans

- `HProps.bronzechest` and its subclasses `WoodChest`, `IronChest`, `GoldChest` open with Alohomora
  (`TakeSpellEffect` in state `waitforspell` goes to `turnover`) and spawn `iNumberOfBeans` objects from
  `EjectedObjects[8]`. With `bRandomBeans` (the default), `SetupRandomBeans` replaces the list when the chest
  opens: random bean colours, and in slot 0 a chocolate frog with a chance that grows as Harry's health drops. A
  chest holding a card sets `bRandomBeans` false, so the card stays.
- 97 chests in total: Lev_Tut3 11, Lev_Tut3b 5, Lev2_fire1 13, Lev2_Fire2 10, Lev2_HogFront 11, Lev2_HogFront_2 1,
  Lev2_Inc_A 6, Lev2_Inc_B 7, Lev3_Dungeon 3, Lev3_DungeonB 5, Lev3_Intro 8, Lev3_Lumos 9, Lev3_PreDungeon 3,
  Lev4_Sneak 2, Lev4_Sneak2 1, Lev5_Final 1, Lev5_fluffy 1.
- Beans are a `travel` count on Harry (`baseHarry.numBeans`, `AddBeans`). Fred sells things for beans
  (`fred.merchant`, `salePrice`, `saleScene`), and Hub4's `TakeAllBeansTrigger` takes 25.

A randomizer was considered and dropped: the cards are the only thing worth shuffling (spells are taught in a fixed
order and gate the levels, other chests already roll random beans), which is too little for a randomizer.
