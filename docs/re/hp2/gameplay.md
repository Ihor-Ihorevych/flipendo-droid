# HP2 gameplay scripts: what differs from HP1

HP2's gameplay package is `hgame` (HP1's are `HarryPotter`, `HPBase`, `HPMenu`, ...). Its `.u` files ship with most
source stripped, so these notes come from the scripts UELib decompiles into
HP2's extracted scripts (natives show up there as `__NFUN_<index>__`). The shared engine underneath is in
[`../engine/`](../engine/); the bytecode difference is in [scripting.md](../engine/scripting.md#bytecode-hp1-vs-hp2).

First notes, not yet a full study. Each entry says what HP1 does instead.
Engine-side HP2 features (GameState, Lumos surfaces, ...) are in [engine.md](engine.md).

## Cutscenes

HP1's cutscenes are command lists stored in the map ([hp1/cutscenes.md](../hp1/cutscenes.md)). HP2's `CutScene`
(hgame) reads them from text files instead: with `FileName` set, `CreateThreads` loads up to 20 threads (`thread_<n>`
sections) from `System/cutscenes/<FileName>.int` (214 files on the retail disc) through `Localize` and
`CutScriptDisk.load`; otherwise from the `aThreadScripts[20]` strings. It starts on bump, trigger or level load
(`bBumpStarts`, `bTriggerStarts`, `bLevelLoadStarts`), and has its own skip: state `FastForwarding`, allowed when
`bSkipAllowed` (default true). HP1 has no skip (Flipendo adds one as a mod).

The skip is Enter (`Enter=button bSkipCutScene` in `DefUser.ini`): `harry.PlayerInput` calls
`HPConsole.StartFastForward` while the HUD is in cutscene mode, which fast-forwards every playing CutScene. While any
is still fast-forwarding, `HPConsole.HandleFastForward` sends `MUTESOUNDS` every tick, then `UNMUTESOUNDS`. Those are
ALAudio.dll's console commands (`UALAudioSubsystem::Exec` [HP2 ALAudio 0x100067B0]): a muted flag and every active
sound source's gain set to 0, then given back; music isn't touched. Flipendo holds the sound volume at 0 while muted
(`HP2::AudioCommand`, `src/hp2/HP2Music.cpp`). Checked 2026-10-17: Enter during PrivetDr's intro finishes it and
`00001PrivetIntroPart2` starts.

The package name `Localize` gets is `"Cutscenes\\" $ FileName`: a Windows path, and the retail folder is lowercase
`cutscenes`. SurrealEngine looked for a file literally named that on Linux, read every script as empty, and each
cutscene went to `Finished` the moment it started (PrivetDr's intro did nothing). `PackageManager::Localize` now
converts the separators and matches the file name ignoring case (`0100-knowwonder-core.patch`, any game).

Which cutscenes may start also depends on the story state: a CutScene outside the current GameState disables itself in
`OnResolveGameState`. Without GameState screening ([engine.md](engine.md#gamestate-actors-screened-by-the-story-state))
that event never comes and every `bLevelLoadStarts` cutscene in the map starts (PrivetDr ran `00001PrivetIntro` and
`00001PrivetIntroPart3` at once). Flipendo screens actors since 2026-10-17. `ParseCommand` also relies on `switch`
ignoring case ([scripting.md](../engine/scripting.md#switch-on-strings-ignores-case)).

## Accessed None from the scripts themselves

These show in every run and come from HP2's own scripts, so the original hits them as well (UE1 logs and goes on):

- `harry.PreBeginPlay`: `HPConsole(Player.Console).menuBook` before the pawn is possessed (Player is None).
- `FESoundVideoPage.Created`: the volume labels' text reads `MusicVolumeSlider.Value` / `SoundVolumeSlider.Value`
  before the sliders are created.
- `baseWand.StopChargingSpell`: `fxChargeParticles.Shutdown()` without a check, through `harry.StopAiming` when
  nothing is charging.
- `harry.PlayFootStep`: `HearHarryRecipient.PawnHearHarryNoise()` with no recipient.
- `StatusGroupWizardCards.RemoveHarryOwnedCardsFromLevel(None)` (from `harry.TravelPostAccept`): reads
  `wcExceptThisInstance.Name`.
- `FEInputPage.BeforePaint`: `InQuidditch_KeyButtons[I]` entries that were never created.
- `GameInfo.ScoreKill`: a pawn without a PlayerReplicationInfo that dies ([engine/scripting.md](../engine/scripting.md#an-out-parameter-through-a-none-context)).

`harry.CutCommand` (`CutScript.Running.Tick`) follows the "previous CutCommand hasnt finished yet" cutscene errors and
belongs with them. So does `baseWand.Tick`'s `fxChargeParticles.bEmit` before the wand has charged once.

The same goes for `Pawn.CreateEyeBlinkAnimChannel`'s "Couldn't create EyeBlinkAnimChannel" errors (from
`PostBeginPlay` with `bDoEyeBlinks`): the meshes don't have what it asks for (checked 2026-10-20 by listing their
bones in Flipendo). `skFilchMesh`, `skMrsNorrisMesh` and `skbloodybaronMesh` have no `eye_root` bone;
`skdobbyMesh`, `skfatfriarMesh` and `skpinkladyMesh` have the bone but no `eye_close` animation, and no HP2 script links another animation
(`LinkSkelAnim`) to them. `BoneNumber` and `HasAnim` differ from HP1's only by the DebugInfo check, and the characters whose meshes have
both (Harry, Hermione, ...) blink.

## Saving

`PlayerPawn.SaveGame()` only queues a save for the end of the level tick; the slot page reads save number 0 from a
directory per slot; `LoadGame 0` reloads after death. Details: [savegames.md](../engine/savegames.md#hp2s-flow-hgame-scripts).

## Spells

HP1's `Target` / `SpellLearnTrigger` ([hp1/spells.md](../hp1/spells.md)) are gone. HP2 aims with `SpellCursor` (a
ParticleFX that shows the spell's `GestureSprite`), teaches with `SpellLessonTrigger` (with `SpellLessonShape`,
`SpellLessonWand`, `SpellLessonInterpolationPoint`) and has duelling spells (`spellDuelExpelliarmus`,
`spellDuelMimblewimble`, `spellDuelRictusempra`) and `SpellChallengeTrigger`. None of them calls the Gesture natives
([gestures.md](../engine/gestures.md)). Not studied yet.

The spell symbol on a target (a chest's keyhole for Alohomora) is `SpellCursor.SpellGesture`, a `GestureSprite`: a plain
translucent sprite (`Sprite`, DT_Sprite) whose Texture is the spell's WetTexture (`SpellShapes.SpellFX.AlohomoraWet1`,
`FlipendoWet1`, ...), shown by `TurnOnSpellGestureFX`. The symbols aren't symmetric top to bottom, so they showed that
SurrealEngine drew every sprite upside down ([rendering.md](../engine/rendering.md#sprites)).

## Moving and looking while casting

`PlayerPawn.PlayerInput` (KnowWonder's `Engine.u`): while `PlayerIsAiming()`, forward/back (`aBaseY`) aim the spell up and
down instead of moving Harry (`aForward = 0`) when `bKeyAltFire` is set or `MoveWhileCasting()` is false.
`harry.MoveWhileCasting()` is true when AltFire isn't held and `bMoveWhileCasting` otherwise, and `harry.PlayerInput`
overwrites `bMoveWhileCasting` every frame AltFire is held: true if a mouse button is down (`GetCurrentKeyState` of
LeftMouse, RightMouse or MiddleMouse), false if not. So casting with the mouse lets Harry walk, and casting with a key
(`Alt=AltFire`) makes forward/back aim. The menu's Move While Casting checkbox (`FEInputPage.MoveWhileCastingChanged`) sets
the same variable, so it has no lasting effect; `SaveConfig` writes whatever the last frame left in `[HGame.Harry]`. On macOS
the window didn't report mouse buttons as held, so every cast counted as a keyboard cast (fixed in `0003-cursor-focus`).

Looking up and down while walking: `PlayerWalking.PlayerMove` zeroes `aLookUp`; the camera (`BaseCam.ApplyMouseYToDestPitch`)
pitches from `SmoothMouseY` instead. While Harry walks forward and isn't aiming, `PlayerMove` calls `Cam.SetPitch(-1500)`
every frame if `bAutoCenterCamera` is set (Options > Input, "Auto Centre Camera", on in `DefUser.ini`), so the mouse can't
change the pitch then. `FEInputPage.HideWindow` saves it with `SaveConfig`. Its `set ini:HGame.Harry <prop> <value>`
console commands (and the Difficulty one) fail in the original too: Core's `UObject::StaticExec` [HP2 Core 0x101601A0]
(GET / SET) finds the class with `StaticFindObject(UClass, ANY_PACKAGE, name)`, whose `UObject::ResolveName` [HP2 Core
0x10161DB0] reads `ini:A.B.C` as ini section `A.B`, key `C` of the system ini, so `ini:HGame.Harry` asks for
`[HGame] Harry`, which no HP2 ini has: no class, "Unrecognized class ini:HGame.Harry", nothing set. The same goes for the
options page's `get ini:Engine.Engine GameRenderDevice` (`[Engine] Engine`), which returns nothing. Flipendo fails the
same way (logged as "Could not find class 'None'"). The settings are kept anyway: the input options by the
`SaveConfig`, the difficulty and object detail as the player's `Difficulty` / `ObjectDetail`, which the pages set directly and the save game keeps.

## Collision

Two new collide types, `CT_AlignedOvalCylinder` and `CT_OrientedOvalCylinder` ([collision.md](../engine/collision.md#hp1-and-hp2)).

## Engine differences (not checked)

Not read in HP2's code yet. Check each one in `../ida/hp2/` before relying on it.

- **Sounds** are a modified Maxis XA format (an ADPCM), not plain wav; a decoder is ~150 lines.
- **Saves** are not loaded with `load saveN` as in every other UE1 game (fits the queued saves above).
- **Lip sync** is precomputed when a sound is imported: at runtime a sound gives one 0-1 mouth value. HP1 has none
  (no script mentions it, no Engine.dll function is named for it).
- **Movers** collide with the world by their bounding box, not only GridMover (the Flipendo blocks) as in HP1
  ([collision.md](../engine/collision.md)); that breaks hollow movers.
- **Mover flickering**: HP2 retail has Epic's fix for it.
- **Script folders**: the script compiler searches class subfolders one level deep (no effect at runtime).

## HP2-only natives

`Actor.BoneRot` (328), `IsSoftwareRendering` (329), `GetCurrentKeyState` (330) (`src/hp2/HP2Natives.cpp`), the OpenAL / Ogg Vorbis
music natives (`PlayMusic`, `StopMusic`, `StopAllMusic`, not ported), and the changed signatures of `StopSound`
(`FadeOutTime`) and `CreateAnimChannel` (`bNotReplaceable`).
