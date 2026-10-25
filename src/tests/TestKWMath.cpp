#include "Precomp.h"
#include "Test.h"
#include "KW.h"
#include "KWMeshLight.h"
#include "KWMove.h"
#include "Math/coords.h"
#include "Packages/Engine/Resources/Level/UModel.h"

// KW's pure math: the view and canvas scaling, centred canvas text, FGetHSV, FCoords::OrthoRotation, AActor::TwoWallAdjust and the mesh
// opacity flags.

static int AngleDelta(int a, int b)
{
	return (int16_t)(uint16_t)(a - b);
}

TEST(ViewFovAngle_KeepsFourThreeAndNarrower)
{
	CHECK_NEAR(KW::ViewFovAngle(90.0f, 1024, 768), 90.0, 1e-4);
	CHECK_NEAR(KW::ViewFovAngle(90.0f, 1280, 1024), 90.0, 1e-4);
	CHECK_NEAR(KW::ViewFovAngle(90.0f, 0, 0), 90.0, 1e-4);
	CHECK_NEAR(KW::ViewFovAngle(180.0f, 1920, 1080), 180.0, 1e-4);
}

TEST(ViewFovAngle_WidensHorizontally)
{
	// Hor+: the 4:3 vertical FOV is kept, so tan(h/2) grows with the aspect ratio. 16:9: 2 * atan(4/3) = 106.26.
	CHECK_NEAR(KW::ViewFovAngle(90.0f, 1920, 1080), 106.2602, 1e-3);
	CHECK_NEAR(KW::ViewFovAngle(90.0f, 2560, 1080), 2.0 * std::atan(1.77778) * 180.0 / 3.14159265, 1e-2);
}

TEST(CanvasUIScale_FromViewportHeight)
{
	CHECK_NEAR(KW::CanvasUIScale(768), 1.0, 1e-6);
	CHECK_NEAR(KW::CanvasUIScale(480), 1.0, 1e-6);
	CHECK_NEAR(KW::CanvasUIScale(1080), 1.40625, 1e-6);
	CHECK_NEAR(KW::CanvasUIScale(1536), 2.0, 1e-6);
}

TEST(CenteredTextLineX_OnCurX)
{
	// HP1's loading screen at 1024x768: OrgX 153.6, ClipX 716.8, CurX 358.4; a 301-wide line starts 150 left of CurX
	CHECK_NEAR(KW::CenteredTextLineX(358.4f, 301.0f), 208.0, 1e-6);
	CHECK_NEAR(KW::CenteredTextLineX(358.4f, 0.0f), 358.0, 1e-6);
	CHECK_NEAR(KW::CenteredTextLineX(100.0f, 300.5f), -50.0, 1e-6); // the start may be left of OrgX
}

TEST(GetHSV_Hues)
{
	// Brightness 255 is 1.4 on HP1's curve: 0.7 * 1.4 / (sqrt(1.4) + 0.01)
	const double full = 0.7 * 1.4 / (std::sqrt(1.4) + 0.01);
	vec3 red = KW::GetHSV(0, 0, 255);
	CHECK_NEAR(red.x, full, 1e-4);
	CHECK_NEAR(red.y, 0.0, 1e-6);
	CHECK_NEAR(red.z, 0.0, 1e-6);
	vec3 green = KW::GetHSV(85, 0, 255);
	CHECK_NEAR(green.x, 0.0, 1e-6);
	CHECK_NEAR(green.y, full, 1e-4);
	vec3 blue = KW::GetHSV(170, 0, 255);
	CHECK_NEAR(blue.y, 0.0, 1e-6);
	CHECK_NEAR(blue.z, full, 1e-4);
}

TEST(GetHSV_SaturationAndBrightness)
{
	// Saturation 255 is white in UE1's colour picker; brightness 0 is black.
	vec3 white = KW::GetHSV(40, 255, 255);
	CHECK_NEAR(white.x, white.y, 1e-6);
	CHECK_NEAR(white.y, white.z, 1e-6);
	vec3 black = KW::GetHSV(40, 128, 0);
	CHECK_NEAR(black.x + black.y + black.z, 0.0, 1e-6);
	float last = 0.0f;
	for (int b = 1; b < 256; b++)
	{
		float r = KW::GetHSV(0, 0, (uint8_t)b).x;
		CHECK(r > last);
		CHECK(r <= 1.0f);
		last = r;
	}
}

TEST(OrthoRotation_InvertsCoordsRotation)
{
	const Rotator rotations[] = {
		Rotator(0, 0, 0), Rotator(0, 16384, 0), Rotator(0, -20000, 0), Rotator(4000, 0, 0), Rotator(-9000, 30000, 0),
		Rotator(0, 0, 8000), Rotator(3000, 12000, -6000), Rotator(-12000, -5000, 20000),
	};
	for (const Rotator& r : rotations)
	{
		Coords c = Coords::Rotation(r);
		Rotator back = KW::OrthoRotation(c.XAxis, c.YAxis, c.ZAxis);
		CHECK(std::abs(AngleDelta(back.Pitch, r.Pitch)) <= 2);
		CHECK(std::abs(AngleDelta(back.Yaw, r.Yaw)) <= 2);
		CHECK(std::abs(AngleDelta(back.Roll, r.Roll)) <= 2);
	}
}

TEST(TwoWallAdjust_SameSide)
{
	// Walls facing the same way: slide along the new wall for the rest of the move, unless that turns back.
	vec3 delta(-10.0f, 5.0f, 0.0f);
	KW::TwoWallAdjust(vec3(0.0f, 1.0f, 0.0f), delta, vec3(1.0f, 0.0f, 0.0f), vec3(0.7071f, 0.7071f, 0.0f), 0.5f);
	CHECK_NEAR(delta.x, 0.0, 1e-5);
	CHECK_NEAR(delta.y, 2.5, 1e-5);
	CHECK_NEAR(delta.z, 0.0, 1e-5);

	delta = vec3(-10.0f, 5.0f, 0.0f);
	KW::TwoWallAdjust(vec3(0.0f, -1.0f, 0.0f), delta, vec3(1.0f, 0.0f, 0.0f), vec3(0.7071f, 0.7071f, 0.0f), 0.5f);
	CHECK_NEAR(length(delta), 0.0, 1e-6);
}

TEST(TwoWallAdjust_Corner)
{
	// Walls at right angles: move along the crease, turned round if it points away from where the move wanted to go.
	vec3 delta(-1.0f, -1.0f, 4.0f);
	KW::TwoWallAdjust(vec3(0.0f, 0.0f, 1.0f), delta, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), 0.25f);
	CHECK_NEAR(delta.x, 0.0, 1e-5);
	CHECK_NEAR(delta.y, 0.0, 1e-5);
	CHECK_NEAR(delta.z, 3.0, 1e-5);

	delta = vec3(-1.0f, -1.0f, 4.0f);
	KW::TwoWallAdjust(vec3(0.0f, 0.0f, -1.0f), delta, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), 0.25f);
	CHECK_NEAR(delta.z, -3.0, 1e-5);
}

TEST(ApplyOpacityFlags_OnlyBelowOne)
{
	uint32_t flags = PF_Translucent | PF_Masked;
	KW::ApplyOpacityFlags(1.0f, flags);
	CHECK_EQ(flags, (uint32_t)(PF_Translucent | PF_Masked));

	KW::ApplyOpacityFlags(0.5f, flags);
	CHECK_EQ(flags, (uint32_t)(PF_Highlighted | PF_Masked));

	flags = PF_Modulated;
	KW::ApplyOpacityFlags(0.0f, flags);
	CHECK_EQ(flags, (uint32_t)PF_Highlighted);
}
