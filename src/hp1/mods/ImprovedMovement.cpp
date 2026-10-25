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
//   and down with the mouse alone, as in HP2. He jumps with the spell held too, and can start one in the air, but
//   doesn't climb.
// - Moving the mouse up and down looks up and down while Harry moves too, not only while he stands still.
// - AdamJD's smaller tweaks: half the sideways air control, slower blends at the end of an animation, the idle (not the
//   run) while Harry slides with no input, and spells that hit a creature at once from up close.
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
// - playeraiming.ProcessMove has PlayerWalking's jump commented out (PlayerWalking's BeginState clears bPressedJump,
//   so a jump pressed while aiming is lost); the wrapper does it as PlayerWalking does: DoJump, then clears it. In the air the wand stays
//   up and the main channel plays DoJump's jump, then 'fall' (playeraiming's AnimEnd is empty), the air control is
//   PlayerWalking's (aStrafe * 0.16 against playeraiming's 0.4, which only bosses meet) and landing plays
//   PlayerWalking's Landed sound (its volume comes from fTimeInAir, which PlayerWalking's ProcessFalling counts; the
//   wrapper counts it while aiming). Harry.AltFire refuses to start aiming while Harry falls; its wrapper lets it.
// - Harry.UpdateRotation returns at once while Harry falls, so nothing turns him in the air; after either PlayerMove
//   the extra turns him by the mouse as UpdateRotation would (aTurn * 0.24, clamped to turnRate, 24 or 32 a second).
// - Moving into a ledge raises Mount (the ledge grab) and a gap can raise DoJump (auto jump); while aiming the
//   wrappers drop both, so Harry stops at the ledge or walks off the edge.
// - Mantling, after AdamJD's mod: a ledge grabbed in the air less than 80 up is climbed at once, the way Mount climbs
//   from the ground (Mounting picks climb32 or climb64 by the height). The original's FallingMount waits until Harry
//   has fallen to 82 below the ledge (or landed) and pulls him up from there with climb96end; from 80 up that is
//   at once anyway (MaxMountHeight is 96.5).
// - Air control: PlayerWalking.PlayerMove gives aStrafe * 0.16 in the air, playeraiming 0.4; AdamJD's 0.08 for both.
// - Blends: PlayerWalking's AnimEnd starts the next animation with TweenToWaiting(0.2) / TweenToWalking(0.1) /
//   TweenToRunning(0.1); AdamJD's 0.4. TweenToRunning and PlayRunning play 'run' when there is no acceleration
//   (Harry still sliding, Left and Right both held); AdamJD's 'breath' (TweenToWaiting here).
// - Auto hit, from HP2 (AdamJD's baseWand.CastSpell): Harry's spell at a pawn less than 128 from the wand hits it as
//   it leaves the wand (the spell's ProcessTouch), where the projectile could start inside the pawn and never touch it.
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
			return IsOn("improved-movement");
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
		UFunction* HarryAltFire = nullptr;
		UFunction* WalkingAnimEnd = nullptr;
		UFunction* HarryTweenToRunning = nullptr;
		UFunction* HarryPlayRunning = nullptr;
		UFunction* HarryTweenToWaiting = nullptr;
		UFunction* WandCastSpell = nullptr;

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

		// In the air, with the mouse: Harry.UpdateRotation's yaw (it returns at once while falling). Air control uses the
		// facing PlayerMove started with, so it follows the turn from the next frame.
		void AirTurn(UPlayerPawn* harry, float deltaTime)
		{
			if (!Active() || harry->Physics() != PHYS_Falling || !NoSpecialMode(harry))
				return;
			float turn = harry->SmoothMouseX() * 0.24f;
			if (float* turnRate = Prop<float>(harry, "turnRate"))
				turn = std::clamp(turn, -*turnRate, *turnRate);
			vec3 accel = harry->Acceleration();
			int yaw = (int)((dot(accel, accel) == 0.0f ? 32.0f : 24.0f) * deltaTime * turn);
			if (yaw == 0)
				return;
			harry->ViewRotation().Yaw += yaw;
			harry->DesiredRotation().Yaw = harry->ViewRotation().Yaw;
			Rotator rotation = harry->Rotation();
			rotation.Yaw = harry->ViewRotation().Yaw;
			harry->SetRotation(rotation);
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

		void WandUp(UPlayerPawn* harry)
		{
			UActor* channel = SpineChannel(harry);
			if (channel && channel->AnimSequence() != NameString("wave"))
				KW::PlayAnim(channel, NameString("wave"), true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Replace, NameString());
		}

		bool WasInAir = false;

		// PlayerWalking.ProcessFalling (called by PlayerWalking's PlayerTick, before PlayerMove): fTimeInAir and fFallingZ,
		// which PlayLandedSound takes the volume from
		void ProcessFalling(UPlayerPawn* harry, float deltaTime)
		{
			float* timeInAir = Prop<float>(harry, "fTimeInAir");
			float* fallingZ = Prop<float>(harry, "fFallingZ");
			uint8_t* lastPhysics = Prop<uint8_t>(harry, "eLastPhysState");
			if (!timeInAir || !fallingZ || !lastPhysics)
				return;
			if (harry->Physics() == PHYS_Falling)
			{
				if (*lastPhysics != PHYS_Falling)
					*fallingZ = harry->Location().z;
				*timeInAir += deltaTime;
			}
			else
			{
				SetBoolProperty(harry, "bPlayedFallSound", false);
				*timeInAir = 0.0f;
			}
			*lastPhysics = harry->Physics();
		}

		// playeraiming's animation while the extra moves Harry
		void AimingAnims(UPlayerPawn* harry)
		{
			bool release = harry->AnimSequence() == NameString("cast");
			if (harry->Physics() != PHYS_Walking)
			{
				// Jumped or walked off an edge with the spell held: the wand stays up, the body plays DoJump's
				// jump2 / jump, then 'fall', as PlayerWalking's AnimEnd would (PlayInAir's loopanim, played here
				// AT_Combine so it keeps the wand)
				WasInAir = true;
				StopLegs(harry);
				if (release)
				{
					PutWandBack(harry);
					return;
				}
				WandUp(harry);
				NameString anim = harry->AnimSequence();
				bool jumping = (anim == NameString("jump") || anim == NameString("jump2")) && !harry->bAnimFinished();
				if (!jumping && anim != NameString("fall"))
					KW::PlayAnim(harry, NameString("fall"), true, 1.0f, 0.2f, 0.0f, EAnimType::AT_Combine, NameString());
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
				WandUp(harry);
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
			if (Active() && harry->Physics() == PHYS_Falling && NoSpecialMode(harry))
				harry->aStrafe() *= 0.5f; // PlayerWalking's 0.16 to 0.08
			CallOriginal(WalkingPlayerMove, self, args);
			AirTurn(harry, args[0].ToFloat());
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
			if (moving)
			{
				if (WasInAir && harry->Physics() == PHYS_Walking)
				{
					// Landed (PlayerPawn's; playeraiming has none) has no sound, PlayerWalking's plays this. Before
					// ProcessFalling clears fTimeInAir, as Landed comes before PlayerTick.
					WasInAir = false;
					CallEvent(harry, NameString("PlayLandedSound"));
				}
				ProcessFalling(harry, args[0].ToFloat());
				if (float* timeToStop = Prop<float>(harry, "fTimeToStop"))
					*timeToStop = 1.0f;
				if (harry->Physics() == PHYS_Falling)
					harry->aStrafe() *= 0.2f; // playeraiming's 0.4 to 0.08
			}
			CallOriginal(AimingPlayerMove, self, args);
			AirTurn(harry, args[0].ToFloat());
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
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			// Not during the release: jump2 would replace 'cast', which the state waits for (the jump is lost, as in the
			// original)
			if (MovingWhileAiming(harry) && BoolProperty(harry, "bPressedJump") && harry->AnimSequence() != NameString("cast"))
			{
				Frame::Call(HarryDoJump, self, {});
				SetBoolProperty(harry, "bPressedJump", false);
			}
		}

		void NAltFire(UObject* self, ExpressionValue* args)
		{
			// The original returns while Harry falls (and goes to playeraiming, which changes no physics, otherwise)
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			bool inAir = Active() && harry->Physics() == PHYS_Falling && NoSpecialMode(harry) &&
				harry->GetStateName() == NameString("PlayerWalking");
			if (inAir)
				harry->Physics() = PHYS_Walking;
			CallOriginal(HarryAltFire, self, args);
			if (inAir)
				harry->Physics() = PHYS_Falling;
		}

		bool InWalkingAnimEnd = false;

		bool PlainFeel(UPlayerPawn* harry)
		{
			return Active() && NoSpecialMode(harry);
		}

		bool NoAcceleration(UPlayerPawn* harry)
		{
			vec3 accel = harry->Acceleration();
			return accel.x == 0.0f && accel.y == 0.0f;
		}

		void NWalkingAnimEnd(UObject* self, ExpressionValue* args)
		{
			InWalkingAnimEnd = true;
			CallOriginal(WalkingAnimEnd, self, args);
			InWalkingAnimEnd = false;
		}

		// TweenToRunning / TweenToWaiting (TweenToWalking calls TweenToRunning) from PlayerWalking's AnimEnd
		void SlowBlend(UPlayerPawn* harry, ExpressionValue& tweenTime)
		{
			if (InWalkingAnimEnd && PlainFeel(harry))
				tweenTime.Store(ExpressionValue::FloatValue(0.4f));
		}

		void NTweenToWaiting(UObject* self, ExpressionValue* args)
		{
			SlowBlend(static_cast<UPlayerPawn*>(self), args[0]);
			CallOriginal(HarryTweenToWaiting, self, args);
		}

		void NTweenToRunning(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			SlowBlend(harry, args[0]);
			if (PlainFeel(harry) && NoAcceleration(harry))
				CallEvent(harry, NameString("TweenToWaiting"), { ExpressionValue::FloatValue(args[0].ToFloat()) });
			else
				CallOriginal(HarryTweenToRunning, self, args);
		}

		void NPlayRunning(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			// AdamJD's PlayRunning is TweenToRunning(0); the idle gets TweenToWaiting's own blend instead of a cut
			if (PlainFeel(harry) && NoAcceleration(harry))
				CallEvent(harry, NameString("TweenToWaiting"), { ExpressionValue::FloatValue(0.2f) });
			else
				CallOriginal(HarryPlayRunning, self, args);
		}

		UClass* HarryClass = nullptr;

		void NCastSpell(UObject* self, ExpressionValue* args)
		{
			UActor* target = UObject::TryCast<UActor>(args[0].ToObject());
			CallOriginal(WandCastSpell, self, args);
			UActor* wand = static_cast<UActor*>(self);
			UActor* caster = wand->Owner();
			if (!Active() || !caster || !HarryClass || !caster->IsA(HarryClass->Name) || !target || !target->IsA(NameString("Pawn")))
				return;
			vec3 d = wand->Location() - target->Location();
			if (dot(d, d) >= 128.0f * 128.0f)
				return;
			UActor* spell = UObject::TryCast<UActor>(ObjectProperty(wand, "LastCastedSpell"));
			if (spell && !spell->bDeleteMe())
				CallEvent(spell, NameString("ProcessTouch"), { ExpressionValue::ObjectValue(target), ExpressionValue::VectorValue(target->Location()) });
		}

		void NMount(UObject* self, ExpressionValue* args)
		{
			UPlayerPawn* harry = static_cast<UPlayerPawn*>(self);
			if (MovingWhileAiming(harry))
				return;
			// Mount's ground branch (bFallingMount off, Mounting); Mounting's BeginState sets PHYS_Projectile
			bool mantle = Active() && harry->Physics() == PHYS_Falling && NoSpecialMode(harry) &&
				args[0].ToVector().z < 80.0f;
			if (mantle)
				harry->Physics() = PHYS_Walking;
			CallOriginal(HarryMount, self, args);
			if (mantle && harry->Physics() == PHYS_Walking)
				harry->Physics() = PHYS_Falling;
		}

		void NDoJump(UObject* self, ExpressionValue* args)
		{
			if (!MovingWhileAiming(static_cast<UPlayerPawn*>(self)))
				CallOriginal(HarryDoJump, self, args);
		}

		UClass* WandClass = nullptr;
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
			HarryAltFire = WrapScriptFunction(cls, nullptr, "AltFire", &NAltFire);
			WalkingAnimEnd = WrapScriptFunction(cls, "PlayerWalking", "AnimEnd", &NWalkingAnimEnd);
			HarryTweenToRunning = WrapScriptFunction(cls, nullptr, "TweenToRunning", &NTweenToRunning);
			HarryPlayRunning = WrapScriptFunction(cls, nullptr, "PlayRunning", &NPlayRunning);
			HarryTweenToWaiting = WrapScriptFunction(cls, nullptr, "TweenToWaiting", &NTweenToWaiting);
		}
		UClass* wandClass = engine->packages->FindClass("HPBase.baseWand");
		if (wandClass && wandClass != WandClass)
		{
			WandClass = wandClass;
			WandCastSpell = WrapScriptFunction(wandClass, nullptr, "CastSpell", &NCastSpell);
		}

		// Out of playeraiming (cast, or left some other way: a cutscene, a hit): the wand comes down, and once 'cast'
		// is over the legs stop running on their own (normally the next animation, played AT_Replace, has done it).
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (player && HarryClass && player->IsA(HarryClass->Name) && !Aiming(player))
		{
			WasInAir = false;
			PutWandBack(player);
			if (player->AnimSequence() != NameString("cast"))
				StopLegs(player);
		}
	}
}
