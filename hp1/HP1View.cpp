#include "Precomp.h"
#include "HP1.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include <cmath>

namespace HP1
{
	// HP1's FovAngle is the horizontal FOV of a 4:3 screen and its cutscenes are framed for that. UE1 keeps
	// the horizontal FOV at any aspect ratio, which on a wide screen crops the top and bottom of every shot
	// (Dumbledore at the top of the stairs ends up under the letterbox bar). Keep the 4:3 vertical FOV and
	// widen the horizontal one instead ("Hor+"). Screens narrower than 4:3 keep the original FOV.
	float ViewFovAngle(float fovAngle, int width, int height)
	{
		if (width <= 0 || height <= 0)
			return fovAngle;
		float widen = ((float)width / (float)height) / (4.0f / 3.0f);
		if (widen <= 1.0f || fovAngle <= 0.0f || fovAngle >= 180.0f)
			return fovAngle;
		float halfTan = std::tan(fovAngle * (3.14159265f / 360.0f)) * widen;
		return std::atan(halfTan) * (360.0f / 3.14159265f);
	}

	// KnowWonder dropped PlayerPawn.FlashScale: FlashFog is a plane whose W is the scene brightness (1 = normal,
	// 0 = black; PlayerPawn.ViewFlash eases it, ClientFadeIn/Out and the cutscene FadeIn/FadeOut drive it).
	// UGameEngine::Draw hands the render device FlashScale = clamp(W * 0.5) and FlashFog = clamp(XYZ), and the
	// device's EndFlash does the stock UE1 out = scene * min(2 * FlashScale, 1) + FlashFog. (The ScreenFlashes
	// client option that can turn it off isn't ported.)
	// IDA Engine.dll: ?Draw@UGameEngine@@UAEXPAVUViewport@@HPAEPAH@Z [HP1 0x1039FA40] (FlashScale/FlashFog before RenDev->Lock)
	void ViewFlashParams(UPlayerPawn* player, vec3& flashScale, vec3& flashFog)
	{
		const float* fog = &player->FlashFog().x; // FPlane: X, Y, Z, W
		float scale = std::clamp(fog[3] * 0.5f, 0.0f, 1.0f);
		flashScale = vec3(scale);
		flashFog = vec3(std::clamp(fog[0], 0.0f, 1.0f), std::clamp(fog[1], 0.0f, 1.0f), std::clamp(fog[2], 0.0f, 1.0f));
	}
}
