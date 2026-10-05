#include "Precomp.h"
#include "KW.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Collision/TopLevel/CollisionSystem.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "VM/NativeFunc.h"
#include "VM/ScriptCall.h"
#include "Math/coords.h"
#include "GameWindow.h"
#include "Engine.h"
#include <cmath>

// Pawn and PlayerPawn view natives SurrealEngine only stubs or doesn't have: FindStairRotation (524),
// ScreenToWorld (542), and Console.CreateNativeFont.

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// Stair look (PlayerPawn.PlayerWalking's PlayerMove with the "StairLook" option on, bLookUpStairs): while walking
	// and not looking around, pitch the view down towards stairs going down and up towards stairs going up. It probes
	// ahead along the view yaw: one sweep forward from eye height (a box of the collision radius, 1 unit tall) for how
	// far the way is clear, then sweeps straight down from the middle of that stretch and from its end to compare the
	// floor heights. Returns the new ViewRotation.Pitch, eased towards the target; the original also normalizes the
	// actor's ViewRotation.Pitch to -32768..32768 on the way. Steps of more than 0.33 s leave the pitch as it is.
	// IDA Engine.dll: ?execFindStairRotation@APawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D7020]
	static void NFindStairRotation(UObject* self, float deltaTime, int& returnValue)
	{
		UPawn* pawn = UObject::Cast<UPawn>(self);
		Rotator& view = pawn->ViewRotation();
		if (deltaTime > 0.33f)
		{
			returnValue = view.Pitch;
			return;
		}
		if (view.Pitch > 32768)
			view.Pitch = (view.Pitch & 0xffff) - 65536;

		ULevel* level = pawn->XLevel();
		if (!level)
		{
			returnValue = view.Pitch;
			return;
		}
		CollisionSystem& collision = level->Collision;
		TraceFlags flags;
		flags.movers = true;
		flags.world = true; // TRACE_Movers | TRACE_Level
		auto trace = [&](const vec3& start, const vec3& end, const vec3& extent) {
			return collision.TraceFirstHit(start, end, pawn, extent, flags).Fraction;
		};

		// The X axis of GMath.UnitCoords / (0, Yaw, Roll): the horizontal view direction.
		float yaw = Rotator(0, view.Yaw, 0).YawRadians();
		vec3 dir(std::cos(yaw), std::sin(yaw), 0.0f);
		vec3 eye = pawn->Location() + vec3(0.0f, 0.0f, pawn->BaseEyeHeight());
		float dist = pawn->CollisionHeight() + pawn->BaseEyeHeight();
		vec3 extent(pawn->CollisionRadius(), pawn->CollisionRadius(), 1.0f);

		float time = trace(eye, eye + dir * (2.0f * dist), extent);
		int stairRot = 0;
		float clear = 2.0f * dist * time;
		if (dist * 0.8f < clear)
		{
			float down = dist * 3.0f;
			vec3 spot = eye + dir * (clear * 0.5f);
			time = trace(spot, spot - vec3(0.0f, 0.0f, down), extent);
			if (time < 1.0f)
			{
				float firstHit = down * time;
				float level70 = dist * 0.7f;
				if (level70 - 6.0f > firstHit)
				{
					// Floor higher than eye level minus 70%: stairs going up.
					spot = eye + dir * clear;
					time = trace(spot, spot - vec3(0.0f, 0.0f, down), extent);
					stairRot = std::max(view.Pitch, 0);
					if (firstHit - 10.0f > down * time)
						stairRot = 5400;
				}
				else if (level70 + 6.0f < firstHit)
				{
					// Lower floor: stairs going down, if the way ahead at foot level is clear.
					time = trace(pawn->Location(), pawn->Location() + dir * (clear * 0.9f), vec3(0.0f));
					if (time == 1.0f)
					{
						spot = eye + dir * clear;
						time = trace(spot, spot - vec3(0.0f, 0.0f, down), extent);
						stairRot = std::min(view.Pitch, 0);
						if (firstHit + 10.0f < down * time)
							stairRot = -5000;
					}
				}
			}
		}

		int current = view.Pitch;
		int diff = std::abs(current - stairRot);
		if (diff > 0)
		{
			double rate = diff < 1000 ? (double)(8000 / diff) : 8.0;
			double f = std::min(rate * deltaTime, 1.0);
			stairRot = (int)((float)((1.0 - f) * current + stairRot * f));
		}
		returnValue = stairRot;
	}

	// The world point under screen position (S.X, S.Y) at depth S.Z: the offset in the view's frame
	// ((2X - SizeX) and (2Y - SizeY) scaled to the depth by the field of view, S.Z) turned by the camera rotation
	// that PlayerCalcView returns for this player and added to the camera location. No HP1 script calls it.
	// Uses the Hor+ field of view the view is actually drawn with (src/knowwonder/KWView.cpp); the original took FovAngle as is.
	// NOT verified in game: ported literally, including the original's axis order.
	// IDA Engine.dll: ?execScreenToWorld@APlayerPawn@@QAEXAAUFFrame@@QAX@Z [HP1 0x103D5AC0]
	static void NScreenToWorld(UObject* self, const vec3& screen, vec3& returnValue)
	{
		UPlayerPawn* player = UObject::Cast<UPlayerPawn>(self);
		float sizeX = (float)engine->window->GetPixelWidth();
		float sizeY = (float)engine->window->GetPixelHeight();
		float fov = ViewFovAngle(player->FovAngle(), (int)sizeX, (int)sizeY);
		float scale = sizeX > 0.0f ? screen.z * std::tan(fov * (3.14159265f / 360.0f)) / sizeX : 0.0f;
		vec3 offset((2.0f * screen.x - sizeX) * scale, (2.0f * screen.y - sizeY) * scale, screen.z);

		UObject* viewActor = player;
		vec3 cameraLocation = player->Location();
		Rotator cameraRotation = player->Rotation();
		if (UFunction* calcView = FindEventFunction(player, "PlayerCalcView"))
		{
			static UObjectProperty* objProp = GC::Alloc<UObjectProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
			static UStructProperty* vecProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
			static UStructProperty* rotProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
			vecProp->Struct = UObject::Cast<UStructProperty>(calcView->Properties[1])->Struct;
			rotProp->Struct = UObject::Cast<UStructProperty>(calcView->Properties[2])->Struct;
			CallEvent(player, "PlayerCalcView", {
				ExpressionValue::Variable(&viewActor, objProp),
				ExpressionValue::Variable(&cameraLocation, vecProp),
				ExpressionValue::Variable(&cameraRotation, rotProp)
			});
		}

		// FVector::TransformVectorBy(GMath.UnitCoords * CameraRotation)
		returnValue = cameraLocation + Coords::Rotation(cameraRotation) * offset;
	}

	// Console.CreateNativeFont(FontName, Height): HPConsole uses it for the Asian languages (SIM, CHI, TRA, KOR, THA,
	// JAP), whose fonts are system fonts named in SAPFont.int. Engine.dll hands it to the viewport (vtable +156);
	// the base UViewport returns None and the Windows viewport (WinDrv.dll, not reversed) rasterizes the system
	// font. Not ported yet: returns None like the base viewport, so those languages draw no menu text.
	// IDA Engine.dll: ?execCreateNativeFont@UConsole@@QAEXAAUFFrame@@QAX@Z [HP1 0x1038B860]
	// IDA Engine.dll: ?CreateNativeFont@UViewport@@UAEPAVUFont@@PBGH@Z [HP1 0x10350B30]
	static void NCreateNativeFont(UObject* self, const std::string& fontName, int height, UObject*& returnValue)
	{
		LogMessage("CreateNativeFont(" + fontName + ", " + std::to_string(height) + "): system fonts are not supported yet");
		returnValue = nullptr;
	}

	void RegisterPlayerNatives()
	{
		OverrideNative(524, [] { RegisterVMNativeFunc_2("Pawn", "FindStairRotation", &NFindStairRotation, 524); });
		OverrideNative(542, [] { RegisterVMNativeFunc_2("PlayerPawn", "ScreenToWorld", &NScreenToWorld, 542); });
		RegisterVMNativeFunc_3("Console", "CreateNativeFont", &NCreateNativeFont, 0);
	}
}
