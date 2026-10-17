#include "Precomp.h"
#include "HP2.h"
#include "Utils/Logger.h"
#include <algorithm>
#include <cstring>

// HP2's "XA" sounds (USound.Format == 'XA', most of HP2's sound effects, ambience and dialogue) are EA-XA ADPCM, EA's own
// codec (HP2 ships EA's sx.exe tool with the game). Engine.dll only encodes it (USound::CompressXA, editor side); the game
// streams the sound with FFileStream (Core.dll), started from UALAudioSubsystem::RegisterSound (ALAudio.dll). The
// layout is the common EA-XA one, mono: 15-byte blocks of 28 samples. The header byte holds the predictor in its high
// nibble and the shift in its low nibble, 0xEE marks an uncompressed block (two 16-bit history samples, then 28 16-bit
// samples). The data bytes hold two samples each, high nibble first. The sound's HP2Values give the sample count, bits,
// channels and rate (USound::Load).

namespace HP2
{
	// IDA ALAudio.dll: ?RegisterSound@UALAudioSubsystem@@UAEHPAVUSound@@@Z [HP2 ALAudio 0x10003400] NOT yet verified: the decoder itself (FFileStream, Core.dll) was not found; the format is EA-XA as decoded by vgmstream
	// Only mono is decoded (every XA sound checked in HPSounds.u is mono); other channel counts give silence.
	Array<uint8_t> DecodeXA(const Array<uint8_t>& data, int sampleCount, int channels, int rate)
	{
		if (channels != 1)
		{
			LogMessage("XA sound with " + std::to_string(channels) + " channels isn't supported");
			return DecodeXA(Array<uint8_t>(), 0, 1, rate);
		}
		static const int coefs[4][2] = { { 0, 0 }, { 240, 0 }, { 460, -208 }, { 392, -220 } };

		std::vector<int16_t> pcm;
		pcm.reserve(sampleCount > 0 ? sampleCount : data.size() * 2);
		int hist1 = 0, hist2 = 0;
		size_t pos = 0;
		while (pos < data.size())
		{
			uint8_t header = data[pos];
			if (header == 0xEE)
			{
				if (pos + 61 > data.size())
					break;
				const uint8_t* p = data.data() + pos + 1;
				hist1 = (int16_t)(p[0] << 8 | p[1]);
				hist2 = (int16_t)(p[2] << 8 | p[3]);
				for (int i = 0; i < 28; i++)
					pcm.push_back((int16_t)(p[4 + i * 2] << 8 | p[5 + i * 2]));
				pos += 61;
				continue;
			}
			if (pos + 15 > data.size())
				break;
			int coef1 = coefs[(header >> 4) & 3][0];
			int coef2 = coefs[(header >> 4) & 3][1];
			int shift = header & 0x0F;
			for (int i = 0; i < 28; i++)
			{
				uint8_t byte = data[pos + 1 + i / 2];
				int nibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);
				int sample = (int)(int16_t)(nibble << 12) >> shift;
				sample += (coef1 * hist1 + coef2 * hist2 + 128) >> 8;
				sample = std::clamp(sample, -32768, 32767);
				pcm.push_back((int16_t)sample);
				hist2 = hist1;
				hist1 = sample;
			}
			pos += 15;
		}
		if (sampleCount > 0 && (size_t)sampleCount < pcm.size())
			pcm.resize(sampleCount);

		// As a WAV file, which SurrealEngine's audio code already reads
		uint32_t dataSize = (uint32_t)(pcm.size() * 2);
		uint16_t blockAlign = (uint16_t)(channels * 2);
		uint32_t byteRate = rate * blockAlign;
		Array<uint8_t> wav(44 + dataSize);
		uint8_t* w = wav.data();
		auto put32 = [&](size_t at, uint32_t v) { memcpy(w + at, &v, 4); };
		auto put16 = [&](size_t at, uint16_t v) { memcpy(w + at, &v, 2); };
		memcpy(w, "RIFF", 4); put32(4, 36 + dataSize); memcpy(w + 8, "WAVEfmt ", 8);
		put32(16, 16); put16(20, 1); put16(22, (uint16_t)channels); put32(24, rate); put32(28, byteRate);
		put16(32, blockAlign); put16(34, 16); memcpy(w + 36, "data", 4); put32(40, dataSize);
		memcpy(w + 44, pcm.data(), dataSize);
		return wav;
	}
}
