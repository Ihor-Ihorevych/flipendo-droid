#include "Precomp.h"
#include "KW.h"
#include "KWMove.h"
#include "KWPhysics.h"
#include "VM/NativeFunc.h"
#include "Engine.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

// The natives that move or trace actors, on KnowWonder's own movement and collision (KWMove.h, KWCheck.h), so script
// and physics agree on what blocks what. Registered when KnowWonder's physics runs (KW::UseKWPhysics).

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// IDA Engine.dll: ?execMove@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C120]
	static void NMove(UObject* Self, const vec3& Delta, BitfieldBool& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		CheckResult hit;
		ReturnValue = MoveActor(actor, Delta, actor->Rotation(), hit);
	}

	// IDA Engine.dll: ?execMoveSmooth@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x103E4A40]
	static void NMoveSmooth(UObject* Self, const vec3& Delta, BitfieldBool& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		actor->bJustTeleported() = false;
		ReturnValue = MoveSmooth(actor, Delta);
	}

	// IDA Engine.dll: ?execSetLocation@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C210]
	static void NSetLocation(UObject* Self, const vec3& NewLocation, BitfieldBool& ReturnValue)
	{
		ReturnValue = FarMoveActor(UObject::Cast<UActor>(Self), NewLocation, false, false);
	}

	// IDA Engine.dll: ?execSetRotation@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C2A0]
	static void NSetRotation(UObject* Self, const Rotator& NewRotation, BitfieldBool& ReturnValue)
	{
		CheckResult hit;
		ReturnValue = MoveActor(UObject::Cast<UActor>(Self), vec3(0.0f), NewRotation, hit);
	}

	// Trace(HitLocation, HitNormal, End, optional Start = Location, optional bTraceActors = bCollideActors, optional
	// Extent): the first blocking/mover/level/other hit (just level and movers without actors). Nothing hit: None, and
	// a zero location and normal.
	// IDA Engine.dll: ?execTrace@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C670]
	static void NTrace(UObject* Self, vec3& HitLocation, vec3& HitNormal, const vec3& TraceEnd, std::optional<vec3> TraceStart, std::optional<bool> bTraceActors, std::optional<vec3> Extent, UObject*& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		bool traceActors = bTraceActors.value_or(actor->bCollideActors());
		CheckResult hit;
		hit.Location = vec3(0.0f);
		hit.Normal = vec3(0.0f);
		SingleLineCheck(hit, actor, TraceEnd, TraceStart.value_or(actor->Location()), traceActors ? 23 : 6, Extent.value_or(vec3(0.0f)));
		ReturnValue = hit.Actor;
		HitLocation = hit.Location;
		HitNormal = hit.Normal;
	}

	// IDA Engine.dll: ?execFastTrace@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040C930]
	static void NFastTrace(UObject* Self, const vec3& TraceEnd, std::optional<vec3> TraceStart, BitfieldBool& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		ReturnValue = ModelFastLineCheck(engine->Level->Model, TraceEnd, TraceStart.value_or(actor->Location()));
	}

	// Acceleration rounded to 0.1, then performPhysics.
	// IDA Engine.dll: ?execAutonomousPhysics@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x103E4B40]
	static void NAutonomousPhysics(UObject* Self, float DeltaSeconds)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		vec3& a = actor->Acceleration();
		a = vec3((float)(int)(a.x * 10.0f) * 0.1f, (float)(int)(a.y * 10.0f) * 0.1f, (float)(int)(a.z * 10.0f) * 0.1f);
		if (actor->Physics() != PHYS_None)
			PerformPhysics(actor, DeltaSeconds);
	}

	// IDA Engine.dll: ?execSetBase@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040AB20]
	static void NSetBase(UObject* Self, UObject* NewBase)
	{
		UObject::Cast<UActor>(Self)->SetBase(UObject::Cast<UActor>(NewBase), true);
	}

	// IDA Engine.dll: ?IsOverlapping@AActor@@QBEHPBV1@@Z [HP1 0x10379D90]
	static void NIsOverlapping(UObject* Self, UObject* checkActor, BitfieldBool& ReturnValue)
	{
		UActor* other = UObject::Cast<UActor>(checkActor);
		ReturnValue = other && IsOverlapping(UObject::Cast<UActor>(Self), other);
	}

	// IDA Engine.dll: ?execSetPhysics@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x103E4AD0]
	static void NSetPhysics(UObject* Self, uint8_t newPhysics)
	{
		SetPhysics(UObject::Cast<UActor>(Self), newPhysics, nullptr);
	}

	void RegisterMoveNatives()
	{
		if (!UseKWPhysics())
			return;
		OverrideNative(266, [] { RegisterVMNativeFunc_2("Actor", "Move", &NMove, 266); });
		OverrideNative(3969, [] { RegisterVMNativeFunc_2("Actor", "MoveSmooth", &NMoveSmooth, 3969); });
		OverrideNative(267, [] { RegisterVMNativeFunc_2("Actor", "SetLocation", &NSetLocation, 267); });
		OverrideNative(299, [] { RegisterVMNativeFunc_2("Actor", "SetRotation", &NSetRotation, 299); });
		OverrideNative(277, [] { RegisterVMNativeFunc_7("Actor", "Trace", &NTrace, 277); });
		OverrideNative(548, [] { RegisterVMNativeFunc_3("Actor", "FastTrace", &NFastTrace, 548); });
		OverrideNative(3971, [] { RegisterVMNativeFunc_1("Actor", "AutonomousPhysics", &NAutonomousPhysics, 3971); });
		OverrideNative(298, [] { RegisterVMNativeFunc_1("Actor", "SetBase", &NSetBase, 298); });
		OverrideNative(718, [] { RegisterVMNativeFunc_2("Actor", "IsOverlapping", &NIsOverlapping, 718); });
		OverrideNative(3970, [] { RegisterVMNativeFunc_1("Actor", "SetPhysics", &NSetPhysics, 3970); });
	}
}
