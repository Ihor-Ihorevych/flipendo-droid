# Fonts

How HP1's Engine.dll stores fonts and turns characters into glyphs, and the system fonts of WinDrv.dll's Windows
viewport (Console.CreateNativeFont). Reversed in HP1's Engine.dll and WinDrv.dll.

**HP1 and HP2.** Not checked in HP2 yet. `UViewport::CreateNativeFont` is identical in HP2's Engine.dll (the HP1 vs
HP2 code comparison); HP2's WinDrv.dll and UFont::Serialize haven't been read.

## UFont in the package

`UFont::Serialize` [HP1 0x10394610] reads, after UObject's properties:

1. `Pages`: a TArray of (Texture, TArray of characters: StartU, StartV, USize, VSize as 32-bit ints).
2. `CharactersPerPage` (32 bits), asserted to be a power of two.
3. From package version 69 on: `CharRemap`, a map of 16-bit character to 16-bit character (an index count, then
   key/value pairs), and `IsRemapped` (32 bits). Older packages get IsRemapped = 0.

SurrealEngine stopped after CharactersPerPage. HP1's own fonts (the INT and the Russian release) have an empty map
and IsRemapped = 0; every font in HPMenu.u has 256 characters (8 pages of 32 or one of 256) with Latin-1 glyphs.

## Character to glyph

`UCanvas::WrappedPrint` [HP1 0x10387C50] (and `UFont::RemapChar` [HP1 0x10318D30]):

- With IsRemapped, the character goes through CharRemap; one missing from the map becomes a space (32).
- Page = c / CharactersPerPage, index = c % CharactersPerPage. A page or index out of range, or a glyph of width 0:
  a lower-case a-z is tried again as upper case, anything else draws nothing.
- Strings are UTF-16, so the character is the code point. Flipendo's strings are UTF-8 and the canvas walks them by
  code point (`UFont::Codepoints`); walking bytes drew no non-ASCII character.
- A font with a system font handle (UFont +84 non-zero) isn't drawn from glyphs at all: WrappedPrint hands the whole
  string to the viewport's DrawString (vtable +152).
- When the font is the canvas's SmallFont or MedFont (+112/+116) and the language isn't INT, WrappedPrint and
  execDrawTextClipped draw with the font at +108 instead. Not ported.

## Centred text (Canvas.bCenter)

`UCanvas::WrappedPrint` [HP1 0x10387C50], called by `execDrawText` with Canvas.bCenter:

- Each line is centred on **CurX**: it starts at `(int)(CurX - (int)width / 2)` (OrgX added when drawing,
  `sub_10388530`). SurrealEngine centred lines between OrgX and OrgX + ClipX and added CurX on top.
- Wrapping measures from 0 against ClipX (not from CurX), and CurX goes back to its value after every line; then
  `execDrawText` adds the widest line to CurX (and with CR, CurX 0 and CurY on as usual).
- HP1's loading screen relies on it: `HPConsole.DrawLevelInfo` sets OrgX 0.15, ClipX 0.7 and CurX 0.35 of FrameX, so
  the level title and objective are centred on the screen; with SurrealEngine's rule they ran off the right edge.
- `execStrLen` (`WrappedStrLenf`) never centres, and `execDrawTextClipped` doesn't read bCenter at all. SurrealEngine's
  DrawTextClipped still centres with bCenter (not changed; no HP case seen).

Flipendo: `KW::CenteredTextLineX` (`src/knowwonder/KWCanvas.cpp`), used by RenderSubsystem::DrawText for both games.

## System fonts (Console.CreateNativeFont)

`UConsole::execCreateNativeFont` calls the viewport's CreateNativeFont (vtable +156); Engine.dll's base UViewport
returns null. WinDrv.dll's `UWindowsViewport::CreateNativeFont` [HP1 WinDrv 0x1110A3C0] makes a transient UFont (100
bytes) holding the font name (+80, FString), the height (+92) and
`CreateFontW(-Height, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, Name)` (+96). The
negative height asks for a character (em) height of Height pixels.

`UWindowsViewport::DrawString` [HP1 WinDrv 0x11109F20] (re)creates the GDI font if needed, selects it into the
frame's DC, sets a transparent background and the text color from the canvas DrawColor (render style ignored),
measures with `DrawTextW(DT_CALCRECT | DT_NOPREFIX)` and, unless only measuring (flag 1), draws at the canvas origin
plus the current position with `DT_NOPREFIX` (`DT_CENTER` too with flag 0x100). The string's width and height are added to
the caller's X/Y.

Flipendo (`src/knowwonder/KWNativeFont.cpp`) finds the font file (Windows: GDI's data for the name, falling back to
Microsoft Sans Serif and Arial, since the real MS Sans Serif is a bitmap font; Linux: fontconfig, with the language
added so the match covers its script), rasterizes Latin, Cyrillic and common punctuation into one BGRA page (cells
the advance wide and ascent + descent high, the baseline at the ascent), and returns an ordinary UFont whose code
points map to the cells. The canvas draws such a font premultiplied in the DrawColor with its alpha taken as 1, as GDI's anti-aliased text
(HP2's cutscene text color has alpha 0, which would make it additive), and
scales it with the menu canvas like the bitmap fonts. Asian languages need their glyphs added to the set.
