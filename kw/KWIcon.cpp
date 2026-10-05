#include "Precomp.h"
#include "KW.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include <surrealwidgets/core/image.h>
#include <filesystem>
#include <map>

// The game window's icon is the game's own, from the player's folder: both games ship it next to their exe
// (HP1 System/hp.ico, one 32x32 16-colour image; HP2 system/Game.ico, 16 to 64 pixels in 8 and 24 bits). Without it
// the window keeps SurrealEngine's icon.
namespace KW
{
	namespace
	{
		// The folder entry whose name matches `name` ignoring case (the games' folder and file names vary in case).
		std::filesystem::path FindIgnoringCase(const std::filesystem::path& folder, const std::string& name)
		{
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
			{
				if (StrTools::equals_ignore_case(entry.path().filename().string(), name))
					return entry.path();
			}
			return {};
		}

		uint32_t ReadU32(const std::vector<uint8_t>& d, size_t at) { return d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | ((uint32_t)d[at + 3] << 24); }
		uint16_t ReadU16(const std::vector<uint8_t>& d, size_t at) { return (uint16_t)(d[at] | (d[at + 1] << 8)); }

		// HP1's hp.ico has an empty transparency mask: the Snitch sits on an opaque grey (192, 192, 192) square. Make the
		// corner colour transparent where it touches the edges (flood fill), so the window shows the Snitch alone.
		void ClearBackground(std::vector<uint32_t>& pixels, int width, int height)
		{
			uint32_t background = pixels[0];
			std::vector<int> stack = { 0, width - 1, (height - 1) * width, height * width - 1 };
			while (!stack.empty())
			{
				int i = stack.back();
				stack.pop_back();
				if (pixels[i] != background)
					continue;
				pixels[i] = 0;
				int x = i % width, y = i / width;
				if (x > 0) stack.push_back(i - 1);
				if (x + 1 < width) stack.push_back(i + 1);
				if (y > 0) stack.push_back(i - width);
				if (y + 1 < height) stack.push_back(i + width);
			}
		}

		// One icon image stored as a DIB (BITMAPINFOHEADER, palette, colour rows bottom-up, then the 1-bit
		// transparency mask). Returns RGBA, R in the low byte; empty if the format isn't one icons use.
		std::vector<uint32_t> DecodeDib(const std::vector<uint8_t>& d, size_t at, size_t size, int& width, int& height, int& bpp)
		{
			if (size < 40 || at + size > d.size() || ReadU32(d, at) < 40)
				return {};
			width = (int)ReadU32(d, at + 4);
			height = (int)ReadU32(d, at + 8) / 2; // colour rows + mask rows
			bpp = ReadU16(d, at + 14);
			if (width <= 0 || height <= 0 || width > 256 || height > 256 || ReadU32(d, at + 16) != 0) // uncompressed only
				return {};
			if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
				return {};

			size_t palette = at + ReadU32(d, at);
			size_t colors = bpp <= 8 ? (ReadU32(d, at + 32) ? ReadU32(d, at + 32) : (size_t)1 << bpp) : 0;
			size_t pixels = palette + colors * 4;
			size_t stride = ((size_t)width * bpp + 31) / 32 * 4;
			size_t mask = pixels + stride * height;
			size_t maskStride = ((size_t)width + 31) / 32 * 4;
			bool hasMask = bpp != 32;
			if ((hasMask ? mask + maskStride * height : mask) > at + size)
				return {};

			std::vector<uint32_t> out((size_t)width * height);
			bool anyMasked = false;
			for (int y = 0; y < height; y++)
			{
				const uint8_t* row = d.data() + pixels + stride * (height - 1 - y);
				const uint8_t* maskRow = d.data() + mask + maskStride * (height - 1 - y);
				for (int x = 0; x < width; x++)
				{
					uint32_t b, g, r, a = 255;
					if (bpp <= 8)
					{
						int bit = x * bpp;
						uint32_t index = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
						if (index >= colors)
							index = 0;
						const uint8_t* c = d.data() + palette + index * 4;
						b = c[0]; g = c[1]; r = c[2];
					}
					else
					{
						const uint8_t* c = row + x * (bpp / 8);
						b = c[0]; g = c[1]; r = c[2];
						if (bpp == 32)
							a = c[3];
					}
					if (hasMask && (maskRow[x / 8] >> (7 - x % 8)) & 1)
					{
						a = 0;
						anyMasked = true;
					}
					out[(size_t)y * width + x] = r | (g << 8) | (b << 16) | (a << 24);
				}
			}
			if (hasMask && !anyMasked)
				ClearBackground(out, width, height);
			return out;
		}
	}

	std::vector<std::shared_ptr<Image>> GameIcons(const std::string& gameRootFolder, const std::string& exeName)
	{
		std::filesystem::path system = FindIgnoringCase(gameRootFolder, "System");
		std::filesystem::path icoPath = system.empty() ? std::filesystem::path() : FindIgnoringCase(system, exeName + ".ico");
		if (icoPath.empty())
			return {};

		std::vector<uint8_t> d;
		try
		{
			Array<uint8_t> bytes = File::read_all_bytes(icoPath.string());
			d.assign(bytes.begin(), bytes.end());
		}
		catch (...)
		{
			return {};
		}
		if (d.size() < 6 || ReadU16(d, 0) != 0 || ReadU16(d, 2) != 1)
			return {};

		// The best image of each size (highest bit depth). PNG entries (Vista-style icons) aren't read; neither game has them.
		std::map<int, std::pair<int, std::shared_ptr<Image>>> best;
		int count = ReadU16(d, 4);
		for (int i = 0; i < count && 6 + 16 * (size_t)(i + 1) <= d.size(); i++)
		{
			size_t entry = 6 + 16 * (size_t)i;
			size_t size = ReadU32(d, entry + 8), at = ReadU32(d, entry + 12);
			int width = 0, height = 0, bpp = 0;
			std::vector<uint32_t> pixels = DecodeDib(d, at, size, width, height, bpp);
			if (pixels.empty() || width != height)
				continue;
			auto& slot = best[width];
			if (!slot.second || bpp > slot.first)
				slot = { bpp, Image::Create(width, height, ImageFormat::R8G8B8A8, pixels.data()) };
		}

		std::vector<std::shared_ptr<Image>> icons;
		for (auto& [size, image] : best)
			icons.push_back(image.second);
		return icons;
	}
}
