#include "Precomp.h"
#include "KW.h"
#include "KWGame.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Render/RenderSubsystem.h"
#include "Engine.h"
#include "Utils/StrTools.h"
#include <cmath>
#include <chrono>
#include <thread>
#ifdef __ANDROID__
#include <atomic>
extern std::atomic<float> g_fovOffset; // android/jni/android_main.cpp
extern std::atomic<bool> g_firstPerson; // android/jni/android_main.cpp: Settings > First person
#endif

namespace KW
{
	// HP1's FovAngle is the horizontal FOV of a 4:3 screen and its cutscenes are framed for that. UE1 keeps
	// the horizontal FOV at any aspect ratio, which on a wide screen crops the top and bottom of every shot
	// (Dumbledore at the top of the stairs ends up under the letterbox bar). Keep the 4:3 vertical FOV and
	// widen the horizontal one instead ("Hor+"). Screens narrower than 4:3 keep the original FOV.
	float ViewFovAngle(float fovAngle, int width, int height)
	{
#ifdef __ANDROID__
		// the player's field of view setting (android settings panel), in degrees on top of the game's
		fovAngle = std::clamp(fovAngle + g_fovOffset.load(), 40.0f, 150.0f);
#endif
		if (width <= 0 || height <= 0)
			return fovAngle;
		float widen = ((float)width / (float)height) / (4.0f / 3.0f);
		if (widen <= 1.0f || fovAngle <= 0.0f || fovAngle >= 180.0f)
			return fovAngle;
		float halfTan = std::tan(fovAngle * (3.14159265f / 360.0f)) * widen;
		return std::atan(halfTan) * (360.0f / 3.14159265f);
	}

	// A mod of ours, not in the original: the view from Harry's eyes. The game's own camera (a BaseCam actor behind him) is
	// replaced only while he is walking about; cutscenes, spell lessons, duels, the broom, shops and menus keep it. The
	// renderer hides the viewport player when the camera actor is the player himself (VisibleActor: as if bOwnerNoSee).
	void FirstPersonView(UActor*& cameraActor, vec3& location, Rotator& rotation)
	{
#ifdef __ANDROID__
		if (!g_firstPerson.load())
			return;
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (!player)
			return;
		// Walking, and aiming the wand: casting a spell (AltFire) sends HP1's Harry to state PlayerAiming for as long as the
		// spell is held, and the view must stay in his head then (it jumped to the third person camera at every cast)
		const std::string state = player->GetStateName().ToString();
		if (!StrTools::equals_ignore_case(state, "PlayerWalking") && !StrTools::equals_ignore_case(state, "PlayerAiming"))
		{
			static std::string lastOther;
			if (state != lastOther)
			{
				lastOther = state;
				LogMessage("First person: not in state " + state);
			}
			return;
		}
		UObject* hud = ObjectProperty(player, "myHUD");
		if (hud && BoolProperty(hud, "bCutSceneMode"))
			return;

		const Rotator view = player->ViewRotation();
		float eye = player->BaseEyeHeight();
		if (eye <= 1.0f)
			eye = player->CollisionHeight() * 0.8f;
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			LogMessage("First person: eye height " + std::to_string(eye) + " (base " + std::to_string(player->BaseEyeHeight()) + ", collision " + std::to_string(player->CollisionHeight()) + ")");
		}
		// in front of the head, so that its own mesh stays out of the picture
		const float yaw = view.Yaw * (6.2831853f / 65536.0f);
		cameraActor = player;
		location = player->Location() + vec3(std::cos(yaw) * 18.0f, std::sin(yaw) * 18.0f, eye);
		rotation = view;
#endif
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
		if (!IsHP1())
			return;
		// Not in the original: a fast load would flash the screen by, so keep redrawing it (every frame, which also
		// fills every swapchain image) for MinLoadingScreenSeconds before the rest of the level loads.
		const float MinLoadingScreenSeconds = 3.5f;
		auto start = std::chrono::steady_clock::now();
		while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < MinLoadingScreenSeconds)
		{
			engine->render->DrawLevelInfo(levelInfo->LevelEnterText());
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	}
}
