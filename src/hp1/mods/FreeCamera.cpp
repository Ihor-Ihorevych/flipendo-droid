#include "Precomp.h"
#include "HP1Mods.h"
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Package/PackageManager.h"
#include "Math/coords.h"
#include "VM/Frame.h"

// AdamJD's independent, smoothed orbit on retail packages, with camera-relative movement and free idle facing.
// The extra camera fields in his replacement packages live here instead; no game packages or bindings change.
namespace HP1::Mods
{
	namespace
	{
		UClass* HarryClass = nullptr;
		UClass* CameraClass = nullptr;
		UClass* TargetClass = nullptr;
		UFunction* WalkingPlayerTick = nullptr;
		UFunction* AimingPlayerTick = nullptr;
		UFunction* CameraTick = nullptr;
		UFunction* CameraBeginState = nullptr;
		UFunction* CameraEndState = nullptr;
		UFunction* TargetSetTarget = nullptr;
		UActor* OrbitCamera = nullptr;
		ULevel* OrbitLevel = nullptr;
		float TargetYaw = 0.0f;
		float TargetPitch = 0.0f;
		float CurrentYaw = 0.0f;
		float CurrentPitch = 0.0f;
		float Distance = 128.0f;
		float MousePriorityRemaining = 0.0f;

		UActor* ControlledCamera(UPlayerPawn* player)
		{
			if (!IsOn("free-cam") || !engine->console || !engine->LevelInfo ||
				!engine->LevelInfo->Pauser().empty() || player->bStrafe() != 0 || player->IsA("BroomHarry") ||
				(player->GetStateName() != NameString("PlayerWalking") && player->GetStateName() != NameString("playeraiming")) ||
				(player->Physics() != PHYS_Walking && player->Physics() != PHYS_Falling))
				return nullptr;
			UObject* book = ObjectProperty(engine->console, "menuBook");
			if (!book || !BoolProperty(book, "bGamePlaying") || BoolProperty(book, "bIsOpen") ||
				BoolProperty(ObjectProperty(player, "myHUD"), "bCutSceneMode") ||
				BoolProperty(player, "bKeepStationary") || BoolProperty(player, "bLockedOnTarget") ||
				BoolProperty(player, "bFixedFaceDirection") || BoolProperty(player, "bScreenRelativeMovement") ||
				BoolProperty(player, "bReverseInput") || BoolProperty(player, "bConstrainYaw") ||
				ObjectProperty(player, "CarryingActor"))
				return nullptr;
			UActor* camera = UObject::TryCast<UActor>(ObjectProperty(player, "cam"));
			return camera && camera->GetStateName() == NameString("StandardState") &&
				!BoolProperty(camera, "bInSpecialPause") ? camera : nullptr;
		}

		void InitOrbit(UActor* camera)
		{
			if (OrbitCamera == camera && OrbitLevel == engine->Level)
				return;
			OrbitCamera = camera;
			OrbitLevel = engine->Level;
			Rotator rotation = normalize(camera->Rotation());
			CurrentYaw = TargetYaw = (float)rotation.Yaw;
			CurrentPitch = TargetPitch = 0.0f;
			Distance = 128.0f;
			MousePriorityRemaining = 0.0f;
		}

		void UpdateOrbit(UPlayerPawn* player, UActor* camera, float elapsed)
		{
			InitOrbit(camera);
			float mouseX = player->SmoothMouseX();
			float mouseY = player->SmoothMouseY();
			if (std::abs(mouseX) > 0.01f || std::abs(mouseY) > 0.01f)
				MousePriorityRemaining = 0.75f;
			else
				MousePriorityRemaining = std::max(0.0f, MousePriorityRemaining - elapsed);
			TargetYaw = std::remainder(TargetYaw + std::clamp(mouseX * elapsed, -20000.0f, 20000.0f) * 4.0f, 65536.0f);
			bool aiming = player->GetStateName() == NameString("playeraiming");
			float minPitch = aiming ? -8000.0f : -14000.0f;
			float maxPitch = aiming ? 12000.0f : 14000.0f;
			TargetPitch = std::clamp(TargetPitch + std::clamp(mouseY * elapsed, -10000.0f, 10000.0f) * 4.0f, minPitch, maxPitch);
			CurrentPitch = std::clamp(CurrentPitch, minPitch, maxPitch);
			float blend = std::min(1.0f, 8.0f * elapsed);
			CurrentYaw = std::remainder(CurrentYaw + std::remainder(TargetYaw - CurrentYaw, 65536.0f) * blend, 65536.0f);
			CurrentPitch += (TargetPitch - CurrentPitch) * blend;
			Rotator rotation((int)CurrentPitch, (int)CurrentYaw, 0);
			camera->SetRotation(rotation);
			camera->DesiredRotation() = static_cast<UPawn*>(camera)->ViewRotation() = rotation;
		}

		vec3 CameraPosition(UActor* camera, UPlayerPawn* player, const vec3& from, const vec3& to)
		{
			float fraction = 1.0f;
			for (const CollisionHit& hit : engine->Level->Collision.Trace(from, to, 5.0f, 5.0f, true, true, false))
			{
				if (hit.Actor != camera && hit.Actor != player &&
					(hit.Actor == engine->LevelInfo || hit.Actor->bBlockActors() || hit.Actor->bBlockPlayers()))
					fraction = std::min(fraction, hit.Fraction);
			}
			return from + (to - from) * fraction;
		}

		void NCameraTick(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
			UActor* camera = static_cast<UActor*>(self);
			if (!player || ControlledCamera(player) != camera)
			{
				OrbitCamera = nullptr;
				Frame::Call(CameraTick, self, ArrayView<ExpressionValue>(args, 1));
				return;
			}
			InitOrbit(camera);
			// Retail Standardstate also has a latent TurnToward loop; the orbit supplies its own rotation.
			camera->BoolValue(camera->GetPropertyDataOffset("bTurnToward")) = false;
			vec3 focus = CameraPosition(camera, player, player->Location(), player->Location() + vec3(0.0f, 0.0f, 55.0f));
			vec3 forward = Coords::Rotation(Rotator((int)CurrentPitch, (int)CurrentYaw, 0)).XAxis;
			float desiredDistance = 128.0f * std::max(0.15f, 1.0f - std::max(0.0f, CurrentPitch) / 10000.0f);
			vec3 goal = CameraPosition(camera, player, focus, focus - forward * desiredDistance);
			desiredDistance = length(goal - focus);
			// Pull in immediately at walls, ease back out; trace both the focus offset and the whole orbit arm.
			Distance = desiredDistance < Distance ? desiredDistance : Distance + (desiredDistance - Distance) * std::min(1.0f, 2.5f * args[0].ToFloat());
			camera->SetLocation(CameraPosition(camera, player, focus, focus - forward * Distance));
			Rotator rotation((int)CurrentPitch, (int)CurrentYaw, 0);
			camera->SetRotation(rotation);
			camera->DesiredRotation() = static_cast<UPawn*>(camera)->ViewRotation() = rotation;
		}

		void NCameraBeginState(UObject* self, ExpressionValue*)
		{
			OrbitCamera = nullptr;
			Frame::Call(CameraBeginState, self, ArrayView<ExpressionValue>());
		}

		void NCameraEndState(UObject* self, ExpressionValue*)
		{
			OrbitCamera = nullptr;
			Frame::Call(CameraEndState, self, ArrayView<ExpressionValue>());
		}

		void PlayerTick(UObject* self, ExpressionValue* args, UFunction* original, bool aiming)
		{
			UPlayerPawn* player = static_cast<UPlayerPawn*>(self);
			UActor* camera = ControlledCamera(player);
			if (!camera)
			{
				OrbitCamera = nullptr;
				Frame::Call(original, self, ArrayView<ExpressionValue>(args, 1));
				return;
			}
			float elapsed = args[0].ToFloat();
			UpdateOrbit(player, camera, elapsed);
			Rotator facing = player->Rotation();
			Rotator movement(0, (int)CurrentYaw, 0);
			bool cameraRelative = aiming || IsOn("improved-movement");
			if (cameraRelative)
			{
				player->SetRotation(movement);
				player->ViewRotation().Yaw = player->DesiredRotation().Yaw = movement.Yaw;
			}
			// The orbit consumes horizontal mouse input; the retail move must not turn Harry a second time.
			float mouseX = player->SmoothMouseX();
			player->SmoothMouseX() = 0.0f;
			player->aTurn() = cameraRelative ? 0.0f : player->aTurn() - mouseX;
			Frame::Call(original, self, ArrayView<ExpressionValue>(args, 1));
			player->SmoothMouseX() = mouseX;
			if (ControlledCamera(player) != camera || !cameraRelative)
				return;

			vec3 acceleration = player->Acceleration();
			acceleration.z = 0.0f;
			if (aiming || player->GetStateName() == NameString("playeraiming"))
				facing = movement;
			else if (dot(acceleration, acceleration) > 1.0f)
			{
				int target = Rotator::FromVector(acceleration).Yaw;
				facing.Yaw = Rotator::TurnToShortest(facing.Yaw, target, (int)(98304.0f * elapsed));
				if (MousePriorityRemaining == 0.0f)
				{
					vec3 right = Coords::Rotation(movement).YAxis;
					TargetYaw = std::remainder(TargetYaw + dot(acceleration, right) / length(acceleration) * 16384.0f * elapsed, 65536.0f);
				}
			}
			player->SetRotation(facing);
			player->DesiredRotation().Yaw = player->ViewRotation().Yaw = facing.Yaw;
		}

		void NWalkingPlayerTick(UObject* self, ExpressionValue* args) { PlayerTick(self, args, WalkingPlayerTick, false); }
		void NAimingPlayerTick(UObject* self, ExpressionValue* args) { PlayerTick(self, args, AimingPlayerTick, true); }

		void NTargetSetTarget(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(ObjectProperty(self, "p"));
			bool controlled = player && ControlledCamera(player) == OrbitCamera && OrbitCamera &&
				player->GetStateName() == NameString("playeraiming");
			float mouseY = controlled ? player->SmoothMouseY() : 0.0f;
			float* forward = controlled ? static_cast<float*>(player->GetProperty("aForMove")) : nullptr;
			float savedForward = forward ? *forward : 0.0f;
			if (controlled)
			{
				*static_cast<int*>(self->GetProperty("TargetPitch")) = (int)std::clamp(CurrentPitch, -8000.0f, 12000.0f);
				player->SmoothMouseY() = *forward = 0.0f;
			}
			Frame::Call(TargetSetTarget, self, ArrayView<ExpressionValue>(args, 1));
			if (controlled)
			{
				player->SmoothMouseY() = mouseY;
				*forward = savedForward;
			}
		}
	}

	void TickFreeCamera()
	{
		if (!IsOn("free-cam"))
			return;
		if (OrbitLevel != engine->Level)
			OrbitCamera = nullptr;
		UClass* cls = engine->packages->FindClass("HarryPotter.Harry");
		if (cls && cls != HarryClass)
		{
			HarryClass = cls;
			WalkingPlayerTick = WrapScriptFunction(cls, "PlayerWalking", "PlayerTick", &NWalkingPlayerTick);
			AimingPlayerTick = WrapScriptFunction(cls, "playeraiming", "PlayerTick", &NAimingPlayerTick);
		}
		cls = engine->packages->FindClass("HPBase.BaseCam");
		if (cls && cls != CameraClass)
		{
			CameraClass = cls;
			CameraTick = WrapScriptFunction(cls, "Standardstate", "Tick", &NCameraTick);
			CameraBeginState = WrapScriptFunction(cls, "Standardstate", "BeginState", &NCameraBeginState);
			CameraEndState = WrapScriptFunction(cls, "Standardstate", "EndState", &NCameraEndState);
		}
		cls = engine->packages->FindClass("HPBase.Target");
		if (cls && cls != TargetClass)
		{
			TargetClass = cls;
			TargetSetTarget = WrapScriptFunction(cls, "seeking", "setTarget", &NTargetSetTarget);
		}
	}
}
