#include "Precomp.h"
#include "HP1.h"
#include "HP1Mods.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

// Texture overrides (on by default, off with --vanilla): a texture is drawn from Textures/<package>/<group>/<name>.dds
// in the game folder when that file exists, the layout of the D3D11Drv renderer's overrides and so of the texture packs
// made for it (e.g. the ESRGAN Upscale Pack). Groups nest as folders; the match ignores case. Only plain textures are
// replaced: realtime and procedural ones (fire, water, ice, scripted) keep their own pixels. The replacement only changes
// what is uploaded: UVs still come from the texture's own USize/VSize, so an image 4 times as large maps the same way.
// DXT1/DXT3/DXT5 files are uploaded as they are (BC1/BC2/BC3), mipmaps included. Nothing is cached in memory; a texture's
// file is read each time the render device uploads it.

namespace HP1::Mods
{
	namespace
	{
		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}

		// "package/group/name" (lower case) -> the .dds file, for every .dds under the game's Textures folder.
		const std::unordered_map<std::string, std::filesystem::path>& OverrideFiles()
		{
			static std::unordered_map<std::string, std::filesystem::path> files;
			static bool scanned = false;
			if (scanned)
				return files;
			scanned = true;

			std::error_code ec;
			std::filesystem::path root = std::filesystem::path(engine->LaunchInfo.gameRootFolder) / "Textures";
			for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			{
				const std::filesystem::path& path = it->path();
				if (it.depth() < 1 || !it->is_regular_file(ec) || Lower(path.extension().string()) != ".dds")
					continue;
				std::string key = Lower(path.lexically_relative(root).replace_extension().generic_string());
				files.emplace(key, path);
			}
			if (!files.empty())
				LogMessage("Texture overrides: " + std::to_string(files.size()) + " .dds files in " + root.string());
			return files;
		}

		std::string OverrideKey(UTexture* texture)
		{
			std::string key = texture->Name.ToString();
			for (UObject* outer = texture->Outer(); outer; outer = outer->Outer())
				key = outer->Name.ToString() + "/" + key;
			return Lower(texture->package->GetPackageName().ToString() + "/" + key);
		}

		uint32_t ReadU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

		// A DDS file's DXT1/3/5 image and its mipmaps, or false for anything else.
		bool LoadDDS(const std::filesystem::path& path, TextureFormat& format, Array<UnrealMipmap>& mips)
		{
			std::ifstream file(path, std::ios::binary);
			std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			if (data.size() < 128 || memcmp(data.data(), "DDS ", 4) != 0 || ReadU32(&data[4]) != 124)
				return false;

			int height = (int)ReadU32(&data[12]);
			int width = (int)ReadU32(&data[16]);
			int mipCount = std::max((int)ReadU32(&data[28]), 1);
			uint32_t fourCC = ReadU32(&data[84]);
			int blockBytes;
			if (fourCC == ReadU32((const uint8_t*)"DXT1")) { format = TextureFormat::BC1; blockBytes = 8; }
			else if (fourCC == ReadU32((const uint8_t*)"DXT3")) { format = TextureFormat::BC2; blockBytes = 16; }
			else if (fourCC == ReadU32((const uint8_t*)"DXT5")) { format = TextureFormat::BC3; blockBytes = 16; }
			else return false;
			if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
				return false;

			size_t pos = 128;
			mips.clear();
			for (int level = 0; level < mipCount; level++)
			{
				size_t size = (size_t)((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
				if (pos + size > data.size())
					break;
				UnrealMipmap mip;
				mip.Width = width;
				mip.Height = height;
				mip.Data.resize(size);
				memcpy(mip.Data.data(), &data[pos], size);
				mips.push_back(std::move(mip));
				pos += size;
				if (width == 1 && height == 1)
					break;
				width = std::max(width / 2, 1);
				height = std::max(height / 2, 1);
			}
			return !mips.empty();
		}
	}
}

namespace HP1
{
	bool LoadTextureOverride(UTexture* texture, TextureFormat& format, Array<UnrealMipmap>& mips)
	{
		using namespace Mods;
		if (!Enabled() || !texture || !texture->package || texture->bRealtime() || texture->bParametric() || texture->Class->Name != "Texture")
			return false;
		const auto& files = OverrideFiles();
		if (files.empty())
			return false;
		auto it = files.find(OverrideKey(texture));
		if (it == files.end())
			return false;
		if (!LoadDDS(it->second, format, mips))
		{
			LogMessage("Texture overrides: " + it->second.string() + " is not a DXT1/DXT3/DXT5 .dds file, skipped");
			return false;
		}
		return true;
	}
}
