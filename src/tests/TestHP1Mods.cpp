#include "Precomp.h"
#include "Test.h"
#include "mods/HP1Mods.h"

#include <set>

// The mod switches (HP1::Mods::ResolveMod): --no-mod= / --mod= ids, the old per-mod flags, --vanilla and all.

using HP1::Mods::ModInfo;
using HP1::Mods::ResolveMod;

static const ModInfo OnMod = { "improved-movement", true, "HP1", nullptr, "--classic-movement", "" };
static const ModInfo OffMod = { "intro-skip", false, "HP1", "--skip-intro", nullptr, "" };

static bool Resolve(const ModInfo& mod, std::set<std::string> flags, const std::string& onList = "", const std::string& offList = "")
{
	return ResolveMod(mod, [&](const std::string& flag) { return flags.count(flag) != 0; }, onList, offList);
}

TEST(ResolveMod_Defaults)
{
	CHECK(Resolve(OnMod, {}));
	CHECK(!Resolve(OffMod, {}));
}

TEST(ResolveMod_IdLists)
{
	CHECK(!Resolve(OnMod, {}, "", "improved-movement"));
	CHECK(!Resolve(OnMod, {}, "", "discord,improved-movement"));
	CHECK(Resolve(OffMod, {}, "intro-skip"));
	CHECK(Resolve(OffMod, {}, "splash-skip,intro-skip,discord"));
	CHECK(!Resolve(OffMod, {}, "intro-ski"));
	// named in both: off wins
	CHECK(!Resolve(OffMod, {}, "intro-skip", "intro-skip"));
}

TEST(ResolveMod_OldFlags)
{
	CHECK(!Resolve(OnMod, { "--classic-movement" }));
	CHECK(Resolve(OffMod, { "--skip-intro" }));
	// an id beats the old flag
	CHECK(Resolve(OnMod, { "--classic-movement" }, "improved-movement"));
}

TEST(ResolveMod_VanillaAndAll)
{
	CHECK(!Resolve(OnMod, { "--vanilla" }));
	CHECK(!Resolve(OnMod, {}, "", "all"));
	CHECK(Resolve(OffMod, {}, "all"));
	// a mod named on, or turned on by its old flag, runs with --vanilla too (--vanilla --skip-splash)
	CHECK(Resolve(OnMod, { "--vanilla" }, "improved-movement"));
	CHECK(Resolve(OffMod, { "--vanilla", "--skip-intro" }));
	CHECK(Resolve(OnMod, {}, "improved-movement", "all"));
}

TEST(ModList_IdsUnique)
{
	std::set<std::string> ids;
	for (const ModInfo& mod : HP1::Mods::ModList())
		CHECK(ids.insert(mod.Id).second);
	CHECK(ids.count("improved-movement") == 1);
}

TEST(FreeCamera_DefaultOff)
{
	bool found = false;
	for (const ModInfo& mod : HP1::Mods::ModList())
	{
		if (std::string(mod.Id) != "free-cam")
			continue;
		found = true;
		CHECK(!Resolve(mod, {}));
		CHECK(!Resolve(mod, { "--vanilla" }));
		CHECK(Resolve(mod, { "--vanilla" }, "free-cam"));
		CHECK(!Resolve(mod, {}, "free-cam", "free-cam"));
	}
	CHECK(found);
}

TEST(FreeCamera_IndependentOfImprovedMovement)
{
	const ModInfo* camera = nullptr;
	const ModInfo* movement = nullptr;
	for (const ModInfo& mod : HP1::Mods::ModList())
	{
		if (std::string(mod.Id) == "free-cam") camera = &mod;
		if (std::string(mod.Id) == "improved-movement") movement = &mod;
	}
	CHECK(camera && movement);
	if (!camera || !movement)
		return;
	CHECK(Resolve(*camera, { "--vanilla" }, "free-cam"));
	CHECK(!Resolve(*movement, { "--vanilla" }, "free-cam"));
	CHECK(!Resolve(*camera, { "--vanilla" }, "improved-movement"));
	CHECK(Resolve(*movement, { "--vanilla" }, "improved-movement"));
	CHECK(Resolve(*camera, { "--vanilla" }, "free-cam,improved-movement"));
	CHECK(Resolve(*movement, { "--vanilla" }, "free-cam,improved-movement"));
	CHECK(!Resolve(*movement, {}, "free-cam", "improved-movement"));
	CHECK(!Resolve(*camera, {}, "improved-movement", "free-cam"));
}
