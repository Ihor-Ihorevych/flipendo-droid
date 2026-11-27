#include "Precomp.h"
#include "Test.h"
#include "KW.h"

// HP1's song level under Galaxy's mixer (KWSound.cpp): the sample volume times the centre pan of a stereo sample.

TEST(MusicMixGain_FullSoundVolume)
{
	CHECK_NEAR(KW::MusicMixGain(255), 0.7071f, 0.0001f);
}

TEST(MusicMixGain_DiscDefault)
{
	// HP1's Default.ini: SoundVolume=200
	CHECK_NEAR(KW::MusicMixGain(200), 200.0f / 255.0f * 0.7071f, 0.0001f);
}

TEST(MusicMixGain_Muted)
{
	CHECK_EQ(KW::MusicMixGain(0), 0.0f);
}
