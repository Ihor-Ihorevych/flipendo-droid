#include "Precomp.h"
#include "Test.h"
#include "GameFolder.h"
#include <filesystem>
#include <fstream>

// GameFolderSelection::GameRootFromPath: what a player gives (the install folder, its System folder, or the exe
// dragged onto SurrealEngine.exe, issue #37) becomes the install folder.

namespace
{
	struct FakeInstall
	{
		std::filesystem::path root;
		FakeInstall()
		{
			root = std::filesystem::temp_directory_path() / "flipendo_test_gamefolder" / "Harry Potter TM";
			std::filesystem::remove_all(root.parent_path());
			std::filesystem::create_directories(root / "System");
			std::ofstream(root / "System" / "HP.exe") << "x";
		}
		~FakeInstall()
		{
			std::error_code ec;
			std::filesystem::remove_all(root.parent_path(), ec);
		}
	};
}

TEST(GameRootFromPath_InstallFolderStays)
{
	FakeInstall game;
	CHECK(GameFolderSelection::GameRootFromPath(game.root.string()) == game.root.string());
}

TEST(GameRootFromPath_TrailingSeparatorAndQuotes)
{
	FakeInstall game;
	CHECK(GameFolderSelection::GameRootFromPath(game.root.string() + "/") == game.root.string());
	CHECK(GameFolderSelection::GameRootFromPath("\"" + game.root.string() + "\"") == game.root.string());
}

TEST(GameRootFromPath_ExeGoesToInstallFolder)
{
	FakeInstall game;
	CHECK(GameFolderSelection::GameRootFromPath((game.root / "System" / "HP.exe").string()) == game.root.string());
}

TEST(GameRootFromPath_SystemFolderGoesUp)
{
	FakeInstall game;
	CHECK(GameFolderSelection::GameRootFromPath((game.root / "System").string()) == game.root.string());
}

TEST(GameRootFromPath_MissingFolderUnchanged)
{
	CHECK(GameFolderSelection::GameRootFromPath("/no/such/folder") == std::string("/no/such/folder"));
	CHECK(GameFolderSelection::GameRootFromPath("") == std::string());
}
