#include "Precomp.h"
#include "HP2.h"
#include <algorithm>
#include <cstring>

// HP2's "XA" sounds (USound.Format == 'XA', most of HP2's sound effects, ambience and dialogue) are EA-XA ADPCM, EA's own
// codec (HP2 ships EA's sx.exe tool with the game). Engine.dll only encodes it (USound::CompressXA, editor side); the game
// streams the sound with FFileStream (Core.dll), started from UALAudioSubsystem::RegisterSound (ALAudio.dll), which
// decodes it with a mono EA-XA decoder: 15-byte blocks of 28 samples, the header byte holding the predictor in its high
// nibble and the shift in its low nibble, then two samples per byte, high nibble first. The sound's HP2Values give the
// sample count, bits, channels and rate (USound::Load).

namespace HP2
{
	// IDA ALAudio.dll: ?RegisterSound@UALAudioSubsystem@@UAEHPAVUSound@@@Z [HP2 ALAudio 0x10003400]
	// IDA Core.dll: ?Read@FFileStream@@QAEHHH@Z [HP2 Core 0x10178390]
	// IDA Core.dll: not exported: sub_1017AD30 [HP2 Core 0x1017AD30] (the XA stream's read, called from FFileStream::Read for stream types 3 and 4)
	// IDA Core.dll: not exported: sub_1017AB80 [HP2 Core 0x1017AB80] (the block decoder, sub_1017AD30's only callee; coefficient table at 0x101D44D8)
	// The decoder has one sample history, so the data is always one stream of samples; RegisterSound gives OpenAL a
	// stereo format when the sound isn't mono, which plays that stream as interleaved stereo (no HP2 sound does this,
	// all 3114 XA sounds are mono).
	Array<uint8_t> DecodeXA(const Array<uint8_t>& data, int sampleCount, int channels, int rate)
	{
		// Only predictors 0-3 occur in HP2's sounds; the original's table runs on into unrelated data after them
		static const int coefs[4][2] = { { 0, 0 }, { 240, 0 }, { 460, -208 }, { 392, -220 } };

		std::vector<int16_t> pcm;
		pcm.reserve(sampleCount > 0 ? sampleCount : data.size() * 2);
		int hist1 = 0, hist2 = 0;
		for (size_t pos = 0; pos + 15 <= data.size(); pos += 15)
		{
			uint8_t header = data[pos];
			int coef1 = coefs[(header >> 4) & 3][0];
			int coef2 = coefs[(header >> 4) & 3][1];
			int shift = (header & 0x0F) + 8;
			for (int i = 0; i < 28; i++)
			{
				uint8_t byte = data[pos + 1 + i / 2];
				uint32_t nibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);
				// One rounding for the prediction and the nibble together, as the original does
				int sample = (coef1 * hist1 + coef2 * hist2 + ((int32_t)(nibble << 28) >> shift)) >> 8;
				sample = std::clamp(sample, -32768, 32767);
				pcm.push_back((int16_t)sample);
				hist2 = hist1;
				hist1 = sample;
			}
		}
		if (sampleCount > 0 && (size_t)sampleCount < pcm.size())
			pcm.resize(sampleCount);
		// AL_FORMAT_MONO16 for one channel, AL_FORMAT_STEREO16 for anything else
		channels = channels == 1 ? 1 : 2;
		pcm.resize(pcm.size() / channels * channels);

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
