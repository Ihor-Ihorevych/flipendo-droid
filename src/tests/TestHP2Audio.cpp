#include "Precomp.h"
#include "Test.h"
#include "HP2.h"
#include <cstring>

// HP2's EA-XA decoder (HP2Sound.cpp) and ALAudio's float volumes (HP2Music.cpp).

namespace
{
	// One 15-byte EA-XA block: header (predictor << 4 | shift - 8), then 28 nibbles, high nibble first
	void AddBlock(Array<uint8_t>& data, uint8_t header, std::vector<uint8_t> bytes)
	{
		bytes.resize(14, 0);
		data.push_back(header);
		for (uint8_t b : bytes)
			data.push_back(b);
	}

	uint32_t Get32(const Array<uint8_t>& wav, size_t at) { uint32_t v; memcpy(&v, wav.data() + at, 4); return v; }
	uint16_t Get16(const Array<uint8_t>& wav, size_t at) { uint16_t v; memcpy(&v, wav.data() + at, 2); return v; }
	int16_t Sample(const Array<uint8_t>& wav, size_t i) { int16_t v; memcpy(&v, wav.data() + 44 + i * 2, 2); return v; }
	size_t SampleCount(const Array<uint8_t>& wav) { return (wav.size() - 44) / 2; }
}

TEST(DecodeXA_WavHeader)
{
	Array<uint8_t> data;
	AddBlock(data, 0x0C, {});
	Array<uint8_t> wav = HP2::DecodeXA(data, 28, 1, 22050);
	CHECK_EQ(wav.size(), (size_t)(44 + 56));
	CHECK(memcmp(wav.data(), "RIFF", 4) == 0);
	CHECK_EQ(Get32(wav, 4), 36u + 56u);
	CHECK(memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0);
	CHECK_EQ(Get16(wav, 20), 1);     // PCM
	CHECK_EQ(Get16(wav, 22), 1);     // mono
	CHECK_EQ(Get32(wav, 24), 22050u);
	CHECK_EQ(Get32(wav, 28), 44100u);
	CHECK_EQ(Get16(wav, 32), 2);
	CHECK_EQ(Get16(wav, 34), 16);
	CHECK(memcmp(wav.data() + 36, "data", 4) == 0);
	CHECK_EQ(Get32(wav, 40), 56u);
}

TEST(DecodeXA_NibblesAndShift)
{
	// Predictor 0, shift 12 (+8): nibble 1 is 1, nibble 0xF is -1
	Array<uint8_t> data;
	AddBlock(data, 0x0C, { 0x1F });
	Array<uint8_t> wav = HP2::DecodeXA(data, 0, 1, 22050);
	CHECK_EQ(SampleCount(wav), (size_t)28);
	CHECK_EQ(Sample(wav, 0), 1);
	CHECK_EQ(Sample(wav, 1), -1);
	CHECK_EQ(Sample(wav, 2), 0);
}

TEST(DecodeXA_Predictors)
{
	// Predictor 2 (460, -208) uses both earlier samples: 1, (460 * 1) >> 8 = 1, (460 - 208) >> 8 = 0
	Array<uint8_t> data;
	AddBlock(data, 0x2C, { 0x10 });
	Array<uint8_t> wav = HP2::DecodeXA(data, 0, 1, 22050);
	CHECK_EQ(Sample(wav, 0), 1);
	CHECK_EQ(Sample(wav, 1), 1);
	CHECK_EQ(Sample(wav, 2), 0);
}

TEST(DecodeXA_Clamps)
{
	// Predictor 1 (240), shift 0 (+8): 7 << 20 >> 8 = 28672, then 240 * 28672 / 256 + 28672 overflows
	Array<uint8_t> data;
	AddBlock(data, 0x10, { 0x77 });
	Array<uint8_t> wav = HP2::DecodeXA(data, 0, 1, 22050);
	CHECK_EQ(Sample(wav, 0), 28672);
	CHECK_EQ(Sample(wav, 1), 32767);
	CHECK_EQ(Sample(wav, 2), (240 * 32767) >> 8);

	// Nibble 8 is the most negative: -32768, then 240 * -32768 / 256 - 32768
	data.clear();
	AddBlock(data, 0x10, { 0x88 });
	wav = HP2::DecodeXA(data, 0, 1, 22050);
	CHECK_EQ(Sample(wav, 0), -32768);
	CHECK_EQ(Sample(wav, 1), -32768);
}

TEST(DecodeXA_Lengths)
{
	Array<uint8_t> data;
	AddBlock(data, 0x0C, {});
	AddBlock(data, 0x0C, {});
	CHECK_EQ(SampleCount(HP2::DecodeXA(data, 0, 1, 22050)), (size_t)56);
	CHECK_EQ(SampleCount(HP2::DecodeXA(data, 30, 1, 22050)), (size_t)30);
	CHECK_EQ(SampleCount(HP2::DecodeXA(data, 100, 1, 22050)), (size_t)56);

	// A partial block at the end is left out
	data.resize(data.size() + 5, (uint8_t)0);
	CHECK_EQ(SampleCount(HP2::DecodeXA(data, 0, 1, 22050)), (size_t)56);

	// Anything but mono is played as interleaved stereo: whole sample pairs only
	Array<uint8_t> stereo = HP2::DecodeXA(data, 29, 2, 22050);
	CHECK_EQ(SampleCount(stereo), (size_t)28);
	CHECK_EQ(Get16(stereo, 22), 2);
	CHECK_EQ(Get16(stereo, 32), 4);
}

TEST(VolumeFromIni_Parses)
{
	CHECK_EQ(HP2::VolumeFromIni("0.53", 7), 135);
	CHECK_EQ(HP2::VolumeFromIni("1", 7), 255);
	CHECK_EQ(HP2::VolumeFromIni("0", 7), 0);
	CHECK_EQ(HP2::VolumeFromIni("1.5", 7), 255);
	CHECK_EQ(HP2::VolumeFromIni("-2", 7), 0);
	CHECK_EQ(HP2::VolumeFromIni("loud", 7), 7);
	CHECK_EQ(HP2::VolumeFromIni("", 7), 7);
}

TEST(VolumeToIni_RoundTrips)
{
	for (int v = 0; v < 256; v++)
		CHECK_EQ(HP2::VolumeFromIni(HP2::VolumeToIni((uint8_t)v), 0), v);
}
