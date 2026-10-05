# Cut and unused content (HP1)

What HP1's retail files contain that the game never uses. Found by checking every map's actors
(`tools/uelib_dump props <map>.unr`), every script (`reference/hp1/ScriptSource/`) and the raw bytes of every `.u` and
`.unr` (for defaults UELib prints as `()`, such as nested struct arrays). Not compared with HP2.

**Caveat:** "unused" means no map, script or package names it. `HPMenu.FESoundBrowser` (a debug sound browser) lists
almost every sound in `MasterList`, so it is left out of the count. Content reached only through a string built at
run time could be missed, but no script builds a dialogue or sound name that way (dialogue is loaded with
`DynamicLoadObject("AllDialog." $ dialogID)`, where `dialogID` is a full name from a map or a script).

## Summary

| What | Unused | Notes |
|---|---|---|
| Maps | 0 of 41 | every map is reached from another map's travel or from the menus (`FEQuidMatchPage` builds the `Quid_*` names) |
| Spells | 8 spell classes, 5 more spells only in the enum | below |
| Script classes | ~300 never placed and never named by a script | mostly mesh-import classes (`sk*`) and empty bases; the interesting ones are below |
| Dialogue (`AllDialog.uax`) | 527 of 1394 lines | 526 of them have subtitle text in `hpdialog.int` |
| Quidditch commentary (`Quidditch_Lee_Jordan.uax`) | 89 of 200 lines | mostly `_B`/`_C` alternate takes |

## Spells

`Actor.ESpellType` lists 18 spells, but HP1 teaches only Flipendo, Alohomora, Incendio, Lumos and Wingardium Leviosa.
`baseWand.ChooseSpell` and `spellTrigger` map the others:

- **Spell classes that exist but no map actor reacts to** (no `eVulnerableToSpell` in any map or class default, no
  `bAvifTarget`/`bVerdTarget` set in a map): `spellAvif` (Avifors), `spellFlint` (Flintifors), `spellVerd`
  (Verdimillious), `spellRepairo` (Reparo), `spellEcto` (Ectomatic), `spellTrans` (Transfiguration), `spellPuffapod`,
  `spellFlute`. `baseProps` and `baseChar` still have reaction code for Avifors, Flintifors, Verdimillious,
  Transfiguration and Reparo, and `Hog2.Snail`/`VenomousTent2` have commented-out Ectomatic/Verdimillious code.
  `baseWand.AllSpells` (an `exec` cheat) gives Avifors, Flintifors and Verdimillious.
- **Enum only, mapped to Alohomora:** Locomotor Wibbly, Nox, Petrificus Totalus, Vermillious, Mucor ad Nauseam.
- Spell effects for cut spells are in `HPParticle` but never spawned: `Petri_*` (Petrificus), `Repairo_*`, `Verd_Book`,
  `avif_book`/`avifors_wand`, `Flin_book`.

## Classes that are never used

- **Mechanics:** `HProps.mirrortarget` shrinks Harry (`state shrinkHarry`); its sounds `spellShrinkSound` /
  `spellUnShrinkSound` (the loose `Sounds/s_spell_shrink*.wav`) are commented out. `HProps.FloatingSpellBook` gives
  Harry a spell when touched. `HarryPotter.masterScroll` starts a 60 s puzzle timer and selects Avifors.
  `Hub4.InvisibilityCloak`, a cloak pickup (the sneak levels give the cloak with `TriggerToggleHasCloak` instead).
  `Hog1.transFrog`, a hopping frog for Transfiguration.
- **Characters:** `Hub2.h2barron` (633 lines: an attacking Bloody Baron with attack points, using Peeves' voice
  variables), `HarryPotter.BossTroll` (an earlier troll boss than `BossTrollChase`), `Neville`, `mcgonagall`,
  `mrsnorris`, `bat`, `Hog1.H202Hermione`/`H202Neville` (classroom 202; `H202Draco`, `H202Ron`, `H202Gargoyle` are
  used), `Tut1.CutFilch`, `Hub2.CutBroomHooch`.
- **E3 demo leftovers:** `Harrye3` (Harry with Alohomora), `demoHarry`, `E3Fred`, `E3George`, `E3Goyle`, `E3Knight`.
- **Props:** Great Hall (`GHallSortHat`, `GHallDumbleThrone`, `GHallBenchTable`), Potions class (`PotionsStudentCauldron`,
  `PotionsSupplyCase`, `PotionBeaker`), greenhouse (`GreenHouseWaterCan`, `GreenhouseCactus`, `GreenHouseFaucet`, ...),
  Peeves' thrown objects (`PeeveThrowBook`/`Pot`/`Spoon`), Voldemort challenge pillars and gargoyles
  (`VoldmortChallengePillar`, `voldChallengeBrokeGargoyle1-3`), `NorbertEgg`, `CrystalBall`, `Telescope`,
  `Sundial`, Quidditch balls as props (`QuidditchBludger`, `QuidditchQuaffle`, `GoldenSnitch`), `WizCracker` crackers.

## Dialogue

The groups with the most unused lines (subtitles from `hpdialog.int`):

| Group | Lines | Example |
|---|---|---|
| `harry_*` | 83 | "A Challenge Star!" |
| `STUDENT_*`, `student_*` | 58 | "I hear there's a Portrait Door that's hiding something special!" |
| `Emotive*` (Harry, Ron, Filch, Peeves, Quirrell, Snape, Flitwick, Hagrid) | ~100 | grunts and laughs |
| `WIZARDCARD*` | 20 | card names and descriptions read aloud ("Celebrated Seer and author of 'Unfogging the Future.'") |
| storybook | 20 | a re-recorded `storybook_new_*` set (the Forbidden Forest unicorn scene with extra half-lines, the Nimbus Two Thousand arriving); `STORYBOOK19` (Norbert carried up the tower), `STORYBOOK42` (Pomfrey), `STORYBOOK47`/`48`, `STORYBOOK51` (Hagrid leaving baby Harry at Privet Drive). The forest detention itself is told in the game (`STORYBOOK23`–`26`) |
| `147*` | 21 | final battle: Voldemort ("SEIZE HIM!", "KILL HIM, FOOL, AND BE DONE!"), Quirrell ("But... Master! It wasn't my fault...") |
| `HERMIONE_*`, `RON_*`, `hermione_new_*` | ~35 | hints and story lines |
| `111*` | 15 | first day at Hogwarts: Dumbledore ("Nitwit! Blubber! Oddment! Tweak!"), Malfoy, Ron and Hermione |
| `121*`, `124*`, `Hagrid_seeds_*` | 13 | Hagrid's Fire Seeds and dragon egg, Neville's Remembrall (alternate takes; the Fire Seed challenge itself is in the game) |
| `hootch_new_*`, `HOOCH_*`, `SPROUT_*`, `quirrell_lesson*`, `Flitwick_*` | ~35 | lesson instructions |
| `Extra42`–`45` | 4 | the four house names |

## Quidditch commentary

`Hub2.QuidCommentator` (spawned by `QuidditchReferee`) picks its lines from `Comments[29]`, each with variants per
house. 89 of Lee Jordan's 200 recordings are not in that table or in any map: mostly `127Commentary*_B`/`_C`
alternate takes and `127LeeJordan*` lines ("The Snitch is away!"), plus `Boo` and `WickedQuick`.
