// Spike: Android resource loader (no GTK/fontconfig).
#include "surrealwidgets/core/resourcedata.h"
#include <fstream>
#include <stdexcept>

static std::vector<uint8_t> ReadAllBytesFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	if (!file)
		throw std::runtime_error("Could not open: " + filename);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> buffer(size);
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
		throw std::runtime_error("Could not read: " + filename);
	return buffer;
}

std::vector<SingleFontData> ResourceData::LoadSystemFont()
{
	return { SingleFontData{ReadAllBytesFile("/system/fonts/Roboto-Regular.ttf"), ""} };
}

std::vector<SingleFontData> ResourceData::LoadMonospaceSystemFont()
{
	return { SingleFontData{ReadAllBytesFile("/system/fonts/DroidSansMono.ttf"), ""} };
}

double ResourceData::GetSystemFontSize()
{
	return 11.0;
}

class ResourceLoaderAndroid : public ResourceLoader
{
public:
	std::vector<SingleFontData> LoadFont(const std::string& name) override
	{
		if (name == "system")
			return ResourceData::LoadSystemFont();
		else if (name == "monospace")
			return ResourceData::LoadMonospaceSystemFont();
		else
			return { SingleFontData{ReadAllBytesFile(name + ".ttf"), ""} };
	}

	std::vector<uint8_t> ReadAllBytes(const std::string& filename) override
	{
		return ReadAllBytesFile(filename);
	}
};

static std::unique_ptr<ResourceLoader>& GetLoader()
{
	static std::unique_ptr<ResourceLoader> loader = std::make_unique<ResourceLoaderAndroid>();
	return loader;
}

ResourceLoader* ResourceLoader::Get()
{
	return GetLoader().get();
}

void ResourceLoader::Set(std::unique_ptr<ResourceLoader> instance)
{
	GetLoader() = std::move(instance);
}
