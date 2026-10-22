#include "Precomp.h"
#include "HP2.h"
#include "KW.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UArrayProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Core/Properties/UByteProperty.h"
#include "Packages/Core/Properties/UFloatProperty.h"
#include "Packages/Core/Properties/UIntProperty.h"
#include "Packages/Core/Properties/UNameProperty.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Core/Properties/UStringProperty.h"
#include "Packages/Core/Properties/UStrProperty.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/Logger.h"
#include "Utils/StrTools.h"
#include "Utils/UTF16.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>

// HP2's persistent actors (docs/re/hp2/engine.md#persistent-actors-across-hub-travel). Harry walks between hub maps
// and back; before he leaves one, harry.LoadLevel runs the SAVEPACTORS console command, which writes every bPersistent
// actor's properties to <save folder>/cache/<map>_pa.usa. When the map loads again its persistent actors take those
// properties back, the ones the file doesn't list (a bean Harry picked up) are destroyed and the ones only the file
// lists (what a GenericSpawner spawned) are spawned. Saving the game copies the cache files into the slot folder,
// starting and loading a game copies them back. The file is written as the original writes it, so a save folder
// from the original game works here and the other way round.

namespace HP2
{
	namespace
	{
		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
			return s;
		}

		// Tagged property types, as UE1's property tags number them
		enum : uint8_t { TagByte = 1, TagInt = 2, TagBool = 3, TagFloat = 4, TagObject = 5, TagName = 6, TagString = 7, TagArray = 9, TagStruct = 10, TagStr = 13 };

		// What the file holds is written through FFileManagerWindows' file writer (Game.exe): names as strings
		// (int32 byte count of the UTF-16 text and its terminator, then the text), object references as nothing.
		// A tag's size comes from a counting archive that takes 4 bytes for either, as for a package; the size is
		// only used to skip a property that can't be read, so the difference never matters to the original.
		class Writer
		{
		public:
			explicit Writer(bool counting = false) : counting(counting) {}

			void Bytes(const void* data, size_t size)
			{
				if (counting)
					count += size;
				else
					buffer.insert(buffer.end(), (const uint8_t*)data, (const uint8_t*)data + size);
			}
			void UInt8(uint8_t value) { Bytes(&value, 1); }
			void Int32(int32_t value) { Bytes(&value, 4); }
			void Int64(int64_t value) { Bytes(&value, 8); }

			// IDA Game.exe: not exported: sub_10909740 [HP2 Game 0x10909740] (No-CD exe; the file writer's operator<<(FName&), vtable +28 of the writer CreateFileWriter makes)
			void Text(const std::string& text)
			{
				std::wstring wide = to_utf16(text);
				int32_t size = (int32_t)(wide.size() * 2 + 2);
				Int32(size);
				for (wchar_t c : wide)
				{
					uint16_t unit = c <= 0xffff ? (uint16_t)c : (uint16_t)'?';
					Bytes(&unit, 2);
				}
				UInt8(0);
				UInt8(0);
			}

			void Name(const NameString& name)
			{
				if (counting)
					count += 4;
				else
					Text(name.ToString());
			}

			void Object()
			{
				if (counting)
					count += 4;
			}

			// FCompactIndex
			void Index(int32_t value)
			{
				uint32_t v = value < 0 ? (uint32_t)-(int64_t)value : (uint32_t)value;
				uint8_t b = (uint8_t)((value < 0 ? 0x80 : 0) | (v & 0x3f) | (v >= 0x40 ? 0x40 : 0));
				UInt8(b);
				if (v >= 0x40)
				{
					v >>= 6;
					while (true)
					{
						b = (uint8_t)((v & 0x7f) | (v >= 0x80 ? 0x80 : 0));
						UInt8(b);
						if (v < 0x80)
							break;
						v >>= 7;
					}
				}
			}

			// IDA Core.dll: ??6@YAAAVFArchive@@AAV0@AAVFString@@@Z [HP2 Core 0x10151F10]
			// The character count with its terminator (0 for an empty string), negative when a character doesn't fit
			// a byte; then the characters as bytes or as UTF-16.
			void String(const std::string& text)
			{
				std::wstring wide = to_utf16(text);
				if (wide.empty())
				{
					Index(0);
					return;
				}
				bool ansi = true;
				for (wchar_t c : wide)
					ansi = ansi && c <= 0xff;
				int32_t num = (int32_t)wide.size() + 1;
				Index(ansi ? num : -num);
				for (size_t i = 0; i <= wide.size(); i++)
				{
					wchar_t c = i < wide.size() ? wide[i] : 0;
					if (ansi)
					{
						UInt8((uint8_t)c);
					}
					else
					{
						uint16_t unit = c <= 0xffff ? (uint16_t)c : (uint16_t)'?';
						Bytes(&unit, 2);
					}
				}
			}

			size_t Size() const { return counting ? count : buffer.size(); }

			std::vector<uint8_t> buffer;

		private:
			bool counting = false;
			size_t count = 0;
		};

		class Reader
		{
		public:
			// Copies share the data: a copy reads ahead without moving the original
			explicit Reader(const std::vector<uint8_t>& data) : data(&data) {}

			bool Bytes(void* dest, size_t size)
			{
				if (error || pos + size > data->size())
				{
					error = true;
					return false;
				}
				memcpy(dest, data->data() + pos, size);
				pos += size;
				return true;
			}
			uint8_t UInt8() { uint8_t v = 0; Bytes(&v, 1); return v; }
			int32_t Int32() { int32_t v = 0; Bytes(&v, 4); return v; }
			int64_t Int64() { int64_t v = 0; Bytes(&v, 8); return v; }
			void Skip(size_t size)
			{
				if (pos + size > data->size())
					error = true;
				else
					pos += size;
			}

			std::string Text()
			{
				int32_t size = Int32();
				if (error || size < 0 || pos + (size_t)size > data->size())
				{
					error = true;
					return {};
				}
				std::wstring wide;
				for (int32_t i = 0; i + 1 < size; i += 2)
				{
					uint16_t unit = (uint16_t)((*data)[pos + i] | ((*data)[pos + i + 1] << 8));
					if (unit == 0)
						break;
					wide.push_back((wchar_t)unit);
				}
				pos += size;
				return from_utf16(wide);
			}

			int32_t Index()
			{
				uint8_t b = UInt8();
				bool negative = (b & 0x80) != 0;
				uint32_t value = b & 0x3f;
				if (b & 0x40)
				{
					int shift = 6;
					do
					{
						b = UInt8();
						value |= (uint32_t)(b & 0x7f) << shift;
						shift += 7;
					} while ((b & 0x80) && shift < 32 && !error);
				}
				return negative ? -(int32_t)value : (int32_t)value;
			}

			std::string String()
			{
				int32_t num = Index();
				std::wstring wide;
				for (int32_t i = 0; i < std::abs(num) && !error; i++)
				{
					uint16_t c = 0;
					if (num < 0)
						Bytes(&c, 2);
					else
						c = UInt8();
					if (c != 0)
						wide.push_back((wchar_t)c);
				}
				return from_utf16(wide);
			}

			size_t Tell() const { return pos; }
			void Seek(size_t position) { if (position > data->size()) error = true; else pos = position; }

			bool error = false;

		private:
			const std::vector<uint8_t>* data;
			size_t pos = 0;
		};

		uint8_t TagType(UProperty* prop)
		{
			if (dynamic_cast<UBoolProperty*>(prop)) return TagBool;
			if (dynamic_cast<UByteProperty*>(prop)) return TagByte;
			if (dynamic_cast<UIntProperty*>(prop)) return TagInt;
			if (dynamic_cast<UFloatProperty*>(prop)) return TagFloat;
			if (dynamic_cast<UObjectProperty*>(prop)) return TagObject; // class properties too
			if (dynamic_cast<UNameProperty*>(prop)) return TagName;
			if (dynamic_cast<UStrProperty*>(prop)) return TagStr;
			if (dynamic_cast<UArrayProperty*>(prop)) return TagArray;
			if (dynamic_cast<UStructProperty*>(prop)) return TagStruct;
			return 0; // fixed strings, maps, pointers, fixed arrays: not saved
		}

		bool Skipped(UProperty* prop)
		{
			// native ones and, the file being persistent, transient ones (as UStruct::SerializeTaggedProperties)
			return AnyFlags(prop->PropFlags, PropertyFlags::Native | PropertyFlags::Transient);
		}

		// UProperty::SerializeItem of each type, for one element
		void WriteValue(Writer& w, UProperty* prop, void* data)
		{
			switch (TagType(prop))
			{
			case TagBool: w.UInt8(static_cast<UBoolProperty*>(prop)->GetBool(data) ? 1 : 0); break;
			case TagByte: w.UInt8(*static_cast<uint8_t*>(data)); break;
			case TagInt: w.Int32(*static_cast<int32_t*>(data)); break;
			case TagFloat: w.Bytes(data, 4); break;
			case TagObject: w.Object(); break;
			case TagName: w.Name(*static_cast<NameString*>(data)); break;
			case TagStr: w.String(*static_cast<std::string*>(data)); break;
			case TagArray:
			{
				auto array = static_cast<UArrayProperty*>(prop);
				ScriptArray& vec = *static_cast<ScriptArray*>(data);
				w.Index((int32_t)vec.GetSize());
				for (size_t i = 0; i < vec.GetSize(); i++)
					WriteValue(w, array->Inner, vec.GetItem(i));
				break;
			}
			case TagStruct:
				// UStruct::SerializeBin: every field that isn't native or transient, element by element
				for (UProperty* field : static_cast<UStructProperty*>(prop)->Struct->Properties)
				{
					if (Skipped(field))
						continue;
					for (int i = 0; i < field->ArrayDimension; i++)
						WriteValue(w, field, (uint8_t*)data + field->DataOffset.DataOffset + i * field->ElementPitch());
				}
				break;
			}
		}

		void ReadValue(Reader& r, UProperty* prop, void* data)
		{
			switch (TagType(prop))
			{
			case TagBool: static_cast<UBoolProperty*>(prop)->SetBool(data, r.UInt8() != 0); break;
			case TagByte: *static_cast<uint8_t*>(data) = r.UInt8(); break;
			case TagInt: *static_cast<int32_t*>(data) = r.Int32(); break;
			case TagFloat: r.Bytes(data, 4); break;
			case TagObject: break; // the file holds no object references: the actor keeps its own
			case TagName: *static_cast<NameString*>(data) = NameString(r.Text()); break;
			case TagStr: *static_cast<std::string*>(data) = r.String(); break;
			case TagArray:
			{
				auto array = static_cast<UArrayProperty*>(prop);
				ScriptArray& vec = *static_cast<ScriptArray*>(data);
				int32_t count = r.Index();
				if (r.error || count < 0 || count > 0x100000)
				{
					r.error = true;
					break;
				}
				vec.Resize(count);
				for (int32_t i = 0; i < count && !r.error; i++)
					ReadValue(r, array->Inner, vec.GetItem(i));
				break;
			}
			case TagStruct:
				for (UProperty* field : static_cast<UStructProperty*>(prop)->Struct->Properties)
				{
					if (Skipped(field))
						continue;
					for (int i = 0; i < field->ArrayDimension; i++)
						ReadValue(r, field, (uint8_t*)data + field->DataOffset.DataOffset + i * field->ElementPitch());
				}
				break;
			}
		}

		// IDA Core.dll: not exported: sub_1012E9A0 [HP2 Core 0x1012E9A0] (operator<<(FArchive&, FPropertyTag&); called from SerializeTaggedPersistentProperties)
		// The name; an info byte: the type, the size class (bits 4-6) and bit 7 (a bool's value, or an array index
		// follows); a struct's name; the size when its class doesn't give it; the array index.
		void WriteTag(Writer& w, const NameString& name, uint8_t type, int32_t size, int32_t arrayIndex, bool boolValue, const NameString& structName)
		{
			uint8_t info = type;
			switch (size)
			{
			case 1: break;
			case 2: info |= 0x10; break;
			case 4: info |= 0x20; break;
			case 12: info |= 0x30; break;
			case 16: info |= 0x40; break;
			default: info |= size > 255 ? (size > 0x10000 ? 0x70 : 0x60) : 0x50; break;
			}
			if (arrayIndex != 0 || boolValue)
				info |= 0x80;

			w.Name(name);
			w.UInt8(info);
			if (type == TagStruct)
				w.Name(structName);
			switch (info & 0x70)
			{
			case 0x50: w.UInt8((uint8_t)size); break;
			case 0x60: { uint16_t s = (uint16_t)size; w.Bytes(&s, 2); break; }
			case 0x70: w.Int32(size); break;
			}
			if ((info & 0x80) && type != TagBool)
			{
				uint32_t index = (uint32_t)arrayIndex;
				if (index <= 127)
				{
					w.UInt8((uint8_t)index);
				}
				else if (index <= 0x3fff)
				{
					w.UInt8((uint8_t)((index >> 8) + 0x80));
					w.UInt8((uint8_t)index);
				}
				else
				{
					w.UInt8((uint8_t)((index >> 24) + 0xc0));
					w.UInt8((uint8_t)(index >> 16));
					w.UInt8((uint8_t)(index >> 8));
					w.UInt8((uint8_t)index);
				}
			}
		}

		struct Tag
		{
			std::string name; // empty: the None that ends the list
			uint8_t type = 0;
			bool bit7 = false;
			std::string structName;
			int32_t size = 0;
			int32_t arrayIndex = 0;
		};

		Tag ReadTag(Reader& r)
		{
			Tag tag;
			tag.name = r.Text();
			if (r.error || tag.name.empty() || StrTools::equals_ignore_case(tag.name, "None"))
			{
				tag.name.clear();
				return tag;
			}
			uint8_t info = r.UInt8();
			tag.type = info & 0x0f;
			tag.bit7 = (info & 0x80) != 0;
			if (tag.type == TagStruct)
				tag.structName = r.Text();
			switch (info & 0x70)
			{
			case 0x00: tag.size = 1; break;
			case 0x10: tag.size = 2; break;
			case 0x20: tag.size = 4; break;
			case 0x30: tag.size = 12; break;
			case 0x40: tag.size = 16; break;
			case 0x50: tag.size = r.UInt8(); break;
			case 0x60: { uint16_t s = 0; r.Bytes(&s, 2); tag.size = s; break; }
			case 0x70: tag.size = r.Int32(); break;
			}
			if (tag.bit7 && tag.type != TagBool)
			{
				uint8_t b = r.UInt8();
				if ((b & 0x80) == 0)
					tag.arrayIndex = b;
				else if ((b & 0xc0) == 0x80)
					tag.arrayIndex = ((b & 0x7f) << 8) + r.UInt8();
				else
				{
					uint8_t b2 = r.UInt8(), b1 = r.UInt8(), b0 = r.UInt8();
					tag.arrayIndex = (((((b & 0x3f) << 8) + b2) << 8) + b1 << 8) + b0;
				}
			}
			return tag;
		}

		// IDA Core.dll: ?SerializeTaggedPersistentProperties@UStruct@@QAEXAAVFArchive@@PAEPAVUClass@@@Z [HP2 Core 0x10129300]
		// Saving: every property of the class that isn't native or transient, except AuxAnims, each element of a
		// fixed array with its index, whether or not it differs from the defaults; then None.
		void WriteProperties(Writer& w, UActor* actor)
		{
			for (UProperty* prop : actor->PropertyData.Class->Properties)
			{
				uint8_t type = TagType(prop);
				if (Skipped(prop) || type == 0 || prop->Name == "AuxAnims")
					continue;
				for (int i = 0; i < prop->ArrayDimension; i++)
				{
					void* data = (uint8_t*)actor->GetProperty(prop) + i * prop->ElementPitch();
					bool boolValue = type == TagBool && static_cast<UBoolProperty*>(prop)->GetBool(data);
					NameString structName = type == TagStruct ? static_cast<UStructProperty*>(prop)->Struct->Name : NameString();
					Writer counter(true);
					if (type != TagBool)
						WriteValue(counter, prop, data);
					WriteTag(w, prop->Name, type, (int32_t)counter.Size(), i, boolValue, structName);
					if (type != TagBool)
						WriteValue(w, prop, data);
				}
			}
			w.Name(NameString("None"));
		}

		// Loading: each tag goes to the property of that name and type; one that isn't found, has another type, is
		// out of the array's bounds, is native or transient, or is a struct of another kind is skipped by its size.
		// A fixed String tag on a Str property is read as bytes (an older layout of the same field).
		bool ReadProperties(Reader& r, UActor* actor)
		{
			std::string className = actor->Class->Name.ToString();
			while (true)
			{
				Tag tag = ReadTag(r);
				if (r.error)
					return false;
				if (tag.name.empty())
					return true;

				UProperty* prop = nullptr;
				NameString name(tag.name);
				for (UProperty* p : actor->PropertyData.Class->Properties)
				{
					if (p->Name == name)
					{
						prop = p;
						break;
					}
				}
				if (!prop)
				{
					LogMessage("Property " + tag.name + " of " + className + " not found");
				}
				else if (tag.type == TagString && TagType(prop) == TagStr)
				{
					int max = tag.size == 0 ? 254 : tag.size;
					std::string text;
					for (int i = 0; i < max; i++)
					{
						uint8_t c = r.UInt8();
						if (c == 0 || r.error)
							break;
						text.push_back((char)c);
					}
					if (tag.arrayIndex >= 0 && tag.arrayIndex < prop->ArrayDimension)
						*(std::string*)((uint8_t*)actor->GetProperty(prop) + tag.arrayIndex * prop->ElementPitch()) = text;
					continue;
				}
				else if (tag.type != TagType(prop))
				{
					LogMessage("Type mismatch in " + tag.name + " of " + className);
				}
				else if (tag.arrayIndex < 0 || tag.arrayIndex >= prop->ArrayDimension)
				{
					LogMessage("Array bounds in " + tag.name + " of " + className);
				}
				else if (tag.type == TagStruct && static_cast<UStructProperty*>(prop)->Struct->Name != NameString(tag.structName))
				{
					LogMessage("Property " + tag.name + " of " + className + " struct type mismatch");
				}
				else if (!Skipped(prop))
				{
					void* data = (uint8_t*)actor->GetProperty(prop) + tag.arrayIndex * prop->ElementPitch();
					if (tag.type == TagBool)
						static_cast<UBoolProperty*>(prop)->SetBool(data, tag.bit7);
					else
						ReadValue(r, prop, data);
					if (r.error)
						return false;
					continue;
				}
				else if (!StrTools::equals_ignore_case(tag.name, "XLevel"))
				{
					LogMessage("Property " + tag.name + " of " + className + " is native or transient");
				}
				if (tag.type != TagBool)
					r.Skip(tag.size);
			}
		}

		// The file names the class without its package; the original finds it in any loaded package. Here: the
		// game's script packages, [Editor.EditorEngine] EditPackages.
		UClass* FindClassAnyPackage(const std::string& className)
		{
			for (int i = 0; i < 64; i++)
			{
				std::string package = engine->packages->GetIniValue("system", "Editor.EditorEngine", "EditPackages", "", i);
				if (package.empty())
					break;
				if (UClass* cls = engine->packages->FindClass(NameString(package + "." + className)))
					return cls;
			}
			return nullptr;
		}

		// Where a saved actor the level doesn't have stands, to spawn it there: the record's Location, walking the
		// tags with the class's properties (a tag's size can't skip a name or an object: see Writer)
		std::optional<vec3> SavedLocation(Reader r, UClass* cls)
		{
			while (true)
			{
				Tag tag = ReadTag(r);
				if (r.error || tag.name.empty())
					return std::nullopt;
				UProperty* prop = nullptr;
				NameString name(tag.name);
				for (UProperty* p : cls->Properties)
				{
					if (p->Name == name)
					{
						prop = p;
						break;
					}
				}
				if (tag.type == TagBool)
					continue;
				if (!prop || tag.type != TagType(prop) || Skipped(prop) || tag.type == TagString)
				{
					r.Skip(tag.size);
					continue;
				}
				if (prop->Name == "Location" && tag.type == TagStruct && tag.size == 12)
				{
					vec3 location;
					r.Bytes(&location.x, 4);
					r.Bytes(&location.y, 4);
					r.Bytes(&location.z, 4);
					return r.error ? std::nullopt : std::optional<vec3>(location);
				}
				std::vector<uint64_t> scratch((prop->ElementSize() + 7) / 8 + 1);
				prop->ConstructElement(scratch.data());
				ReadValue(r, prop, scratch.data());
				prop->DestructElement(scratch.data());
			}
		}

		bool IsPersistent(UActor* actor)
		{
			return actor && !actor->bDeleteMe() && KW::BoolProperty(actor, "bPersistent");
		}

		// The map's name after its last backslash, with ".unr" when it has none
		std::string MapFileName(std::string map)
		{
			size_t slash = map.find_last_of("\\/");
			if (slash != std::string::npos)
				map = map.substr(slash + 1);
			std::string ext = engine->packages->GetMapExtension();
			if (Lower(map).find(Lower(ext)) == std::string::npos)
				map += "." + ext;
			return map;
		}

		fs::path CacheFolder()
		{
			return engine->packages->GetSaveFolderPath() / "cache";
		}

		// <save folder>\cache\<map without its extension>_pa.usa
		fs::path PersistentFile(const std::string& mapFile)
		{
			std::string ext = engine->packages->GetMapExtension();
			size_t at = Lower(mapFile).find(Lower(ext));
			std::string base = mapFile.substr(0, at == std::string::npos || at == 0 ? mapFile.size() : at - 1);
			return CacheFolder() / (base + "_pa." + engine->packages->GetSaveExtension());
		}

		// GFileManager's FileSize and GetGlobalTime (the _stat modification time, in seconds since 1970) of the map file
		void MapFileInfo(int32_t& size, int64_t& time)
		{
			size = -1;
			time = 0;
			if (!engine->LevelPackage)
				return;
			fs::path path = engine->LevelPackage->GetPackageFilePath();
			std::error_code ec;
			auto fileSize = fs::file_size(path, ec);
			if (ec)
				return;
			size = (int32_t)fileSize;
			auto fileTime = fs::last_write_time(path, ec);
			if (ec)
				return;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
			auto sys = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
#else
			// libc++'s file clock counts from 1970 as the system clock does
			auto sys = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(fileTime.time_since_epoch()));
#endif
			time = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
		}

		std::vector<fs::path> PersistentFiles(const fs::path& folder)
		{
			std::vector<fs::path> files;
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(folder, ec))
			{
				std::string name = Lower(entry.path().filename().string());
				if (entry.is_regular_file(ec) && name.find("_pa.") != std::string::npos)
					files.push_back(entry.path());
			}
			return files;
		}
	}

	// IDA Engine.dll: ?SavePersistentActors@ULevel@@QAE_NABVFString@@@Z [HP2 0x103B9030]
	// IDA Engine.dll: ?SerializePersistence@AActor@@QAEXAAVFArchive@@@Z [HP2 0x103811D0]
	// "PA0" (8 bytes, as UTF-16 with its terminator), the map's file name, size and time, the actor count, then for
	// each actor its name, its class name, the byte size of its properties and the properties; then "EOF". Nothing is
	// written when the level has no persistent actor (an older file stays).
	void SavePersistentActors(const std::string& map)
	{
		if (!engine->Level)
			return;
		std::vector<UActor*> actors;
		for (UActor* actor : engine->Level->Actors)
		{
			if (IsPersistent(actor))
				actors.push_back(actor);
		}
		if (actors.empty())
			return;

		std::string mapFile = MapFileName(map);
		fs::path path = PersistentFile(mapFile);
		int32_t mapSize;
		int64_t mapTime;
		MapFileInfo(mapSize, mapTime);

		LogMessage("*** Starting to Save Persistent Actors for map " + mapFile);
		Writer w;
		const char16_t magic[4] = { u'P', u'A', u'0', 0 };
		w.Bytes(magic, 8);
		w.Text(mapFile);
		w.Int32(mapSize);
		w.Int64(mapTime);
		w.Int32((int32_t)actors.size());
		for (UActor* actor : actors)
		{
			w.Text(actor->Name.ToString());
			w.Text(actor->Class->Name.ToString());
			size_t sizeAt = w.buffer.size();
			w.Int32(0);
			WriteProperties(w, actor);
			int32_t size = (int32_t)(w.buffer.size() - sizeAt - 4);
			memcpy(w.buffer.data() + sizeAt, &size, 4);
		}
		w.Text("EOF");

		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file || !file.write((const char*)w.buffer.data(), w.buffer.size()))
		{
			LogMessage("*** Could not Save Persistent Actor file " + path.string() + ", CreateFileWriter had an error!!!!.");
			return;
		}
		LogMessage("*** Finished Saving Persistent Actors for map " + mapFile);
	}

	// IDA Engine.dll: ?LoadPersistentActors@ULevel@@QAE_NABVFString@@@Z [HP2 0x103B9E40]
	// UGameEngine::LoadMap calls it for every map but Entry right after the level is in, before the GameInfo and
	// BeginPlay. A file for another map, or for a map file of another size or time, is left alone. The level's actor
	// of each saved name takes the saved properties; a saved actor the level doesn't have is spawned by its class
	// and name. After a good EOF the level's persistent actors the file didn't name are destroyed.
	void LoadPersistentActors(const std::string& map)
	{
		if (!engine->Level)
			return;
		std::string mapFile = MapFileName(map);
		fs::path path = PersistentFile(mapFile);

		std::vector<uint8_t> data;
		{
			std::ifstream file(path, std::ios::binary);
			if (file)
				data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}
		if (data.empty())
		{
			LogMessage("*** Could not load Persistent Actor file " + path.string() + " (file is probably missing).");
			return;
		}
		LogMessage("*** Starting to Load Persistent Actors for map " + mapFile);

		std::map<NameString, UActor*> levelActors;
		for (UActor* actor : engine->Level->Actors)
		{
			if (IsPersistent(actor))
				levelActors[actor->Name] = actor;
		}
		if (levelActors.empty())
			return;

		Reader r(data);
		uint8_t magic[8];
		r.Bytes(magic, 8);
		std::string savedMap = r.Text();
		if (r.error)
		{
			LogMessage("ULevelBase::LoadPersistentActors ERROR: Loading the map name!");
			return;
		}
		if (!StrTools::equals_ignore_case(savedMap, mapFile))
		{
			LogMessage("ULevelBase::LoadPersistentActors ERROR: FinalMapName != Saved map name!");
			return;
		}
		int32_t mapSize;
		int64_t mapTime;
		MapFileInfo(mapSize, mapTime);
		if (r.Int32() != mapSize)
		{
			LogMessage("ULevelBase::LoadPersistentActors ERROR: Loading map FileSize that this PA file is for!");
			return;
		}
		if (r.Int64() != mapTime)
		{
			LogMessage("ULevelBase::LoadPersistentActors ERROR: Saving map FileTime that this PA file is for!");
			return;
		}
		int32_t count = r.Int32();
		if (r.error)
			return;

		bool ok = true;
		for (int32_t i = 0; i < count && ok; i++)
		{
			std::string name = r.Text();
			std::string className = r.Text();
			int32_t size = r.Int32();
			if (r.error)
			{
				LogMessage("ULevelBase::LoadPersistentActors ERROR: Loading an actor's header!");
				ok = false;
				break;
			}
			size_t start = r.Tell();

			UActor* actor = nullptr;
			auto it = levelActors.find(NameString(name));
			if (it != levelActors.end())
			{
				actor = it->second;
				levelActors.erase(it);
			}
			else
			{
				LogMessage("*** Actor is NOT in level " + mapFile + " but is in pa file " + path.string() + "! ");
				UClass* cls = FindClassAnyPackage(className);
				if (!cls)
				{
					r.Seek(start + size);
					continue;
				}
				std::optional<vec3> location = SavedLocation(r, cls);
				actor = engine->LevelInfo->Spawn(cls, nullptr, NameString(), location ? *location : engine->LevelInfo->Location(), Rotator(0, 0, 0));
				if (!actor)
				{
					LogMessage("ULevelBase::LoadPersistentActors ERROR: Spawning the the actor!");
					ok = false;
					break;
				}
				actor->Name = NameString(name);
				actor->SetName("Name", actor->Name);
			}

			// The saved Location replaces the one the actor is filed under for collision
			engine->Level->Collision.RemoveFromCollision(actor);
			ok = ReadProperties(r, actor);
			engine->Level->Collision.AddToCollision(actor);
			if (!ok)
				LogMessage("ULevelBase::LoadPersistentActors ERROR: Loading the actor's properties!");
		}
		if (!ok)
			return;

		std::string eof = r.Text();
		if (r.error)
		{
			LogMessage("ULevelBase::LoadPersistentActors ERROR: Loading the EOF token!");
			return;
		}
		if (eof != "EOF")
		{
			LogMessage("The EOF token is corrupt in the persistent actor file!!!!!");
			return;
		}
		for (auto& [name, actor] : levelActors)
			actor->Destroy();
		LogMessage("*** Finished Loading Persistent Actors for map " + mapFile);
	}

	// IDA Core.dll: ?SavePersistentActorCache@@YAXXZ [HP2 Core 0x10157790]
	// After a save: the slot folder's old *_pa.* files are deleted and the cache folder's copied in their place.
	void SavePersistentActorCache()
	{
		fs::path folder = engine->packages->GetSaveFolderPath();
		std::error_code ec;
		for (const fs::path& file : PersistentFiles(folder))
			fs::remove(file, ec);
		for (const fs::path& file : PersistentFiles(CacheFolder()))
			fs::copy_file(file, folder / file.filename(), fs::copy_options::overwrite_existing, ec);
	}

	// IDA Core.dll: ?LoadPersistentActorCache@@YAXXZ [HP2 Core 0x10157E20]
	// IDA Core.dll: not exported: sub_10157B90 [HP2 Core 0x10157B90] (emptying the cache folder; LoadPersistentActorCache's first call)
	// When the game starts (appInit) and on LOADGAME: the cache folder's *_pa.* files are deleted and the slot
	// folder's copied in, so the hubs come back as they were at the save.
	void LoadPersistentActorCache()
	{
		fs::path folder = engine->packages->GetSaveFolderPath();
		std::error_code ec;
		for (const fs::path& file : PersistentFiles(CacheFolder()))
			fs::remove(file, ec);
		fs::create_directories(CacheFolder(), ec);
		for (const fs::path& file : PersistentFiles(folder))
			fs::copy_file(file, CacheFolder() / file.filename(), fs::copy_options::overwrite_existing, ec);
	}
}
