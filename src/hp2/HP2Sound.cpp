#include "Precomp.h"
#include "HP2.h"
#include <algorithm>
#include <cstring>

// HP2 stores most sounds in a format of its own, "XA" (docs/re/hp2/engine.md). The export holds, after the (empty) property
// list and the format name:
//   int32  0x104 (a version or flags value, always the same in the sounds read)
//   float  duration in seconds
//   int32  sample count, int32 bits per sample (16), int32 channels, int32 frequency
//   int32  absolute file offset of the end of the data (the lazy array skip offset)
//   index  data size, then the data
// The data is 4-bit ADPCM in frames of 15 bytes: one byte (low nibble the shift, high nibble the filter) and 14 bytes of 28
// samples, low nibble first, so every sound's data size is a multiple of 15 and frames * 28 is its sample count rounded up.
// That is the CD-XA / SPU ADPCM frame. NOT yet verified against HP2's ALAudio.dll decoder (no IDA database on this
// machine): written from the frame arithmetic of the sounds in HPSounds.u and the well-known XA filter set.

namespace HP2
{
	static void Put16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(v & 0xff); out.push_back(v >> 8); }
	static void Put32(std::vector<uint8_t>& out, uint32_t v) { Put16(out, v & 0xffff); Put16(out, v >> 16); }

	// Decodes XA data (as described above) to a 16-bit PCM WAV file image SurrealEngine's wav reader takes.
	// IDA ALAudio.dll: not exported: the XA sound decoder, not located (find it from the "XA" format string in ALAudio.dll) [HP2]
	std::vector<uint8_t> DecodeXASound(const uint8_t* data, size_t size, int sampleCount, int channels, int frequency)
	{
		static const int filter0[5] = { 0, 60, 115, 98, 122 };
		static const int filter1[5] = { 0, 0, -52, -55, -60 };

		std::vector<int16_t> pcm;
		pcm.reserve((size / 15) * 28);
		int s1 = 0, s2 = 0;
		for (size_t frame = 0; frame + 15 <= size; frame += 15)
		{
			int header = data[frame];
			int shift = header & 0x0f;
			int filter = std::min((header >> 4) & 0x0f, 4);
			if (shift > 12)
				shift = 9;
			for (int i = 0; i < 28; i++)
			{
				int byte = data[frame + 1 + i / 2];
				int nibble = (i & 1) ? (byte >> 4) : (byte & 0x0f);
				int sample = (int16_t)(nibble << 12) >> shift;
				sample += (s1 * filter0[filter] + s2 * filter1[filter] + 32) >> 6;
				sample = std::clamp(sample, -32768, 32767);
				s2 = s1;
				s1 = sample;
				pcm.push_back((int16_t)sample);
			}
		}
		if (sampleCount > 0 && (size_t)sampleCount < pcm.size())
			pcm.resize(sampleCount);

		if (channels < 1)
			channels = 1;
		std::vector<uint8_t> wav;
		wav.reserve(44 + pcm.size() * 2);
		const uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
		wav.insert(wav.end(), { 'R', 'I', 'F', 'F' });
		Put32(wav, 36 + dataBytes);
		wav.insert(wav.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
		Put32(wav, 16);
		Put16(wav, 1);
		Put16(wav, (uint16_t)channels);
		Put32(wav, (uint32_t)frequency);
		Put32(wav, (uint32_t)(frequency * channels * 2));
		Put16(wav, (uint16_t)(channels * 2));
		Put16(wav, 16);
		wav.insert(wav.end(), { 'd', 'a', 't', 'a' });
		Put32(wav, dataBytes);
		const size_t at = wav.size();
		wav.resize(at + dataBytes);
		if (dataBytes)
			memcpy(wav.data() + at, pcm.data(), dataBytes);
		return wav;
	}
}
