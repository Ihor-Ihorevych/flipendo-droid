#include "Precomp.h"
#include "KW.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Render/RenderSubsystem.h"
#include "Engine.h"
#include <cmath>
#include <chrono>

namespace KW
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


namespace KW
{
	// The loading screen, part 1: before the old level goes, LoadMap clears LevelInfo.Pauser, the player's
	// bShowMenu and LevelAction, then, if Console.FadeoutTime > 0 (HPConsole: 0.5 s) and the console drew the world
	// last frame (Console.bDrewWorld = !bNoDrawWorld, set by UConsole::PostRender; SurrealEngine raises the script
	// PostRender directly, so it's read from bNoDrawWorld here), keeps drawing frames while it lowers the player's
	// FlashFog.W (the brightness) by elapsed time / FadeoutTime. Then W = 0 and one more (black) frame.
	// IDA Engine.dll: ?LoadMap@UGameEngine@@UAEPAVULevel@@ABVFURL@@PAVUPendingLevel@@PBV?$TMap@VFString@@V1@@@AAVFString@@@Z [HP1 0x1039C3D0] (the block before the package checks)
	static bool LoadingFromLevel = false;

	void LoadMapFadeOut()
	{
		LoadingFromLevel = engine->Level && engine->LevelInfo && engine->viewport;
		if (!LoadingFromLevel)
			return;
		UPlayerPawn* player = engine->viewport->Actor();
		engine->LevelInfo->Pauser() = "";
		if (player)
			player->bShowMenu() = false;
		engine->LevelInfo->LevelAction() = 0; // LEVACT_None
		if (!player)
			return;

		float* fog = &player->FlashFog().x; // FPlane: X, Y, Z, W
		UConsole* console = engine->console;
		float fadeoutTime = (console && console->HasProperty("FadeoutTime")) ? *static_cast<float*>(console->GetProperty("FadeoutTime")) : 0.0f;
		if (fadeoutTime > 0.0f && console && !console->bNoDrawWorld())
		{
			auto prev = std::chrono::steady_clock::now();
			while (fog[3] > 0.0f)
			{
				engine->render->DrawGame(0.0f);
				auto cur = std::chrono::steady_clock::now();
				fog[3] -= std::chrono::duration<float>(cur - prev).count() / fadeoutTime;
				prev = cur;
			}
		}
		fog[3] = 0.0f;
		engine->render->DrawGame(0.0f);
	}

	// The loading screen, part 2: once the new package's LevelInfo0 is loaded (and an empty LevelEnterText set to the
	// URL's map, LevelInfoLoaded), LoadMap locks the viewport (cleared to black), raises
	// Console.DrawLevelInfo(Canvas, LevelEnterText) and shows the frame while the rest of the level loads. HPConsole
	// draws the parchment with the level's title and objective (docs/re/hp1/menus.md). Only with a viewport: the
	// startup map is loaded by UGameEngine::Init before it opens the first viewport, so it never shows one.
	// IDA Engine.dll: ?LoadMap@UGameEngine@@UAEPAVULevel@@ABVFURL@@PAVUPendingLevel@@PBV?$TMap@VFString@@V1@@@AAVFString@@@Z [HP1 0x1039C3D0] (after LoadObject LevelInfo0; FindFunctionChecked on the console)
	void LoadMapLevelInfo(ULevelInfo* levelInfo)
	{
		if (!LoadingFromLevel || !levelInfo || !engine->console || !engine->viewport)
			return;
		LoadingFromLevel = false;
		engine->render->DrawLevelInfo(levelInfo->LevelEnterText());
	}
}
