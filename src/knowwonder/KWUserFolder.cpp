#include "Precomp.h"
#include "KW.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include <fstream>

// Where Flipendo writes a game's saves and its own ini files (SE-HP.ini, SE-User.ini). The original writes both next
// to the game (System\, SavePath=..\Save), and so does Flipendo whenever it can. An install under Program Files can't
// be written without administrator rights: the original HP.exe has no manifest, so Windows quietly moves its writes
// to %LOCALAPPDATA%\VirtualStore, but SurrealEngine.exe declares asInvoker and gets "access denied" instead. Every
// save then failed and the saving screen's text stayed up (issue #37). Such an install gets a folder of the player's
// own instead, laid out like the game folder (System\, Save\).
// NOT reversed: Flipendo's own behaviour, the original has no such fallback.

namespace KW
{
	namespace fs = std::filesystem;

	bool FolderWritable(const fs::path& folder)
	{
		std::error_code ec;
		if (!fs::is_directory(folder, ec))
			return false;
		fs::path probe = folder / "Flipendo-write-test.tmp";
		bool written = false;
		{
			std::ofstream file(probe, std::ios::binary | std::ios::trunc);
			if (file)
			{
				file << "x";
				file.flush();
				written = file.good();
			}
		}
		fs::remove(probe, ec);
		return written;
	}

	std::string UserFolderName(const fs::path& gameRoot)
	{
		// The install folder's name plus a hash of its full path (FNV-1a, ASCII letters folded: Windows paths ignore
		// case), so two installs with the same folder name don't share saves.
		std::string full = fs::absolute(gameRoot).lexically_normal().generic_string();
		while (full.size() > 1 && full.back() == '/')
			full.pop_back();
		uint32_t hash = 2166136261u;
		for (char c : full)
		{
			hash ^= (uint8_t)std::tolower((unsigned char)c);
			hash *= 16777619u;
		}

		std::string name = fs::path(full).filename().string();
		for (char& c : name)
		{
			if (c == ':' || c == '\\' || c == '/' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				c = '_';
		}
		if (name.empty())
			name = "Game";

		char hex[9];
		std::snprintf(hex, sizeof(hex), "%08x", hash);
		return name + "-" + hex;
	}

	fs::path WritableSystemFolder(const fs::path& gameRoot, const fs::path& systemFolder)
	{
		fs::path userRoot;
		try
		{
			userRoot = Directory::localAppData() / "Flipendo" / UserFolderName(gameRoot);
		}
		catch (const std::exception& e)
		{
			LogMessage(std::string("No folder of the player's own: ") + e.what());
			return systemFolder;
		}
		fs::path userSystem = userRoot / systemFolder.filename();

		// Once an install has its own folder it keeps it, even when the game folder becomes writable later (run as
		// administrator once), or its saves would seem to vanish.
		std::error_code ec;
		if (!fs::is_directory(userSystem, ec))
		{
			if (FolderWritable(systemFolder))
				return systemFolder;
			fs::create_directories(userSystem, ec);
			if (ec)
			{
				LogMessage("The game folder can't be written and " + userSystem.string() + " can't be created: " + ec.message());
				return systemFolder;
			}
		}
		LogMessage("Saves and settings are kept in " + userRoot.string() + ", not in the game folder");
		return userSystem;
	}
}
