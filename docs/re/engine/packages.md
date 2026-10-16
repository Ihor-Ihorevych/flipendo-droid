# Package files

What HP1's packages (`.u`, `.unr`, ...) store that SurrealEngine's reader didn't expect. Checked in HP1's Core.dll.

## Strings (`operator<<(FArchive&, FString&)` [HP1 Core 0x10150830])

A string is a compact index count, terminator included (0 = empty), then the characters. Both games are Unicode
builds, so the writer checks the string first: if every character fits in a byte, the count is positive and each
character is one byte (Latin-1); if any character is above 0xFF, the count is negated and each character is two
bytes (UTF-16). Saving the other way round, a character above 0xFF written as one byte becomes 127.

SurrealEngine only read the one-byte form and stopped with `ObjectStream::ReadString: Invalid size` on a negative
count. Lev_Tut3b has such a string, so the level change from Lev_Tut3 crashed. Flipendo (engine fixes patch, any game)
reads the UTF-16 form and keeps the string as UTF-8.

## Language files (`appFindPackageFile` [HP1 Core 0x10153F40])

A package can have a language file next to it: the language code goes before the extension (`AllDialog.int_uax`,
`MenuArt.int_utx`, `AllEmote.eng_uax` in HP1's retail folders). For each Paths folder the original tries the plain
name first (`AllDialog.uax`) and takes it when it exists. Only when it's missing does it try
`<name>.<language>_<ext>`, and only for a language other than `int`. An English game never loads the `int_` files.
HP1's `AllDialog.int_uax` is a different, smaller file than `AllDialog.uax`.

SurrealEngine (since its 2026-10-14 localization change) prefers the language file whenever one exists, and built its
path with the folder twice, so HP1's dialogue failed to load. Flipendo keeps the plain file first for KnowWonder games
(core patch) and fixes the path for every game.

## HP1 and HP2

HP2's `FString` serializer in Core.dll is identical to HP1's (the HP1 vs HP2 code comparison), and so is
`appFindPackageFile` (same code, other addresses). HP2's retail folders carry language files for many
languages (`MenuArt.bra_utx`, `MenuArt.ger_utx`, ...).
