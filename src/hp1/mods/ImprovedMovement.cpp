#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/UViewport.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Anim/KWAnimation.h"
#include "KWActor.h"
#include "Math/coords.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

// Improved movement (on by default, off with --vanilla or --classic-movement): HP2's controls in HP1, after AdamJD's
// Mouse Fix and Strafing mod (on the HP unreal wiki as "Widescreen Mod (Improved Movement)"). That mod replaces seven
// of the game's packages; this one changes none, so saves load the same with it on or off.
//
// - Left / Right strafe instead of turning; the mouse still turns Harry.
// - Harry keeps moving while a spell is held: forward / back, strafing and turning with the mouse. The spell aims up
//   and down with the mouse alone, as in HP2. He doesn't climb or jump while a spell is held.
// - Moving the mouse up and down looks up and down while Harry moves too, not only while he stands still.
//
// It works through the game's own scripts (HarryPotter.Harry, HPBase.BaseCam / Target; docs/re/hp1/controls.md),
// wrapping some of Harry's functions (WrapScriptFunction):
// - DefUser.ini binds Left and Right to both RotLeft / RotRight (aBaseX, which PlayerPawn.PlayerInput adds to aTurn
//   with the mouse's SmoothMouseX) and StrafeLeft / StrafeRight (aStrafe). Harry.PlayerWalking.PlayerMove throws
//   aStrafe away unless Harry is falling or locked on a boss. The PlayerMove wrapper leaves only the mouse in aTurn
//   and keeps aStrafe; the ProcessMove wrapper adds it to NewAccel (aStrafe * 0.08, the scale PlayerMove gives
//   aForward), so ProcessMove's own animation code sees the whole move and TweenToRunning / PlayRunning pick
//   StrafeLeft / StrafeRight / runback, which they already have for the boss fights.
// - Harry.playeraiming.PlayerMove only moves Harry while fTimeToStop (zeroed in BeginState, counted down by
//   PlayerMove) is not below zero; the wrapper keeps it at 1. HPBase.Target pitches the aim by the forward / back
//   keys (aForMove) unless bNoMove is set; it is set every frame. The original never moves Harry while he aims (but
//   on a boss), so nothing animates his legs then: while he moves, the wand stays up on his spine channel
//   (HarryAnimChannel, the one he throws with while running) and the legs run on the main channel, both played
//   AT_Combine so neither clears the other; standing still he waves the wand with his whole body as in the original.
//   The release plays 'cast' on the whole body (AT_Replace, which clears every channel) and waits for it; meanwhile
//   the run goes on in one transient channel per thigh. The next animation the game plays (AT_Replace) clears them.
//   HarryAnimType, which the game's own animation functions pass on and save games keep, is left alone.
// - Moving into a ledge raises Mount (the ledge grab) and a gap can raise DoJump (auto jump); while aiming the
//   wrappers drop both, so Harry stops at the ledge or walks off the edge (the original's AltFire refuses to start
//   aiming in the air). In the air he plays PlayInAir's 'fall' until he lands.
// - BaseCam's Standardstate (GeneralStationaryModeCamera) and HPBase.CamTarget only follow the mouse's SmoothMouseY
//   while Harry is stationary, which the camera works out from HarrysPreviousPosition; while he moves they level the
//   view (and keep the camera at least at his eye height). After Harry's move the camera is given his new position,
//   so it keeps following the mouse.
//
// Bosses (bLockedOnTarget), the troll chase (bReverseInput, bConstrainYaw), fixed or screen-relative directions,
// bKeepStationary and the Strafe button keep the original controls and camera: those code paths already strafe or
// have their own rules. In the air the original's air control (which already strafes) applies.
namespace HP1::Mods
{
	namespace
	{
		bool Active()
		{
			return Enabled() && !HasFlag("--classic-movement");
		}

		template<typename T> T* Prop(UObject* obj, const char* name)
		{
			return obj && obj->HasProperty(name) ? static_cast<T*>(obj->GetProperty(name)) : nullptr;
		}

		void SetBoolProperty(UObject* obj, const char* name, bool value)
		{
			if (obj && obj->HasProperty(name))
				obj->BoolValue(obj->GetPropertyDataOffset(name)) = value;
		}

		UFunction* WalkingPlayerMove = nullptr;
		UFunction* WalkingProcessMove = nullptr;
		UFunction* AimingPlayerMove = nullptr;
		UFunction* AimingProcessMove = nullptr;
		UFunction* HarryMount = nullptr;
		UFunction* HarryDoJump = nullptr;

		// Set by the PlayerMove wrappers for the ProcessMove its script calls: aStrafe and Harry's right axis.
		float PendingStrafe = 0.0f;
		vec3 PendingRight = vec3(0.0f, 0.0f, 0.0f);

		// None of the original's special movement modes (bosses, troll chase, fixed or screen-relative facing, kept
		// still, the Strafe button) is on.
		bool NoSpecialMode(UPlayerPawn* harry)
		{
			return harry->bStrafe() == 0 &&
				!BoolProperty(harry, "bLockedOnTarget") && !BoolProperty(harry, "bFixedFaceDirection") &&
				!BoolProperty(harry, "bScreenRelativeMovement") && !BoolProperty(harry, "bReverseInput") &&
				!BoolProperty(harry, "bConstrainYaw") && !BoolProperty(harry, "bKeepStationary");
		}

		// Harry walks the way the original's plain controls move him (Left / Right turn, aStrafe unused).
		bool PlainControls(UPlayerPawn* harry)
		{
			return harry->Physics() == PHYS_Walking && NoSpecialMode(harry);
		}

		bool Aiming(UObject* harry)
		{
			return harry->GetStateName() == NameString("playeraiming");
		}

		// While a spell is held and the extra moves Harry (not on a boss, where the original moves him itself)
		bool MovingWhileAiming(UPlayerPawn* harry)
		{
			return Active() && Aiming(harry) && NoSpecialMode(harry);
		}

		// Before the script's PlayerMove: the keys strafe, the mouse turns.
		void BeforePlayerMove(UPlayerPawn* harry)
		{
			PendingStrafe = 0.0f;
			if (!Active() || !PlainControls(harry))
				return;
			PendingStrafe = harry->aStrafe();
			PendingRight = Coords::Rotation(harry->Rotation()).YAxis;
			harry->aTurn() = harry->SmoothMouseX();
		}

		// After Harry's move: the camera takes him as standing where he is, so it keeps following the mouse's pitch.
		void AfterPlayerMove(UPlayerPawn* harry)
		{
			PendingStrafe = 0.0f;
			if (!Active() || !NoSpecialMode(harry))
				return;
			UObject* cam = ObjectProperty(harry, "cam");
			if (cam && cam->GetStateName() == NameString("Standardstate"))
			{
				if (vec3* previous = Prop<vec3>(cam, "HarrysPreviousPosition"))
					*previous = harry->Location();
			}
		}

		// NewAccel is PlayerMove's own variable (the VM passes a variable by reference); ProcessMove is the last thing
		// both PlayerMoves do with it.
		void AddStrafe(ExpressionValue& newAccel)
		{
			if (PendingStrafe == 0.0f)
				return;
			vec3 accel = newAccel.ToVector() + PendingRight * (PendingStrafe * 0.08f);
			accel.z = 0.0f;
			newAccel.Store(ExpressionValue::VectorValue(accel));
		}

		// Calls a wrapped function's script body with the arguments the wrapper was called with. Frame::CallNative
		// gives a missing optional argument as Nothing (DoJump()); passing only the ones before it lets Frame::Call
		// give the rest their defaults, as for a script call.
		void CallOriginal(UFunction* original, UObject* self, ExpressionValue* args)
		{
			size_t count = 0;
			for (UProperty* prop : original->Properties)
			{
				if (!AllFlags(prop->PropFlags, PropertyFlags::Parm))
					continue;
				if (args[count].GetType() == ExpressionValueType::Nothing)
					break;
				count++;
			}
			Frame::Call(original, self, ArrayView<ExpressionValue>(args, count));
		}

		NameString RunAnim(UPlayerPawn* harry, const vec3& accel)
		{
			// Harry.PlayRunning's choice (it also halves Velocity.X backing up, so it isn't called every frame)
			Coords axes = Coords::Rotation(harry->Rotation());
			vec3 dir = normalize(accel);
			if (dot(dir, axes.XAxis) >= 0.75f)
				return NameString("run");
			if (dot(dir, axes.XAxis) < -0.75f)
				return NameString("runback");
			return NameString(dot(dir, axes.YAxis) > 0.0f ? "StrafeRight" : "StrafeLeft");
		}

		UActor* SpineChannel(UPlayerPawn* harry)
		{
			return UObject::TryCast<UActor>(ObjectProperty(harry, "HarryAnimChannel"));
		}

		// The original only throws with the spine channel; 'wave' on it is this extra's
		void PutWandBack(UPlayerPawn* harry)
		{
			UActor* channel = SpineChannel(harry);
			if (channel && channel->AnimSequence() == NameString("wave"))
			{
				channel->AnimSequence() = NameString();
				channel->AnimRate() = 0.0f;
			}
		}

		const char* const LegBones[] = { "Harry L Thigh", "Harry R Thigh" };
		NameString LastLegs;
		float LastLegsFrame = 0.0f;

		UActor* LegChannel(UPlayerPawn* harry, const char* bone)
		{
			USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(harry->Mesh());
			int index = mesh ? KW::BoneIndex(mesh, NameString(bone)) : -1;
			if (index < 0)
				return nullptr;
			for (UActor* aux : KW::AuxAnims(harry))
			{
				if (aux && KW::AnimBone(aux) == index && KW::bAnimTransient(aux))
					return aux;
			}
			return nullptr;
		}

		// The run in the legs while 'cast' plays on the whole body, started where the legs were. A transient channel's
		// notifies go to Harry, so only the left one plays the footsteps.
		void RunOnLegs(UPlayerPawn* harry, const NameString& legs)
		{
			for (int i = 0; i < 2; i++)
			{
				UActor* channel = LegChannel(harry, LegBones[i]);
				bool created = !channel;
				if (!channel || channel->AnimSequence() != legs)
					KW::PlayAnim(harry, legs, true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Combine, NameString(LegBones[i]));
				channel = LegChannel(harry, LegBones[i]);
				if (!channel)
					continue;
				if (created && legs == LastLegs)
					channel->AnimFrame() = LastLegsFrame;
				if (i == 0)
				{
					LastLegs = legs;
					LastLegsFrame = channel->AnimFrame();
				}
				else
				{
					channel->bAnimNotify() = false;
				}
			}
		}

		void StopLegs(UPlayerPawn* harry)
		{
			for (const char* bone : LegBones)
			{
				if (LegChannel(harry, bone))
					KW::PlayAnim(harry, NameString(), false, 1.0f, 0.0f, 0.0f, EAnimType::AT_Combine, NameString(bone));
			}
		}

		// playeraiming's animation while the extra moves Harry
		void AimingAnims(UPlayerPawn* harry)
		{
			bool release = harry->AnimSequence() == NameString("cast");
			if (harry->Physics() != PHYS_Walking)
			{
				// Walked off an edge with the spell held. Landed (PlayerPawn's, playeraiming has none) plays
				// PlayLanded; back on the ground the wand comes up again below.
				PutWandBack(harry);
				StopLegs(harry);
				if (!release && harry->AnimSequence() != NameString("fall"))
					CallEvent(harry, NameString("PlayInAir"));
				return;
			}

			vec3 accel = harry->Acceleration();
			accel.z = 0.0f;
			bool moving = dot(accel, accel) > 0.0f;

			if (release)
			{
				PutWandBack(harry);
				if (moving)
					RunOnLegs(harry, RunAnim(harry, accel));
				else
					StopLegs(harry);
				return;
			}

			if (moving)
			{
				UActor* channel = SpineChannel(harry);
				if (channel && channel->AnimSequence() != NameString("wave"))
					KW::PlayAnim(channel, NameString("wave"), true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Replace, NameString());
				NameString legs = RunAnim(harry, accel);
				if (harry->AnimSequence() != legs)
					KW::PlayAnim(harry, legs, true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Combine, NameString());
				LastLegs = legs;
				LastLegsFrame = harry->AnimFrame();
				SetBoolProperty(harry, "bMovingBackwards", legs == NameString("runback"));
			}
			else if (harry->AnimSequence() != NameString("wave"))
			{
				// Harry's playeraiming begin: loopanim('wave', , 0.2), which also clears the spine channel
				KW::PlayAnim(harry, NameString("wave"), true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Replace, NameString());
			}
		}

		void NWalkingPlayerMove(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			BeforePlayerMove(harry);
			CallOriginal(WalkingPlayerMove, self, args);
			AfterPlayerMove(harry);
		}

		void NWalkingProcessMove(UObject* self, ExpressionValue* args)
		{
			AddStrafe(args[1]);
			CallOriginal(WalkingProcessMove, self, args);
		}

		void NAimingPlayerMove(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			bool moving = MovingWhileAiming(harry);
			BeforePlayerMove(harry);
			if (moving && harry->Physics() == PHYS_Walking)
			{
				if (float* timeToStop = Prop<float>(harry, "fTimeToStop"))
					*timeToStop = 1.0f;
			}
			CallOriginal(AimingPlayerMove, self, args);
			if (moving)
			{
				SetBoolProperty(ObjectProperty(harry, "rectarget"), "bNoMove", true);
				AimingAnims(harry);
			}
			AfterPlayerMove(harry);
		}

		void NAimingProcessMove(UObject* self, ExpressionValue* args)
		{
			AddStrafe(args[1]);
			CallOriginal(AimingProcessMove, self, args);
		}

		void NMount(UObject* self, ExpressionValue* args)
		{
			if (!MovingWhileAiming(static_cast<UPlayerPawn*>(self)))
				CallOriginal(HarryMount, self, args);
		}

		void NDoJump(UObject* self, ExpressionValue* args)
		{
			if (!MovingWhileAiming(static_cast<UPlayerPawn*>(self)))
				CallOriginal(HarryDoJump, self, args);
		}

		UClass* HarryClass = nullptr;
	}

	void TickImprovedMovement()
	{
		if (!Active())
			return;

		UClass* cls = engine->packages->FindClass("HarryPotter.Harry");
		if (cls && cls != HarryClass)
		{
			HarryClass = cls;
			WalkingPlayerMove = WrapScriptFunction(cls, "PlayerWalking", "PlayerMove", &NWalkingPlayerMove);
			WalkingProcessMove = WrapScriptFunction(cls, "PlayerWalking", "ProcessMove", &NWalkingProcessMove);
			AimingPlayerMove = WrapScriptFunction(cls, "playeraiming", "PlayerMove", &NAimingPlayerMove);
			AimingProcessMove = WrapScriptFunction(cls, "playeraiming", "ProcessMove", &NAimingProcessMove);
			HarryMount = WrapScriptFunction(cls, nullptr, "Mount", &NMount);
			HarryDoJump = WrapScriptFunction(cls, nullptr, "DoJump", &NDoJump);
		}

		// Out of playeraiming (cast, or left some other way: a cutscene, a hit): the wand comes down, and once 'cast'
		// is over the legs stop running on their own (normally the next animation, played AT_Replace, has done it).
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (player && HarryClass && player->IsA(HarryClass->Name) && !Aiming(player))
		{
			PutWandBack(player);
			if (player->AnimSequence() != NameString("cast"))
				StopLegs(player);
		}
	}
}
