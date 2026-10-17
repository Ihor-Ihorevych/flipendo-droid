#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Utils/File.h"
#include "Utils/UTF16.h"
#include "Engine.h"
#include "core/truetypefont.h"
#include <cctype>
#include <cmath>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(__ANDROID__)
#include <fontconfig/fontconfig.h>
#endif

// System fonts for Console.CreateNativeFont. HP1's Windows viewport keeps a GDI font
// (CreateFontW(-Height, 0, 0, 0, FW_NORMAL, ..., DEFAULT_CHARSET, ..., ANTIALIASED_QUALITY, 0, Name)) in the UFont and
// draws strings with DrawTextW straight onto the frame, anti-aliased in the canvas DrawColor whatever the style.
// Here the font file is rasterized once into an ordinary UFont (one BGRA page of glyph cells, code points mapped
// to cells) that the canvas draws premultiplied (UFont::IsNative). Russian HP1 uses it for all its menu text
// (SAPFont.rus: MS Sans Serif 24/16/10/12), as do the Asian releases.

namespace KW
{
	static std::string ToLower(std::string s)
	{
		for (char& c : s)
			c = (char)std::tolower((unsigned char)c);
		return s;
	}

	struct NativeFontFile
	{
		std::vector<uint8_t> data;
		int index = 0; // font in a TrueType collection
	};

#ifdef _WIN32
	static bool LoadGdiFont(const std::wstring& name, NativeFontFile& file)
	{
		HDC dc = GetDC(0);
		if (!dc)
			return false;
		HFONT font = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, name.c_str());
		bool ok = false;
		if (font)
		{
			HFONT oldFont = (HFONT)SelectObject(dc, font);
			// GetFontData fails for bitmap fonts (the real MS Sans Serif is sserife.fon)
			DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
			if (size != GDI_ERROR && size > 0)
			{
				file.data.resize(size);
				ok = GetFontData(dc, 0, 0, file.data.data(), size) != GDI_ERROR;
			}
			SelectObject(dc, oldFont);
			DeleteObject(font);
		}
		ReleaseDC(0, dc);
		return ok;
	}

	static bool LoadSystemFontFile(const std::string& name, const std::string& language, NativeFontFile& file)
	{
		// MS Sans Serif's TrueType successor is Microsoft Sans Serif
		for (const std::wstring& candidate : { to_utf16(name), std::wstring(L"Microsoft Sans Serif"), std::wstring(L"Arial") })
		{
			if (LoadGdiFont(candidate, file))
				return true;
		}
		return false;
	}
#elif defined(__ANDROID__)
	// Android has no fontconfig: the system's Noto Sans or Roboto cover Latin and Cyrillic
	static bool LoadSystemFontFile(const std::string& name, const std::string& language, NativeFontFile& file)
	{
		for (const char* path : { "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/Roboto-Regular.ttf", "/system/fonts/DroidSans.ttf" })
		{
			try
			{
				Array<uint8_t> bytes = File::read_all_bytes(path);
				file.data.assign(bytes.begin(), bytes.end());
				LogMessage("CreateNativeFont: " + name + " is " + path);
				return true;
			}
			catch (const std::exception&)
			{
			}
		}
		return false;
	}
#else
	// fontconfig language tag for HP1's language codes, so the match covers the language's script
	static std::string FontconfigLanguage(const std::string& language)
	{
		static const std::map<std::string, std::string> tags =
		{
			{ "rus", "ru" }, { "pol", "pl" }, { "hun", "hu" }, { "cze", "cs" }, { "jap", "ja" }, { "kor", "ko" },
			{ "chi", "zh-tw" }, { "tra", "zh-tw" }, { "sim", "zh-cn" }, { "tha", "th" },
		};
		auto it = tags.find(ToLower(language));
		return it != tags.end() ? it->second : std::string();
	}

	static bool LoadSystemFontFile(const std::string& name, const std::string& language, NativeFontFile& file)
	{
		// fontconfig substitutes a font it has for Windows names (MS Sans Serif -> a sans serif)
		std::string pattern = name;
		std::string lang = FontconfigLanguage(language);
		if (!lang.empty())
			pattern += ":lang=" + lang;

		std::string filename;
		FcConfig* config = FcInitLoadConfigAndFonts();
		if (!config)
			return false;
		if (FcPattern* pat = FcNameParse((const FcChar8*)pattern.c_str()))
		{
			FcConfigSubstitute(config, pat, FcMatchPattern);
			FcDefaultSubstitute(pat);
			FcResult res;
			if (FcPattern* font = FcFontMatch(config, pat, &res))
			{
				FcChar8* path = nullptr;
				if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch)
					filename = (const char*)path;
				int index = 0;
				if (FcPatternGetInteger(font, FC_INDEX, 0, &index) == FcResultMatch)
					file.index = index;
				FcPatternDestroy(font);
			}
			FcPatternDestroy(pat);
		}
		FcConfigDestroy(config);
		if (filename.empty())
			return false;

		try
		{
			Array<uint8_t> bytes = File::read_all_bytes(filename);
			file.data.assign(bytes.begin(), bytes.end());
		}
		catch (const std::exception&)
		{
			return false;
		}
		LogMessage("CreateNativeFont: " + name + " is " + filename);
		return true;
	}
#endif

	// The characters given glyphs: Latin (ASCII, Latin-1, Latin Extended-A for the Central European releases),
	// Cyrillic, and the punctuation localized text uses (dashes, quotes, ellipsis, numero sign).
	static std::vector<uint32_t> NativeFontCharacters()
	{
		std::vector<uint32_t> chars;
		auto range = [&](uint32_t first, uint32_t last) { for (uint32_t c = first; c <= last; c++) chars.push_back(c); };
		range(0x20, 0x7e);
		range(0xa0, 0x17f);
		range(0x400, 0x45f);
		range(0x490, 0x491);
		range(0x2010, 0x2027);
		range(0x2030, 0x203a);
		for (uint32_t c : { 0x20acu, 0x2116u, 0x2122u })
			chars.push_back(c);
		return chars;
	}

	static uint8_t SizeBits(int size)
	{
		uint8_t bits = 0;
		while ((1 << (bits + 1)) <= size)
			bits++;
		return bits;
	}

	// The viewport's CreateNativeFont: a transient font named after the system font, Height pixels per character (the
	// em height; CreateFontW gets -Height). None if no font file can be found.
	// IDA WinDrv.dll: ?CreateNativeFont@UWindowsViewport@@UAEPAVUFont@@PBGH@Z [HP1 WinDrv 0x1110A3C0]
	// IDA WinDrv.dll: ?DrawString@UWindowsViewport@@UAEXKPAVUFont@@AAH1PBGABVFPlane@@@Z [HP1 WinDrv 0x11109F20]
	UFont* CreateNativeFont(const std::string& name, int height)
	{
		static std::map<std::pair<std::string, int>, UFont*> fonts;
		auto key = std::make_pair(ToLower(name), height);
		auto it = fonts.find(key);
		if (it != fonts.end())
			return it->second;

		NativeFontFile file;
		if (height <= 0 || !LoadSystemFontFile(name, engine->packages->GetLanguage(), file))
		{
			LogMessage("CreateNativeFont(" + name + ", " + std::to_string(height) + "): no such font");
			return fonts[key] = nullptr;
		}

		std::unique_ptr<TrueTypeFont> ttf;
		try
		{
			ttf = std::make_unique<TrueTypeFont>(std::make_shared<TTFDataBufferImpl>(std::move(file.data)), file.index);
		}
		catch (const std::exception& e)
		{
			LogMessage("CreateNativeFont(" + name + "): " + e.what());
			return fonts[key] = nullptr;
		}

		// Cells are the advance wide and the line (ascent + descent) high, the baseline at the ascent, like the
		// rectangles DrawTextW fills. The rasterizer draws 3 samples per pixel across (subpixel); they're averaged.
		TrueTypeTextMetrics metrics = ttf->GetTextMetrics(height);
		int ascent = (int)std::round(metrics.ascent);
		int cellHeight = std::max(ascent + (int)std::round(metrics.descent), 1);

		struct Cell
		{
			uint32_t codepoint;
			int advance;
			int left, top, width, height;
			std::vector<uint8_t> coverage;
		};
		std::vector<Cell> cells;
		for (uint32_t c : NativeFontCharacters())
		{
			uint32_t glyphIndex = ttf->GetGlyphIndex(c);
			if (glyphIndex == 0 && c != 0x20)
				continue;
			TrueTypeGlyph glyph = ttf->LoadGlyph(glyphIndex, height);
			Cell cell;
			cell.codepoint = c;
			cell.advance = std::max((glyph.advanceWidth + 2) / 3, 1);
			cell.left = (glyph.leftSideBearing + 2) / 3;
			cell.top = ascent + glyph.yOffset;
			cell.width = (glyph.width + 2) / 3;
			cell.height = glyph.height;
			cell.coverage.resize((size_t)cell.width * cell.height);
			for (int y = 0; y < glyph.height; y++)
			{
				for (int x = 0; x < cell.width; x++)
				{
					int sum = 0;
					for (int s = 0; s < 3; s++)
					{
						int sx = x * 3 + s;
						if (sx < glyph.width)
							sum += glyph.grayscale[(size_t)y * glyph.width + sx];
					}
					cell.coverage[(size_t)y * cell.width + x] = (uint8_t)((sum + 1) / 3);
				}
			}
			cells.push_back(std::move(cell));
		}

		// Pack the cells in rows on a 512 wide page, a pixel apart
		const int pageWidth = 512;
		int x = 1, y = 1;
		Array<FontCharacter> characters;
		std::unordered_map<uint16_t, uint16_t> remap;
		for (const Cell& cell : cells)
		{
			if (x + cell.advance + 1 > pageWidth)
			{
				x = 1;
				y += cellHeight + 1;
			}
			FontCharacter character;
			character.StartU = x;
			character.StartV = y;
			character.USize = cell.advance;
			character.VSize = cellHeight;
			remap[(uint16_t)cell.codepoint] = (uint16_t)characters.size();
			characters.push_back(character);
			x += cell.advance + 1;
		}
		int pageHeight = 1;
		while (pageHeight < y + cellHeight + 1)
			pageHeight <<= 1;

		// White, premultiplied by coverage; the canvas multiplies in the draw color
		Array<uint8_t> pixels;
		pixels.resize((size_t)pageWidth * pageHeight * 4, 0);
		for (size_t i = 0; i < cells.size(); i++)
		{
			const Cell& cell = cells[i];
			const FontCharacter& character = characters[i];
			for (int cy = 0; cy < cell.height; cy++)
			{
				int py = character.StartV + cell.top + cy;
				if (py < character.StartV || py >= character.StartV + cellHeight)
					continue;
				for (int cx = 0; cx < cell.width; cx++)
				{
					// glyph overhang past the cell is cut, as DrawTextW clips to its rectangle
					int px = character.StartU + cell.left + cx;
					if (px < character.StartU || px >= character.StartU + character.USize)
						continue;
					uint8_t a = cell.coverage[(size_t)cy * cell.width + cx];
					uint8_t* p = &pixels[((size_t)py * pageWidth + px) * 4];
					p[0] = p[1] = p[2] = p[3] = a;
				}
			}
		}

		Package* transient = engine->packages->GetTransientPackage();
		std::string objName = "NativeFont" + std::to_string(fonts.size());
		UTexture* tex = UObject::Cast<UTexture>(transient->NewObject(objName + "Page", engine->packages->FindClass("Engine.Texture"), ObjectFlags::Transient));
		tex->Format() = (uint8_t)TextureFormat::BGRA8;
		tex->USize() = pageWidth;
		tex->VSize() = pageHeight;
		tex->UClamp() = pageWidth;
		tex->VClamp() = pageHeight;
		tex->UBits() = SizeBits(pageWidth);
		tex->VBits() = SizeBits(pageHeight);
		tex->Palette() = nullptr;
		UnrealMipmap mip;
		mip.Width = pageWidth;
		mip.Height = pageHeight;
		mip.UBits = tex->UBits();
		mip.VBits = tex->VBits();
		mip.Data = std::move(pixels);
		tex->UncompressedMipmaps.push_back(std::move(mip));
		tex->UsedFormat = TextureFormat::BGRA8;
		tex->UsedMipmaps = tex->UncompressedMipmaps;

		UFont* font = UObject::Cast<UFont>(transient->NewObject(objName, engine->packages->FindClass("Engine.Font"), ObjectFlags::Transient));
		font->SetNativeGlyphs(tex, std::move(characters), std::move(remap));
		return fonts[key] = font;
	}
}
