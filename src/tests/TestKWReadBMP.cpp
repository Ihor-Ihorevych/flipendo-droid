#include "Precomp.h"
#include "Test.h"
#include "KW.h"
#include <cstring>

// KW::ReadBMP (the save thumbnails, HP2's front end art), on BMP files built here.

namespace
{
	struct Pixel { uint8_t b, g, r; };

	// An uncompressed BMP; rows are given top first and written bottom-up unless topDown (negative height).
	Array<uint8_t> MakeBMP(int width, int height, int bpp, const std::vector<std::vector<uint8_t>>& rows,
		const std::vector<Pixel>& palette = {}, bool topDown = false, uint32_t compression = 0)
	{
		size_t stride = (((size_t)width * bpp + 31) / 32) * 4;
		uint32_t dataOffset = 54 + (uint32_t)palette.size() * 4;
		Array<uint8_t> f(dataOffset + stride * height, (uint8_t)0);
		auto put16 = [&](size_t at, uint16_t v) { memcpy(f.data() + at, &v, 2); };
		auto put32 = [&](size_t at, uint32_t v) { memcpy(f.data() + at, &v, 4); };
		f[0] = 'B'; f[1] = 'M';
		put32(2, (uint32_t)f.size());
		put32(10, dataOffset);
		put32(14, 40);
		put32(18, (uint32_t)width);
		put32(22, (uint32_t)(topDown ? -height : height));
		put16(26, 1);
		put16(28, (uint16_t)bpp);
		put32(30, compression);
		put32(46, (uint32_t)palette.size());
		for (size_t i = 0; i < palette.size(); i++)
		{
			f[54 + i * 4 + 0] = palette[i].b;
			f[54 + i * 4 + 1] = palette[i].g;
			f[54 + i * 4 + 2] = palette[i].r;
		}
		for (int y = 0; y < height; y++)
		{
			size_t fileRow = topDown ? y : height - 1 - y;
			if (!rows[y].empty()) // an empty row's data() may be null, which memcpy may not be given
				memcpy(f.data() + dataOffset + stride * fileRow, rows[y].data(), rows[y].size());
		}
		return f;
	}

	void CheckPixel(const Array<uint8_t>& bgra, int width, int x, int y, uint8_t b, uint8_t g, uint8_t r)
	{
		const uint8_t* p = bgra.data() + ((size_t)y * width + x) * 4;
		CHECK_EQ(p[0], b);
		CHECK_EQ(p[1], g);
		CHECK_EQ(p[2], r);
		CHECK_EQ(p[3], 255);
	}
}

TEST(ReadBMP_24BitBottomUp)
{
	// 2x2: rows are padded to 8 bytes; the last row in the file is the top one
	Array<uint8_t> file = MakeBMP(2, 2, 24, { { 1, 2, 3, 4, 5, 6 }, { 7, 8, 9, 10, 11, 12 } });
	int width = 0, height = 0;
	Array<uint8_t> bgra;
	CHECK(KW::ReadBMP(file, width, height, bgra));
	CHECK_EQ(width, 2);
	CHECK_EQ(height, 2);
	CHECK_EQ(bgra.size(), (size_t)16);
	CheckPixel(bgra, 2, 0, 0, 1, 2, 3);
	CheckPixel(bgra, 2, 1, 0, 4, 5, 6);
	CheckPixel(bgra, 2, 0, 1, 7, 8, 9);
	CheckPixel(bgra, 2, 1, 1, 10, 11, 12);
}

TEST(ReadBMP_32BitTopDown)
{
	Array<uint8_t> file = MakeBMP(1, 2, 32, { { 1, 2, 3, 0 }, { 4, 5, 6, 0 } }, {}, true);
	int width = 0, height = 0;
	Array<uint8_t> bgra;
	CHECK(KW::ReadBMP(file, width, height, bgra));
	CHECK_EQ(height, 2);
	CheckPixel(bgra, 1, 0, 0, 1, 2, 3);
	CheckPixel(bgra, 1, 0, 1, 4, 5, 6);
}

TEST(ReadBMP_8BitPalette)
{
	// An index past the palette's colours reads colour 0
	Array<uint8_t> file = MakeBMP(3, 1, 8, { { 1, 0, 7 } }, { { 10, 20, 30 }, { 40, 50, 60 } });
	int width = 0, height = 0;
	Array<uint8_t> bgra;
	CHECK(KW::ReadBMP(file, width, height, bgra));
	CHECK_EQ(width, 3);
	CheckPixel(bgra, 3, 0, 0, 40, 50, 60);
	CheckPixel(bgra, 3, 1, 0, 10, 20, 30);
	CheckPixel(bgra, 3, 2, 0, 10, 20, 30);
}

TEST(ReadBMP_Rejects)
{
	int width = 0, height = 0;
	Array<uint8_t> bgra;

	Array<uint8_t> notBmp = MakeBMP(1, 1, 24, { { 1, 2, 3 } });
	notBmp[0] = 'X';
	CHECK(!KW::ReadBMP(notBmp, width, height, bgra));

	CHECK(!KW::ReadBMP(MakeBMP(1, 1, 24, { { 1, 2, 3 } }, {}, false, 1), width, height, bgra)); // RLE
	CHECK(!KW::ReadBMP(MakeBMP(1, 1, 16, { { 1, 2 } }), width, height, bgra));

	Array<uint8_t> truncated = MakeBMP(4, 4, 24, { {}, {}, {}, {} });
	truncated.resize(truncated.size() - 1);
	CHECK(!KW::ReadBMP(truncated, width, height, bgra));

	Array<uint8_t> tiny(20, (uint8_t)0);
	tiny[0] = 'B'; tiny[1] = 'M';
	CHECK(!KW::ReadBMP(tiny, width, height, bgra));
	CHECK_EQ(width, 0);
}
