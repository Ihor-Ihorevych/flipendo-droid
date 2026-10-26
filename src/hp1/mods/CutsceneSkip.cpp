#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Render/RenderSubsystem.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include <algorithm>

// HPBase.CutScene/CutScriptII command tracks must run in order: skipping their commands would lose world changes.
// This optional mod hides bounded fast-forward behind a fade instead of rewriting the scripts' waits.
namespace HP1::Mods
{
	static constexpr float FadeSeconds = 0.3f;
	static constexpr float MaxCameraTail = 3.0f;
	static constexpr float MaxSkipSeconds = 30.0f;
	static constexpr float SkipSpeed = 8.0f;
	static constexpr float SimulationStep = 1.0f / 60.0f;
	enum class SkipPhase { Idle, FadeOut, Running, FadeIn };
	static SkipPhase Phase = SkipPhase::Idle;
	static float Opacity = 0.0f;
	static float CameraTail = 0.0f;
	static float SkipElapsed = 0.0f;
	static NameString SceneName;
	static bool SpeechMuted = false;

	bool CutsceneSkipSpeechMuted() { return SpeechMuted; }

	static void MuteSpeech()
	{
		SpeechMuted = true;
		auto& sounds = engine->audiodev->GetPlayingSounds();
		for (size_t i = 0; i < sounds.size(); i++)
		{
			auto slot = USurrealAudioDevice::GetSoundSlot(sounds[i].Id);
			if (sounds[i].Id && (slot == SLOT_Interact || slot == SLOT_Talk))
				engine->audiodev->StopSoundAt(i);
		}
	}

	static bool SpellLessonActive()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		return player && player->GetStateName() == NameString("SpellLearning");
	}

	static UObject* ActiveCutscene()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* hud = player && !SpellLessonActive() ? player->myHUD() : nullptr;
		return hud && BoolProperty(hud, "bCutSceneMode") ? ObjectProperty(hud, "curCutScene") : nullptr;
	}

	static bool CameraInCutscene()
	{
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* cam = ObjectProperty(player, "cam");
		return cam && cam->GetStateName() == NameString("CutState");
	}

	static bool SceneStillPlaying()
	{
		if (engine->Level)
			for (UActor* actor : engine->Level->Actors)
				if (actor && !actor->bDeleteMe() && actor->Name == SceneName)
					return BoolProperty(actor, "bPlaying");
		return false;
	}

	static bool MenuOpen()
	{
		return BoolProperty(ObjectProperty(engine->console, "menuBook"), "bIsOpen");
	}

	bool CutsceneSkipBusy()
	{
		return Phase != SkipPhase::Idle;
	}

	bool CutsceneSkipAccelerating()
	{
		return Phase == SkipPhase::Running && engine->LevelInfo &&
			engine->LevelInfo->Pauser().empty() && !MenuOpen();
	}

	bool CutsceneSkipWorldPaused()
	{
		return Phase == SkipPhase::FadeIn && SpellLessonActive();
	}

	static void Reveal(const char* reason)
	{
		Phase = SkipPhase::FadeIn;
		SceneName = NameString();
		SpeechMuted = false;
		LogMessage(std::string("Cutscene skip: reveal (") + reason + ")");
	}

	void ResetCutsceneSkip()
	{
		if (!CutsceneSkipBusy())
			return;
		// The old map is about to be destroyed. Only the visual fade may cross the boundary.
		Reveal("level change/load");
		CameraTail = SkipElapsed = 0.0f;
	}

	bool CutsceneSkipKeyDown(int key)
	{
		if (CutsceneSkipBusy())
		{
			if (key == IK_Escape && Phase != SkipPhase::FadeIn)
				Reveal("cancelled");
			return true;
		}
		return IsOn("cutscene-skip") && key == IK_Space && ActiveCutscene() && !MenuOpen() &&
			engine->LevelInfo && engine->LevelInfo->Pauser().empty();
	}

	void TickCutsceneSkip(float realElapsed)
	{
		if (!IsOn("cutscene-skip") || !engine->LevelInfo)
			return;
		// Use real time for the reveal even when a frame stalls.
		float dt = std::max(realElapsed, 0.0f);
		if (Phase == SkipPhase::Idle)
		{
			UObject* scene = ActiveCutscene();
			if (!scene || !SpacePressed() || MenuOpen() || !engine->LevelInfo->Pauser().empty())
				return;
			SceneName = scene->Name;
			Phase = SkipPhase::FadeOut;
			Opacity = CameraTail = SkipElapsed = 0.0f;
			MuteSpeech();
			LogMessage("Cutscene skip: fade out " + SceneName.ToString());
		}
		if (Phase == SkipPhase::FadeIn)
		{
			Opacity = std::max(0.0f, Opacity - dt / FadeSeconds);
			if (Opacity == 0.0f)
			{
				Phase = SkipPhase::Idle;
				LogMessage("Cutscene skip: idle");
			}
			return;
		}
		if (MenuOpen() || !engine->LevelInfo->Pauser().empty())
		{
			Reveal("paused");
			return;
		}
		SkipElapsed += realElapsed;
		if (SkipElapsed > MaxSkipSeconds)
		{
			Reveal("timeout; scene continues normally");
			return;
		}
		if (ActiveCutscene())
			CameraTail = 0.0f;
		else
		{
			CameraTail += realElapsed;
			if (CameraTail > MaxCameraTail)
			{
				Reveal("camera tail timeout");
				return;
			}
		}
		if (Phase == SkipPhase::FadeOut)
		{
			Opacity = std::min(1.0f, Opacity + dt / FadeSeconds);
			if (Opacity == 1.0f)
			{
				Phase = SkipPhase::Running;
				LogMessage("Cutscene skip: running (8x, bounded steps)");
			}
		}
	}

	void CutsceneSkipSimulationTick()
	{
		if (Phase != SkipPhase::Running && Phase != SkipPhase::FadeOut)
			return;
		// These lessons take over Harry without clearing the old HUD curCutScene.
		if (SpellLessonActive())
		{
			Reveal("spell lesson; timer protected");
			return;
		}
		if (UObject* scene = ActiveCutscene())
			SceneName = scene->Name;
		else if (!CameraInCutscene() && !SceneStillPlaying())
			Reveal("finished");
	}

	int CutsceneSkipSteps(float& elapsed, bool accelerating)
	{
		if (!accelerating)
			return 1;
		// Advance the whole world together. Never compress individual script waits or save a speed override.
		elapsed = std::clamp(elapsed, 0.0f, 0.1f) * SkipSpeed;
		return std::max(1, (int)std::ceil(elapsed / SimulationStep));
	}

	bool CutsceneSkipPlayerTick(UPlayerPawn* player)
	{
		if (!CutsceneSkipBusy())
			return false;
		player->aForward() = player->aStrafe() = player->aTurn() = player->aLookUp() = 0.0f;
		player->aUp() = 0.0f;
		player->bJumpStatus() = false;
		player->bFire() = player->bAltFire() = 0;
		// Scripted CutMovingTo must keep running; released gameplay waits for the reveal.
		if (!BoolProperty(ObjectProperty(player, "myHUD"), "bCutSceneMode") &&
			player->GetStateName() == NameString("PlayerWalking"))
		{
			player->Acceleration() = vec3(0.0f);
			return true;
		}
		return false;
	}

	void DrawCutsceneSkip(UCanvas* canvas)
	{
		if (!IsOn("cutscene-skip"))
			return;
		if (Phase == SkipPhase::Idle)
		{
			if (ActiveCutscene() && !MenuOpen())
				DrawPrompt(canvas, "Press Space to skip");
			return;
		}
		static UTexture* black = nullptr;
		if (!black)
		{
			black = UObject::Cast<UTexture>(engine->packages->GetTransientPackage()->NewObject(
				"CutsceneSkipBlack", engine->packages->FindClass("Engine.Texture"), ObjectFlags::Transient));
			black->Format() = (uint8_t)TextureFormat::BGRA8;
			black->USize() = black->UClamp() = 256;
			black->VSize() = black->VClamp() = 1;
			black->UBits() = 8;
			black->VBits() = 0;
			UnrealMipmap mip;
			mip.Width = 256;
			mip.Height = 1;
			mip.UBits = 8;
			mip.VBits = 0;
			// Tile rendering ignores vertex alpha, so sample opacity from the texture.
			mip.Data.resize(256 * 4, 0);
			for (int x = 0; x < 256; x++)
				mip.Data[x * 4 + 3] = (uint8_t)x;
			black->UncompressedMipmaps.push_back(std::move(mip));
			black->UsedFormat = TextureFormat::BGRA8;
			black->UsedMipmaps = black->UncompressedMipmaps;
		}
		float alpha = Opacity * Opacity * (3.0f - 2.0f * Opacity);
		engine->render->DrawTile(black, 0, 0, (float)canvas->SizeX(), (float)canvas->SizeY(),
			std::round(alpha * 255.0f), 0, 1, 1, 1, {1, 1, 1, 1}, {}, PF_Highlighted | PF_NoSmooth);
		if (Phase == SkipPhase::Running)
			DrawPrompt(canvas, "Skipping...");
	}
}
