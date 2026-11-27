#include "Precomp.h"
#include "Test.h"
#include "KW.h"
#include <filesystem>

// KW::UserFolderName / FolderWritable (src/knowwonder/KWUserFolder.cpp): the player's own folder for an install under
// Program Files, which can't be written (issue #37).

TEST(UserFolderName_NameAndHash)
{
	std::string name = KW::UserFolderName("/games/EA Games/Harry Potter TM");
	CHECK(name.rfind("Harry Potter TM-", 0) == 0);
	CHECK_EQ(name.size(), std::string("Harry Potter TM-").size() + 8);
}

TEST(UserFolderName_StableAndPerInstall)
{
	CHECK(KW::UserFolderName("/games/a/Harry Potter TM") == KW::UserFolderName("/games/a/Harry Potter TM"));
	CHECK(KW::UserFolderName("/games/a/Harry Potter TM") == KW::UserFolderName("/games/a/Harry Potter TM/"));
	CHECK(KW::UserFolderName("/games/a/Harry Potter TM") != KW::UserFolderName("/games/b/Harry Potter TM"));
}

TEST(UserFolderName_CaseOfPathIgnoredInHash)
{
	// Only the hash ignores case; the visible name keeps the folder's own spelling
	std::string a = KW::UserFolderName("/games/a/Harry Potter TM");
	std::string b = KW::UserFolderName("/GAMES/A/HARRY POTTER TM");
	CHECK(a.substr(a.size() - 8) == b.substr(b.size() - 8));
}

TEST(FolderWritable_TempAndMissing)
{
	std::filesystem::path folder = std::filesystem::temp_directory_path() / "flipendo_test_userfolder";
	std::filesystem::remove_all(folder);
	CHECK(!KW::FolderWritable(folder));
	std::filesystem::create_directories(folder);
	CHECK(KW::FolderWritable(folder));
	CHECK(std::filesystem::is_empty(folder));
	std::filesystem::remove_all(folder);
}
