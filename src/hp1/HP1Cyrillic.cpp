#include "Precomp.h"
#include "HP1.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "miniz/miniz.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>

// The library is a single C file shipped with SurrealEngine (Thirdparty/schrift) that nothing else compiles. Its names (Point,
// Line, ...) clash with the engine's, so it lives in a namespace of its own; the C headers it includes are already in.
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
// (an elaborated "struct Point" inside a namespace still finds the engine's Point, so the six types are renamed too)
#define Point SchriftPoint
#define Line SchriftLine
#define Curve SchriftCurve
#define Cell SchriftCell
#define Outline SchriftOutline
#define Raster SchriftRaster
namespace Schrift
{
#include "schrift/schrift.cpp"
}
#undef Point
#undef Line
#undef Curve
#undef Cell
#undef Outline
#undef Raster
using namespace Schrift;

// A Russian text translation (--cyrillic) for HP1, which has no Cyrillic in its fonts: the translation's .int files are
// written in Windows-1251, so the bytes 0xC0..0xFF (and a few others) are Cyrillic letters. The game's fonts are textures with
// Latin glyphs only; for those bytes the font gets glyphs drawn here from Noto Sans (SurrealEngine.pk3, SIL Open Font
// License), sized and coloured after the font's own 'A' so they sit on its baseline in its colours. Not a port of anything
// in the original game (a localized release shipped redrawn font textures instead).

namespace HP1
{
	namespace
	{
		// Windows-1251 bytes that are not Latin-1, as Unicode
		char32_t Cp1251ToUnicode(unsigned char c)
		{
			if (c >= 0xC0)
				return 0x410 + (c - 0xC0);
			switch (c)
			{
			case 0xA8: return 0x401; case 0xB8: return 0x451; // Ё ё
			case 0xAA: return 0x404; case 0xBA: return 0x454; // Є є
			case 0xAF: return 0x407; case 0xBF: return 0x457; // Ї ї
			case 0xB2: return 0x406; case 0xB3: return 0x456; // І і
			case 0xA1: return 0x40E; case 0xA2: return 0x45E; // Ў ў
			case 0xA5: return 0x490; case 0xB4: return 0x491; // Ґ ґ
			case 0xB9: return 0x2116; // №
			case 0x84: return 0x201E; case 0x85: return 0x2026; case 0x8B: return 0x2039; case 0x9B: return 0x203A;
			case 0x91: return 0x2018; case 0x92: return 0x2019; case 0x93: return 0x201C; case 0x94: return 0x201D;
			case 0x96: return 0x2013; case 0x97: return 0x2014;
			default: return 0;
			}
		}

		std::vector<uint8_t> TtfData;
		SFT_Font* TtfFont = nullptr;
		bool TtfTried = false;

		bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;
			std::streamsize size = file.tellg();
			if (size <= 0)
				return false;
			file.seekg(0);
			out.resize((size_t)size);
			return (bool)file.read((char*)out.data(), size);
		}

		bool ReadFromPk3(const std::string& pk3, const char* name, std::vector<uint8_t>& out)
		{
			mz_zip_archive zip = {};
			if (!mz_zip_reader_init_file(&zip, pk3.c_str(), 0))
				return false;
			size_t size = 0;
			void* data = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
			bool ok = data && size > 0;
			if (ok)
				out.assign((uint8_t*)data, (uint8_t*)data + size);
			if (data)
				mz_free(data);
			mz_zip_reader_end(&zip);
			return ok;
		}

		SFT_Font* LoadTtf()
		{
			if (TtfTried)
				return TtfFont;
			TtfTried = true;
			std::vector<std::string> pk3s;
			if (const char* home = std::getenv("HOME"))
				pk3s.push_back(std::string(home) + "/.surrealengine/SurrealEngine.pk3");
			pk3s.push_back("SurrealEngine.pk3");
			for (const std::string& pk3 : pk3s)
			{
				if (ReadFromPk3(pk3, "noto/NotoSans-Regular.ttf", TtfData))
					break;
			}
			if (TtfData.empty())
			{
				for (const char* path : { "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/Roboto-Regular.ttf" })
				{
					if (ReadFile(path, TtfData))
						break;
				}
			}
			if (!TtfData.empty())
				TtfFont = sft_loadmem(TtfData.data(), TtfData.size());
			LogMessage(std::string("Cyrillic glyphs: ") + (TtfFont ? "Noto Sans loaded" : "no TrueType font found, Russian text stays blank"));
			return TtfFont;
		}

		// What a game font looks like, from its 'A'
		struct FontLook
		{
			int cellHeight = 16;
			int baseline = 13;   // row of the cell the letters stand on
			int capHeight = 10;  // cap height, without the outline
			int outline = 0;     // outline thickness in pixels (0: none)
			int padding = 1;     // empty columns a glyph cell has besides the ink
			uint8_t fill[3] = { 255, 255, 255 };
			uint8_t edge[3] = { 0, 0, 0 };
			bool upperOnly = false;
		};

		FontLook LookOf(const UFont* font)
		{
			FontLook look;
			FontGlyph a = font->GetGlyph('A');
			FontGlyph small = font->GetGlyph('a');
			look.upperOnly = small.Texture == a.Texture && small.StartU == a.StartU && small.StartV == a.StartV;
			if (!a.Texture || a.VSize <= 0)
				return look;
			look.cellHeight = a.VSize;
			look.baseline = (int)std::lround(a.VSize * 0.8f);
			look.capHeight = std::max((int)std::lround(a.VSize * 0.6f), 4);

			UTexture* tex = a.Texture;
			if (tex->UsedFormat != TextureFormat::P8 || tex->UsedMipmaps.empty() || !tex->Palette())
				return look;
			const UnrealMipmap& mip = tex->UsedMipmaps[0];
			const uint8_t* palette = (const uint8_t*)tex->Palette()->Colors.data();
			size_t paletteSize = tex->Palette()->Colors.size();

			int top = -1, bottom = -1, left = a.USize, right = -1;
			std::vector<int> counts(256, 0);
			for (int y = 0; y < a.VSize; y++)
			{
				for (int x = 0; x < a.USize; x++)
				{
					int u = a.StartU + x, v = a.StartV + y;
					if (u < 0 || v < 0 || u >= mip.Width || v >= mip.Height)
						continue;
					uint8_t index = mip.Data[(size_t)v * mip.Width + u];
					if (index == 0)
						continue;
					counts[index]++;
					if (top < 0)
						top = y;
					bottom = y;
					left = std::min(left, x);
					right = std::max(right, x);
				}
			}
			if (top < 0)
				return look;

			auto lum = [&](int i) { return (int)palette[i * 4] * 299 + (int)palette[i * 4 + 1] * 587 + (int)palette[i * 4 + 2] * 114; };
			int maxLum = 0, total = 0;
			for (int i = 1; i < 256 && (size_t)i < paletteSize; i++)
			{
				if (counts[i])
				{
					maxLum = std::max(maxLum, lum(i));
					total += counts[i];
				}
			}
			// the fill: the average of the bright pixels; an outline: dark pixels around them, when there are enough
			double fill[3] = {}, edge[3] = {};
			int fillCount = 0, edgeCount = 0;
			for (int i = 1; i < 256 && (size_t)i < paletteSize; i++)
			{
				if (!counts[i])
					continue;
				int l = lum(i);
				double* target = nullptr;
				if (l >= maxLum / 2)
				{
					target = fill;
					fillCount += counts[i];
				}
				else if (l < maxLum * 3 / 10)
				{
					target = edge;
					edgeCount += counts[i];
				}
				if (target)
				{
					for (int c = 0; c < 3; c++)
						target[c] += palette[i * 4 + c] * (double)counts[i];
				}
			}
			for (int c = 0; c < 3; c++)
			{
				if (fillCount)
					look.fill[c] = (uint8_t)std::clamp(fill[c] / fillCount, 0.0, 255.0);
				if (edgeCount)
					look.edge[c] = (uint8_t)std::clamp(edge[c] / edgeCount, 0.0, 255.0);
			}
			int inkHeight = bottom - top + 1;
			look.outline = edgeCount * 100 >= total * 15 ? std::max(1, (int)std::lround(inkHeight / 14.0)) : 0;
			look.baseline = bottom + 1 - look.outline;
			look.capHeight = std::max(inkHeight - 2 * look.outline, 4);
			look.padding = std::max(a.USize - (right - left + 1), 0);
			return look;
		}

		struct Atlas
		{
			UTexture* texture = nullptr;
			FontGlyph glyphs[256];
			bool built = false;
		};

		std::map<const UFont*, Atlas> Atlases;
		int AtlasCount = 0;

		void Build(const UFont* font, Atlas& atlas)
		{
			atlas.built = true;
			SFT_Font* ttf = LoadTtf();
			if (!ttf)
				return;
			FontLook look = LookOf(font);

			SFT sft = {};
			sft.font = ttf;
			sft.flags = SFT_DOWNWARD_Y;
			// the scale that gives Н (a capital with a flat top and bottom) the font's cap height
			sft.xScale = sft.yScale = 100.0;
			SFT_Glyph probe = 0;
			SFT_GMetrics probeMetrics = {};
			double em = look.capHeight / 0.714;
			if (sft_lookup(&sft, 0x41D, &probe) == 0 && sft_gmetrics(&sft, probe, &probeMetrics) == 0 && probeMetrics.minHeight > 0)
				em = look.capHeight * 100.0 / probeMetrics.minHeight;
			sft.xScale = sft.yScale = std::max(em, 6.0);

			struct Drawn
			{
				int byte;
				int width;     // cell width
				int inkX;      // where the coverage image starts in the cell
				int inkY;
				int imageWidth, imageHeight;
				std::vector<uint8_t> coverage;
			};
			std::vector<Drawn> drawn;
			for (int b = 0x80; b < 0x100; b++)
			{
				char32_t cp = Cp1251ToUnicode((unsigned char)b);
				if (!cp)
					continue;
				if (look.upperOnly && cp >= 0x430 && cp <= 0x44F)
					cp -= 0x20; // a capitals-only font: lowercase drawn as capitals, like its Latin
				else if (look.upperOnly && cp == 0x451)
					cp = 0x401;
				SFT_Glyph glyph = 0;
				SFT_GMetrics metrics = {};
				if (sft_lookup(&sft, cp, &glyph) != 0 || sft_gmetrics(&sft, glyph, &metrics) != 0)
					continue;
				Drawn d;
				d.byte = b;
				d.imageWidth = (metrics.minWidth + 3) & ~3;
				d.imageHeight = metrics.minHeight;
				d.inkX = look.outline + look.padding / 2 + std::max((int)std::lround(metrics.leftSideBearing), 0);
				d.inkY = look.baseline + metrics.yOffset;
				d.width = std::max((int)std::lround(metrics.advanceWidth), d.inkX + metrics.minWidth) + 2 * look.outline + look.padding;
				if (d.imageWidth > 0 && d.imageHeight > 0)
				{
					d.coverage.resize((size_t)d.imageWidth * d.imageHeight);
					SFT_Image image = { d.coverage.data(), d.imageWidth, d.imageHeight };
					if (sft_render(&sft, glyph, image) != 0)
						d.coverage.clear();
				}
				drawn.push_back(std::move(d));
			}
			if (drawn.empty())
				return;

			// one texture, the glyphs in rows
			int height = look.cellHeight;
			int width = 256;
			for (;;)
			{
				int x = 0, rows = 1;
				for (const Drawn& d : drawn)
				{
					if (x + d.width > width)
					{
						x = 0;
						rows++;
					}
					x += d.width + 1;
				}
				if (rows * (height + 1) <= width || width >= 2048)
					break;
				width *= 2;
			}
			std::vector<FontCharacter> cells(drawn.size());
			int x = 0, y = 0;
			for (size_t i = 0; i < drawn.size(); i++)
			{
				if (x + drawn[i].width > width)
				{
					x = 0;
					y += height + 1;
				}
				cells[i] = { x, y, drawn[i].width, height };
				x += drawn[i].width + 1;
			}
			int texHeight = 1;
			while (texHeight < y + height)
				texHeight *= 2;
			int texWidth = width;

			std::vector<uint8_t> pixels((size_t)texWidth * texHeight * 4, 0); // BGRA, transparent black
			auto put = [&](int px, int py, const uint8_t* rgb) {
				if (px < 0 || py < 0 || px >= texWidth || py >= texHeight)
					return;
				uint8_t* p = &pixels[((size_t)py * texWidth + px) * 4];
				p[0] = rgb[2];
				p[1] = rgb[1];
				p[2] = rgb[0];
				p[3] = 255;
			};
			for (size_t i = 0; i < drawn.size(); i++)
			{
				const Drawn& d = drawn[i];
				if (d.coverage.empty())
					continue;
				const FontCharacter& cell = cells[i];
				for (int pass = (look.outline ? 0 : 1); pass < 2; pass++)
				{
					for (int iy = 0; iy < d.imageHeight; iy++)
					{
						for (int ix = 0; ix < d.imageWidth; ix++)
						{
							if (d.coverage[(size_t)iy * d.imageWidth + ix] < 110)
								continue;
							int cx = d.inkX + ix, cy = d.inkY + iy;
							if (cx < 0 || cy < 0 || cx >= cell.USize || cy >= cell.VSize)
								continue;
							if (pass == 0)
							{
								for (int oy = -look.outline; oy <= look.outline; oy++)
								{
									for (int ox = -look.outline; ox <= look.outline; ox++)
									{
										int ex = cx + ox, ey = cy + oy;
										if (ex >= 0 && ey >= 0 && ex < cell.USize && ey < cell.VSize)
											put(cell.StartU + ex, cell.StartV + ey, look.edge);
									}
								}
							}
							else
							{
								put(cell.StartU + cx, cell.StartV + cy, look.fill);
							}
						}
					}
				}
			}

			UClass* cls = engine->packages->FindClass("Engine.Texture");
			Package* transient = engine->packages->GetTransientPackage();
			if (!cls || !transient)
				return;
			UTexture* tex = UObject::Cast<UTexture>(transient->NewObject(NameString("FlipendoCyrillic" + std::to_string(AtlasCount++)), cls, ObjectFlags::Transient));
			if (!tex)
				return;
			auto bits = [](int v) { int b = 0; while ((1 << b) < v) b++; return b; };
			tex->USize() = texWidth;
			tex->VSize() = texHeight;
			tex->UBits() = (uint8_t)bits(texWidth);
			tex->VBits() = (uint8_t)bits(texHeight);
			tex->Palette() = nullptr;
			tex->UsedFormat = TextureFormat::BGRA8;
			UnrealMipmap mip;
			mip.Width = texWidth;
			mip.Height = texHeight;
			mip.UBits = bits(texWidth);
			mip.VBits = bits(texHeight);
			mip.Data.resize(pixels.size());
			std::copy(pixels.begin(), pixels.end(), mip.Data.begin());
			tex->UsedMipmaps.push_back(std::move(mip));
			atlas.texture = tex;
			for (size_t i = 0; i < drawn.size(); i++)
				atlas.glyphs[drawn[i].byte] = FontGlyph(tex, cells[i]);

			LogMessage("Cyrillic glyphs for font " + font->Name.ToString() + ": cell " + std::to_string(look.cellHeight) + ", cap " +
				std::to_string(look.capHeight) + ", outline " + std::to_string(look.outline) + (look.upperOnly ? ", capitals only" : "") +
				", " + std::to_string(drawn.size()) + " glyphs in " + std::to_string(texWidth) + "x" + std::to_string(texHeight));
		}

		FontGlyph CyrillicGlyph(const UFont* font, unsigned char c)
		{
			if (c < 0x80 || !Cp1251ToUnicode(c))
				return {};
			Atlas& atlas = Atlases[font];
			if (!atlas.built)
				Build(font, atlas);
			return atlas.glyphs[c];
		}
	}

	void InitCyrillic()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
#ifdef __ANDROID__
		// the texts are 8-bit here (IniFile narrows the UTF-16 ones): a code point is a byte, drawn from the font page of that byte
		UFont::ByteText = true;
#endif
		if (commandline && commandline->HasArg("", "--cyrillic"))
		{
			UFont::ExtraGlyph = &CyrillicGlyph;
			UFont::ByteText = true; // the texts are 8-bit: a code point is a byte
			LogMessage("Cyrillic text: on (Windows-1251 .int files, glyphs from Noto Sans)");
		}
	}
}
