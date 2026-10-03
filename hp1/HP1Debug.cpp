#include "Precomp.h"
#include "HP1.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include <chrono>
#include <fstream>
#include <sstream>

// Development aid: in-engine screenshots without capturing the desktop.
//   HP1_SHOTS="4.5,5.7,8"  seconds since the first rendered frame
//   HP1_SHOT_DIR=<dir>     output directory (default: current directory), files hp1shot_<sec>.bmp

namespace HP1
{
	void OnFrameRendered(RenderDevice* device)
	{
		static bool parsed = false;
		static Array<float> times;
		static std::string dir;
		static auto start = std::chrono::steady_clock::now();
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_SHOTS"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
					if (!item.empty()) times.push_back(std::stof(item));
			}
			const char* d = getenv("HP1_SHOT_DIR");
			dir = d ? d : ".";
		}
		if (times.empty())
			return;

		float now = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
		if (now < times.front())
			return;
		float label = times.front();
		times.erase(times.begin());

		int width = engine->viewport->ViewportWidth();
		int height = engine->viewport->ViewportHeight();
		Array<TextureColor> pixels((size_t)width * height);
		device->ReadPixels(pixels.data());

		// 24-bit bottom-up BMP
		int rowSize = (width * 3 + 3) & ~3;
		uint32_t dataSize = rowSize * height;
		uint8_t header[54] = { 'B', 'M' };
		auto put32 = [&](int off, uint32_t v) { memcpy(header + off, &v, 4); };
		put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, width); put32(22, height);
		header[26] = 1; header[28] = 24; put32(34, dataSize);

		char name[64];
		snprintf(name, sizeof(name), "/hp1shot_%05.1f.bmp", label);
		std::ofstream f(dir + name, std::ios::binary);
		f.write((const char*)header, 54);
		Array<uint8_t> row((size_t)rowSize);
		for (int y = height - 1; y >= 0; y--) // ReadPixels is top-down
		{
			for (int x = 0; x < width; x++)
			{
				const TextureColor& c = pixels[(size_t)y * width + x];
				row[x * 3 + 0] = c.R; row[x * 3 + 1] = c.G; row[x * 3 + 2] = c.B; // ReadPixels returns BGRA
			}
			f.write((const char*)row.data(), rowSize);
		}
		LogMessage("HP1 screenshot " + dir + name);
	}
}
