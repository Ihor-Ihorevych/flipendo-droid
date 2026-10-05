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

## HP1 and HP2

HP2's `FString` serializer in Core.dll is identical to HP1's ([hp2_compare.md](../reports/hp2_compare.md)).
