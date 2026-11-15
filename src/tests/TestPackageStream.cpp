#include "Precomp.h"
#include "Test.h"
#include "Package/PackageStream.h"
#include "Utils/File.h"
#include <climits>
#include <cstring>
#include <vector>

namespace
{
	class MemoryFile : public File
	{
	public:
		std::vector<uint8_t> bytes;
		size_t pos = 0;

		int64_t size() override { return (int64_t)bytes.size(); }
		void read(void* data, size_t size) override
		{
			if (pos + size > bytes.size())
				throw std::runtime_error("read past the end");
			memcpy(data, bytes.data() + pos, size);
			pos += size;
		}
		void write(const void* data, size_t size) override
		{
			if (bytes.size() > 64)
				throw std::runtime_error("a compact index is at most 5 bytes");
			if (pos + size > bytes.size())
				bytes.resize(pos + size);
			memcpy(bytes.data() + pos, data, size);
			pos += size;
		}
		void seek(int64_t offset, SeekPoint origin) override { pos = (size_t)offset; }
		uint64_t tell() override { return pos; }
	};

	int32_t RoundTrip(int32_t v, size_t* written = nullptr)
	{
		auto file = std::make_shared<MemoryFile>();
		PackageStreamWriter writer(nullptr, file);
		writer.WriteIndex(v);
		if (written)
			*written = file->bytes.size();
		file->pos = 0;
		PackageStream reader(nullptr, file);
		return reader.ReadIndex();
	}
}

// A compact index of INT_MIN (HP2's Adv12Chamber BSP has one) used to make WriteIndex loop forever while saving.
TEST(PackageStream_CompactIndexRoundTrips)
{
	for (int32_t v : { 0, 1, -1, 63, -63, 64, -64, 8191, 8192, -100000, 123456789, INT_MAX, -INT_MAX, INT_MIN })
	{
		size_t written = 0;
		CHECK_EQ(RoundTrip(v, &written), v);
		CHECK(written >= 1 && written <= 5);
	}
}
