#include "Precomp.h"
#include "KW.h"
#include "KWGame.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Utils/File.h"
#include "Utils/UTF16.h"
#include "Utils/StrTools.h"
#include "VM/NativeFunc.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/UViewport.h"
#include "Render/RenderSubsystem.h"
#include "VM/ScriptCall.h"

// Save games (docs/re/engine/savegames.md). The level itself is saved by the SaveGame console command (upstream's
// Engine::SaveGameToSlot writes <SavePath>/SaveN.usa) and loaded by "open saveN.usa" (KW::SaveGameLoadURL).
// Next to it the menus keep a GameSaveInfo per slot (beans, stars, house points, save point, level name) in
// <SavePath>/GameSaveInfoN, and show each slot's thumbnail from a BMP in the Save folder.

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// The original builds these paths as GSys->SavePath * dir; SavePath (and the thumbnail names the menus pass,
	// "..\Save\SGS <level>.bmp") are relative to the System folder.
	static fs::path GameRelativePath(const std::string& path)
	{
		return (engine->packages->GetSystemFolderPath() / convert_path_separators(path)).lexically_normal();
	}

	static void WriteInt32(Array<uint8_t>& out, int32_t v)
	{
		for (int i = 0; i < 4; i++)
			out.push_back((uint8_t)(((uint32_t)v) >> (i * 8)));
	}

	// FArchive << FCompactIndex: sign + 6 bits, then 7 bits per byte.
	static void WriteCompactIndex(Array<uint8_t>& out, int32_t value)
	{
		uint32_t v = value < 0 ? (uint32_t)-(int64_t)value : (uint32_t)value;
		uint8_t b0 = (value < 0 ? 0x80 : 0) | (v & 0x3f);
		v >>= 6;
		if (v) b0 |= 0x40;
		out.push_back(b0);
		while (v)
		{
			uint8_t b = v & 0x7f;
			v >>= 7;
			if (v) b |= 0x80;
			out.push_back(b);
		}
	}

	// IDA Core.dll: ??6@YAAAVFArchive@@AAV0@AAVFString@@@Z [HP1 Core 0x10150830]
	// Length including the terminator; negative = UTF-16 (only when a character is above 0xFF), else one byte per char.
	static void WriteFString(Array<uint8_t>& out, const std::string& s)
	{
		std::wstring w = to_utf16(s);
		bool wide = false;
		for (wchar_t c : w)
			wide = wide || c > 0xff;
		int len = w.empty() ? 0 : (int)w.size() + 1;
		WriteCompactIndex(out, wide ? -len : len);
		for (int i = 0; i < len; i++)
		{
			wchar_t c = i < (int)w.size() ? w[i] : 0;
			out.push_back((uint8_t)c);
			if (wide)
				out.push_back((uint8_t)(c >> 8));
		}
	}

	class SaveInfoReader
	{
	public:
		SaveInfoReader(const Array<uint8_t>& data) : data(data) {}

		bool ReadInt32(int32_t& v)
		{
			if (pos + 4 > data.size())
				return false;
			v = (int32_t)(data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | ((uint32_t)data[pos + 3] << 24));
			pos += 4;
			return true;
		}

		bool ReadByte(uint8_t& b)
		{
			if (pos >= data.size())
				return false;
			b = data[pos++];
			return true;
		}

		bool ReadCompactIndex(int32_t& value)
		{
			uint8_t b0;
			if (!ReadByte(b0))
				return false;
			uint32_t v = b0 & 0x3f;
			if (b0 & 0x40)
			{
				int shift = 6;
				uint8_t b;
				do
				{
					if (!ReadByte(b) || shift > 27)
						return false;
					v |= (uint32_t)(b & 0x7f) << shift;
					shift += 7;
				} while (b & 0x80);
			}
			value = (b0 & 0x80) ? -(int32_t)v : (int32_t)v;
			return true;
		}

		bool ReadFString(std::string& s)
		{
			int32_t len;
			if (!ReadCompactIndex(len))
				return false;
			bool wide = len < 0;
			if (wide)
				len = -len;
			std::wstring w;
			for (int32_t i = 0; i < len; i++)
			{
				uint8_t lo, hi = 0;
				if (!ReadByte(lo) || (wide && !ReadByte(hi)))
					return false;
				w.push_back((wchar_t)(lo | (hi << 8)));
			}
			if (!w.empty() && w.back() == 0)
				w.pop_back();
			s = from_utf16(w);
			return true;
		}

	private:
		const Array<uint8_t>& data;
		size_t pos = 0;
	};

	// GameSaveInfo's fields, in the order the natives write them (the script's var order, from offset 0x28).
	static const char* SaveInfoInts[] = { "numBeans", "numStars", "numPoints", "savePointID" };

	// IDA Engine.dll: ?execSaveGameSaveInfo@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10413D10]
	// "Sto: all objects in this class are individually saved": four int32 and the FString currentLevelString, raw.
	static void NSaveGameSaveInfo(UObject* Self, const std::string& dir, UObject* object, BitfieldBool& ReturnValue)
	{
		ReturnValue = false;
		if (!object)
			return;

		Array<uint8_t> data;
		for (const char* name : SaveInfoInts)
			WriteInt32(data, (int32_t)object->GetInt(name));
		WriteFString(data, object->GetString("currentLevelString"));

		try
		{
			fs::path folder = engine->packages->GetSaveFolderPath();
			if (!fs::exists(folder))
				fs::create_directories(folder);
			File::write_all_bytes((folder / convert_path_separators(dir)).string(), data.data(), data.size());
			ReturnValue = true;
		}
		catch (const std::exception& e)
		{
			LogMessage("SaveGameSaveInfo: " + std::string(e.what()));
		}
	}

	// IDA Engine.dll: ?execLoadGameSaveInfo@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10413F70]
	// False if the object is None or there is no such file (an empty slot).
	static void NLoadGameSaveInfo(UObject* Self, const std::string& dir, UObject* object, BitfieldBool& ReturnValue)
	{
		ReturnValue = false;
		if (!object)
			return;

		fs::path path = engine->packages->GetSaveFolderPath() / convert_path_separators(dir);
		if (!fs::is_regular_file(path))
			return;

		Array<uint8_t> data;
		try
		{
			data = File::read_all_bytes(path.string());
		}
		catch (const std::exception&)
		{
			return;
		}

		SaveInfoReader reader(data);
		int32_t values[4];
		std::string levelString;
		for (int32_t& v : values)
		{
			if (!reader.ReadInt32(v))
				return;
		}
		if (!reader.ReadFString(levelString))
			return;

		for (int i = 0; i < 4; i++)
			object->SetInt(SaveInfoInts[i], (uint32_t)values[i]);
		object->SetString("currentLevelString", levelString);
		ReturnValue = true;
	}

	// IDA Engine.dll: ?execSaveGameExists@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C420]
	// Whether "%s\Save%i.usa" (SavePath, 9) exists and isn't empty. Only commented-out script code calls it.
	static void NSaveGameExists(UObject* Self, BitfieldBool& ReturnValue)
	{
		fs::path path = engine->packages->GetSaveFolderPath() / "Save9.usa";
		std::error_code ec;
		ReturnValue = fs::is_regular_file(path, ec) && fs::file_size(path, ec) > 0;
	}

	bool ReadBMP(const Array<uint8_t>& f, int& width, int& height, Array<uint8_t>& bgra)
	{
		auto u16 = [&](size_t o) { return (uint32_t)(f[o] | (f[o + 1] << 8)); };
		auto u32 = [&](size_t o) { return (uint32_t)(f[o] | (f[o + 1] << 8) | (f[o + 2] << 16) | ((uint32_t)f[o + 3] << 24)); };

		if (f.size() < 54 || f[0] != 'B' || f[1] != 'M')
			return false;
		uint32_t dataOffset = u32(10);
		uint32_t headerSize = u32(14);
		int32_t w = (int32_t)u32(18);
		int32_t h = (int32_t)u32(22);
		uint32_t bpp = u16(28);
		uint32_t compression = u32(30);
		uint32_t colorsUsed = u32(46);
		if (w <= 0 || h == 0 || compression != 0 || (bpp != 8 && bpp != 24 && bpp != 32))
			return false;

		bool bottomUp = h > 0;
		if (h < 0)
			h = -h;
		size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
		if (dataOffset + stride * h > f.size())
			return false;

		size_t paletteOffset = 14 + headerSize;
		uint32_t paletteCount = colorsUsed ? colorsUsed : 256;
		if (bpp == 8 && paletteOffset + paletteCount * 4 > f.size())
			return false;

		width = w;
		height = h;
		bgra.resize((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
		{
			const uint8_t* src = f.data() + dataOffset + stride * (bottomUp ? h - 1 - y : y);
			uint8_t* dst = bgra.data() + (size_t)y * w * 4;
			for (int x = 0; x < w; x++, dst += 4)
			{
				if (bpp == 8)
				{
					uint32_t index = src[x] < paletteCount ? src[x] : 0;
					const uint8_t* c = f.data() + paletteOffset + index * 4; // B, G, R, reserved
					dst[0] = c[0]; dst[1] = c[1]; dst[2] = c[2];
				}
				else
				{
					const uint8_t* c = src + x * (bpp / 8);
					dst[0] = c[0]; dst[1] = c[1]; dst[2] = c[2];
				}
				dst[3] = 255;
			}
		}
		return true;
	}

	static uint8_t SizeBits(int size)
	{
		uint8_t bits = 0;
		while ((1 << (bits + 1)) <= size)
			bits++;
		return bits;
	}

	// The file in path's folder whose name matches path's ignoring case, or empty. The retail SGS thumbnails don't
	// match the case the slot pages ask for ("SGS Lev_tut1.bmp" for Lev_Tut1), which only Windows ignores.
	static fs::path FindFileIgnoringCase(const fs::path& path)
	{
		auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
		std::string name = lower(path.filename().string());
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(path.parent_path(), ec))
		{
			if (entry.is_regular_file(ec) && lower(entry.path().filename().string()) == name)
				return entry.path();
		}
		return {};
	}

	// IDA Engine.dll: ?execCreateTextureFromBMP@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x10413630]
	// The original imports the file as texture <name> into the package SavePics (TextureFactory); None if it can't
	// be read. The save slot pages use it for the slot thumbnails ("..\Save\SGS <level><savepoint>.bmp").
	// Ours goes into the transient package as a BGRA8 texture (the import's P8 + palette looks the same).
	static void NCreateTextureFromBMP(UObject* Self, const std::string& name, const std::string& filename, UObject*& ReturnValue)
	{
		ReturnValue = nullptr;

		fs::path path = GameRelativePath(filename);
		if (!fs::is_regular_file(path))
			path = FindFileIgnoringCase(path);
		if (path.empty())
			return;

		Array<uint8_t> file;
		try
		{
			file = File::read_all_bytes(path.string());
		}
		catch (const std::exception&)
		{
			return;
		}

		int width = 0, height = 0;
		Array<uint8_t> pixels;
		if (!ReadBMP(file, width, height, pixels))
		{
			LogMessage("CreateTextureFromBMP: can't read " + path.string());
			return;
		}

		Package* transient = engine->packages->GetTransientPackage();
		UTexture* tex = UObject::Cast<UTexture>(transient->NewObject(name, engine->packages->FindClass("Engine.Texture"), ObjectFlags::Transient));
		tex->Format() = (uint8_t)TextureFormat::BGRA8;
		tex->USize() = width;
		tex->VSize() = height;
		tex->UClamp() = width;
		tex->VClamp() = height;
		tex->UBits() = SizeBits(width);
		tex->VBits() = SizeBits(height);
		tex->Palette() = nullptr;

		UnrealMipmap mip;
		mip.Width = width;
		mip.Height = height;
		mip.UBits = tex->UBits();
		mip.VBits = tex->VBits();
		mip.Data = std::move(pixels);
		tex->UncompressedMipmaps.push_back(std::move(mip));
		tex->UsedFormat = TextureFormat::BGRA8;
		tex->UsedMipmaps = tex->UncompressedMipmaps;

		ReturnValue = tex;
	}

	// One frame with the player's FlashFog forced to (0, 0.1, 0.25) at brightness W = 0.2: the world dimmed to dark
	// blue, with the console drawing the LevelAction message on top (baseConsole.DrawLevelAction: LEVACT_Saving shows
	// HPDialog nearly_nick_40, "Your game will restart from this Save Game book."). The frame stays on screen while
	// the save runs.
	// IDA Engine.dll: ?PaintProgress@UGameEngine@@UAEXXZ [HP1 0x10397BA0]
	static void PaintProgress()
	{
		UPlayerPawn* player = engine->viewport ? UObject::TryCast<UPlayerPawn>(engine->viewport->Actor()) : nullptr;
		if (!player)
			return;
		float* fog = &player->FlashFog().x; // FPlane: X, Y, Z, W
		float saved[4] = { fog[0], fog[1], fog[2], fog[3] };
		fog[0] = 0.0f; fog[1] = 0.1f; fog[2] = 0.25f; fog[3] = 0.2f;
		// UGameEngine::Draw [HP1 0x1039FA40] calls PlayerCalcView for each frame. The level-start save runs from HPConsole.Tick before the
		// main loop's view of the new level exists; without this the frame was drawn from the last level's camera spot
		// (views from outside the map).
		engine->CalcView();
		engine->render->DrawGame(0.0f);
		for (int i = 0; i < 4; i++)
			fog[i] = saved[i];
	}

	// HP2: every actor gets PreSaveGame before the save and PostSaveGame after it (harry copies its status into its
	// travel properties, a SavePoint tops Harry's health up for the save).
	static void RaiseSaveEvent(const char* name)
	{
		if (!engine->Level)
			return;
		Array<UActor*> actors = engine->Level->Actors; // a handler may spawn or destroy
		for (UActor* actor : actors)
		{
			if (actor && !actor->bDeleteMe())
				CallEvent(actor, name);
		}
	}

	// HP2: the slot's picture for the front end, <System>\Thumbnails\<map>.bmp (or Default.bmp) copied to
	// <save folder>\Save0.bmp. The map is LevelInfo.LevelEnterText after its last backslash, cut at the first dot.
	// IDA Engine.dll: ?SetThumbnailForSaveSlot@UGameEngine@@QAE_NXZ [HP2 0x103ABB00]
	static bool SetThumbnailForSaveSlot()
	{
		if (!engine->LevelInfo)
			return false;
		std::string map = engine->LevelInfo->LevelEnterText();
		size_t slash = map.find_last_of('\\');
		if (slash != std::string::npos)
			map = map.substr(slash + 1);
		size_t dot = map.find('.');
		if (dot != std::string::npos)
			map = map.substr(0, dot);
		fs::path folder = engine->packages->GetSystemFolderPath() / "Thumbnails";
		fs::path thumbnail = FindIgnoringCase(folder, map + ".bmp");
		if (thumbnail.empty())
			thumbnail = FindIgnoringCase(folder, "Default.bmp");
		if (thumbnail.empty())
			return false;
		std::error_code ec;
		fs::copy_file(thumbnail, engine->packages->GetSaveFolderPath() / "Save0.bmp", fs::copy_options::overwrite_existing, ec);
		return !ec;
	}

	// The SaveGame console command, and HP2's queued saves (HP2::TickQueuedSave): LevelAction is LEVACT_Saving (2)
	// while the save runs, and one progress frame is drawn before it. The level package is written by upstream's
	// Engine::SaveGameToSlot. Hub copies and the movers' saved positions aren't ported, nor HP2's persistent actor
	// cache (docs/re/engine/savegames.md).
	// IDA Engine.dll: ?SaveGame@UGameEngine@@UAEXH@Z [HP1 0x103A2280] [HP2 0x103AB430]
	void SaveGame(int slot, const std::string& description)
	{
		ULevelInfo* level = engine->LevelInfo;
		if (level)
			level->LevelAction() = 2;
		if (IsHP2())
			RaiseSaveEvent("PreSaveGame");
		// HP2 draws no progress frame while the player's loading screen is up (a save on arriving in a level)
		if (!IsHP2() || !(engine->viewport && BoolProperty(engine->viewport->Actor(), "bShowLoadingScreen")))
			PaintProgress();
		engine->SaveGameToSlot(slot, description);
		if (level)
			level->LevelAction() = 0;
		if (IsHP2())
		{
			SetThumbnailForSaveSlot();
			RaiseSaveEvent("PostSaveGame");
		}
	}

	std::string SaveGameLoadURL(const std::string& map)
	{
		// FESlotPage.LoadSelectedSlot: ConsoleCommand("open save" $nSelectedSlot $".usa"). HP1's ini has
		// Paths=../save/*.usa, so the original loads the save file like any map, and LoadMap sees from the saved
		// LevelInfo.bBegunPlay that it's a save (no InitGame/BeginPlay, the saved player is possessed). Upstream does the
		// same for "?load=N".
		std::string stem = fs::path(map).stem().string();
		std::string ext = fs::path(map).extension().string();
		if (stem.size() <= 4 || !StrTools::equals_ignore_case(stem.substr(0, 4), "save"))
			return {};
		if (!ext.empty() && !StrTools::equals_ignore_case(ext, "." + engine->packages->GetSaveExtension()))
			return {};
		std::string number = stem.substr(4);
		if (number.find_first_not_of("0123456789") != std::string::npos)
			return {};
		if (!fs::is_regular_file(engine->packages->GetSaveFolderPath() / ("Save" + number + "." + engine->packages->GetSaveExtension())))
			return {};
		return "?load=" + number;
	}

	// IDA Engine.dll: ?LoadMap@UGameEngine@@UAEPAVULevel@@ABVFURL@@PAVUPendingLevel@@PBV?$TMap@VFString@@V1@@@AAVFString@@@Z [HP1 0x1039C3D0]
	// (at 0x1039CBA6: StaticLoadObject(ALevelInfo, "LevelInfo0", URL.Map), then the copy if appStricmp(LevelEnterText, "") == 0)
	// No HP1 map sets LevelEnterText, so it is the map name the level was travelled to with (with ".unr" when the
	// URL had it). A save keeps the text it was saved with.
	void LevelInfoLoaded(ULevelInfo* levelInfo, const std::string& urlMap)
	{
		if (levelInfo && levelInfo->LevelEnterText().empty())
			levelInfo->LevelEnterText() = urlMap;
	}

	// "Snap N" (FEBook.OpenBook and HPConsole run "Snap 3" when the menu book opens): the original copies the viewport's
	// frame, shrunk by 2^N, into the viewport's snapshot buffer, for "SaveSnap <file>" / "SaveSnap128" and
	// Actor.CreateTextureFromScreenShot to write or wrap later. HP1 never uses that buffer (the SaveSnap calls are
	// commented out, no script calls CreateTextureFromScreenShot; the slot thumbnails are the pre-made SGS BMPs), so
	// the command is accepted and does nothing.
	// IDA Engine.dll: ?Exec@UViewport@@UAEHPBGAAVFOutputDevice@@@Z [HP1 0x10384560] (the "SNAP" branch)
	// IDA Render.dll: ?Exec@URender@@UAEHPBGAAVFOutputDevice@@@Z [HP2 Render 0x10B1EB90]
	// MLOD <factor> (HP2's object detail option) sets URender's global mesh LOD factor when it is above 0 and logs it.
	// Meshes here are always drawn at full detail, so it is accepted and ignored.
	bool ViewportCommand(const Array<std::string>& args)
	{
		if (args.empty())
			return false;
		return StrTools::equals_ignore_case(args[0], "snap") || StrTools::equals_ignore_case(args[0], "mlod");
	}

	// A save stores each actor's Region, BSP leaf included, and the original game trusts it. Saves made by Flipendo before
	// UModel::FindRegion took the leaf from the right side of the node (2026-10-05) hold the other side's leaf, often -1;
	// a mover keeps it until it moves, and a bDynamicLightMover with leaf -1 gets no lights (Lev_Tut1b's dropped
	// portcullis showed black after a load). Recomputing the leaf from the location is a no-op for good saves.
	void SaveGameLoaded()
	{
		if (!engine->Level)
			return;
		for (UActor* actor : engine->Level->Actors)
		{
			if (actor && actor->XLevel())
				actor->Region().BspLeaf = actor->FindRegion().BspLeaf;
		}
	}

	void RegisterSaveNatives()
	{
		OverrideNative(321, [] { RegisterVMNativeFunc_3("Actor", "CreateTextureFromBMP", &NCreateTextureFromBMP, 321); });
		OverrideNative(325, [] { RegisterVMNativeFunc_3("Actor", "SaveGameSaveInfo", &NSaveGameSaveInfo, 325); });
		OverrideNative(326, [] { RegisterVMNativeFunc_3("Actor", "LoadGameSaveInfo", &NLoadGameSaveInfo, 326); });
		OverrideNative(3972, [] { RegisterVMNativeFunc_1("Actor", "SaveGameExists", &NSaveGameExists, 3972); });

		// The latent action ID a suspended state is saved with. KnowWonder's Sleep and FinishAnim write 384 and 385
		// (both games) where upstream registers 257 and 262; registering ours last makes saves write these, so the
		// original game's saves resume a Sleep / FinishAnim instead of continuing at once. 257/262 still load.
		// Read from AActor::execSleep [HP1 0x104081E0] [HP2 0x10415A30] and execFinishAnim [HP1 0x10408250] [HP2 0x10415AC0].
		RegisterLatentAction(384, LatentRunState::Sleep);
		RegisterLatentAction(385, LatentRunState::FinishAnim);
	}
}
