#include "Precomp.h"
#include "HP1.h"
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
}
