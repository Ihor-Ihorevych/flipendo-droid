#include "Precomp.h"
#include "HP1.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "HP1Actor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include <chrono>
#include <fstream>
#include <sstream>

// Development aids, all timed in seconds since the first rendered frame:
//   HP1_SHOTS="4.5,5.7,8"      in-engine screenshots (no desktop capture)
//   HP1_SHOT_DIR=<dir>         screenshot directory (default: current directory), files hp1shot_<sec>.bmp
//   HP1_KEYS="30:W:4,36:Up:1"  press <key> at <sec> and hold it for <dur> seconds. Keys: a letter or
//                              digit, Up/Down/Left/Right, Space, Shift, Ctrl, Enter, Escape, or a number
//                              (EInputKey value)
//   HP1_TRACE="Harry,gen_"     log every actor whose name starts with one of these, every 0.5 s

namespace HP1
{
	static float SecondsSinceFirstFrame()
	{
		static auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}

	static void TickDebugKeys(float now)
	{
		struct KeyPress { float Time; int Key; float Duration; bool Down = false; bool Done = false; };
		static bool parsed = false;
		static Array<KeyPress> presses;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_KEYS"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
				{
					std::stringstream parts(item);
					std::string t, k, d;
					if (!std::getline(parts, t, ':') || !std::getline(parts, k, ':'))
						continue;
					std::getline(parts, d, ':');
					int key = 0;
					static const std::pair<const char*, int> names[] = {
						{ "Up", 0x26 }, { "Down", 0x28 }, { "Left", 0x25 }, { "Right", 0x27 }, { "Space", 0x20 },
						{ "Shift", 0x10 }, { "Ctrl", 0x11 }, { "Enter", 0x0D }, { "Escape", 0x1B } };
					for (auto& n : names)
						if (k == n.first) key = n.second;
					if (!key && k.size() == 1 && isalnum((unsigned char)k[0]))
						key = toupper((unsigned char)k[0]);
					if (!key)
						key = std::atoi(k.c_str());
					if (key > 0 && key < 256)
						presses.push_back({ std::stof(t), key, d.empty() ? 0.1f : std::stof(d) });
				}
			}
		}
		for (KeyPress& p : presses)
		{
			if (!p.Down && !p.Done && now >= p.Time)
			{
				p.Down = true;
				engine->OnWindowKeyDown((EInputKey)p.Key);
				LogMessage("HP1 key down " + std::to_string(p.Key));
			}
			else if (p.Down && now >= p.Time + p.Duration)
			{
				p.Down = false;
				p.Done = true;
				engine->OnWindowKeyUp((EInputKey)p.Key);
				LogMessage("HP1 key up " + std::to_string(p.Key));
			}
		}
	}

	static void TickDebugTrace(float now)
	{
		static bool parsed = false;
		static Array<std::string> prefixes;
		static float next = 0.0f;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_TRACE"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
					if (!item.empty()) prefixes.push_back(item);
			}
		}
		if (prefixes.empty() || now < next || !engine->Level)
			return;
		next = now + 0.5f;

		for (UActor* a : engine->Level->Actors)
		{
			if (!a)
				continue;
			std::string name = a->Name.ToString();
			bool match = false;
			for (const std::string& p : prefixes)
				match = match || name.compare(0, p.size(), p) == 0;
			if (!match)
				continue;

			char buf[700];
			int n = snprintf(buf, sizeof(buf), "HP1 trace t=%.1f %s state=%s loc=(%.0f,%.0f,%.0f) vel=(%.0f,%.0f,%.0f) acc=(%.0f,%.0f) phys=%d rot=%d drot=%d anim=%s rate=%.2f frame=%.2f tween=%.2f",
				now, name.c_str(), a->GetStateName().ToString().c_str(), a->Location().x, a->Location().y, a->Location().z,
				a->Velocity().x, a->Velocity().y, a->Velocity().z, a->Acceleration().x, a->Acceleration().y, (int)a->Physics(),
				a->Rotation().Yaw & 0xffff, a->DesiredRotation().Yaw & 0xffff, a->AnimSequence().ToString().c_str(), a->AnimRate(), a->AnimFrame(), TweenAlpha(a));
			if (UPawn* pawn = UObject::TryCast<UPawn>(a))
				n += snprintf(buf + n, sizeof(buf) - n, " ground=%.0f desired=%.2f rrate=%d walking=%d",
					pawn->GroundSpeed(), pawn->DesiredSpeed(), pawn->RotationRate().Yaw, (int)pawn->bIsWalking());
			if (UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(a))
				snprintf(buf + n, sizeof(buf) - n, " bRun=%d bDuck=%d aForward=%.0f aBaseY=%.0f",
					(int)player->bRun(), (int)player->bDuck(), player->aForward(), player->aBaseY());
			LogMessage(buf);
		}
	}

	void OnFrameRendered(RenderDevice* device)
	{
		float frameTime = SecondsSinceFirstFrame();
		TickDebugKeys(frameTime);
		TickDebugTrace(frameTime);

		static bool parsed = false;
		static Array<float> times;
		static std::string dir;
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

		float now = frameTime;
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
