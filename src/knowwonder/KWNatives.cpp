#include "Precomp.h"
#include "KW.h"
#include "KWGame.h"
#include "HP2.h"
#include "VM/NativeFunc.h"
#include "VM/ExpressionValue.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UState.h"
#include "Packages/Core/UFunction.h"
#include "GC/GC.h"

namespace KW
{
	void RegisterAnimNatives();
	void RegisterTraceNatives();
	void RegisterGestureNatives();
	void RegisterParticleNatives();
	void RegisterWindNatives();
	void RegisterCollisionNatives();
	void RegisterMoveNatives();
	void RegisterNavigationNatives();
	void RegisterSoundNatives();
	void RegisterAttachNatives();
	void RegisterSaveNatives();
	void RegisterPlayerNatives();

	// Upstream already registered stubs for most HP1 natives, and RegisterHandler refuses to assign
	// an index twice, so clear the slot before registering ours.
	void OverrideNative(int index, void (*registerFunc)())
	{
		if ((size_t)index < NativeFunctions::NativeByIndex.size())
			NativeFunctions::NativeByIndex[index] = nullptr;
		registerFunc();
	}

	void RegisterNatives()
	{
		RegisterAnimNatives();
		RegisterTraceNatives();
		RegisterGestureNatives();
		RegisterParticleNatives();
		RegisterWindNatives();
		RegisterCollisionNatives();
		RegisterMoveNatives(); // after RegisterCollisionNatives: replaces its SetPhysics with HP1's whole setPhysics
		RegisterNavigationNatives();
		RegisterSoundNatives();
		RegisterAttachNatives();
		RegisterSaveNatives();
		RegisterPlayerNatives();

		// The natives above have HP1's script signatures, which HP2 shares for almost all of them. HP2 then adds its
		// own natives and overrides the few whose signature changed (docs/one-engine.md).
		if (IsHP2())
			HP2::RegisterNatives();
	}

	UFunction* WrapScriptFunction(UClass* cls, const char* stateName, const char* functionName, void (*handler)(UObject* self, ExpressionValue* args))
	{
		if (!cls)
			return nullptr;
		UState* state = stateName ? cls->GetState(NameString(stateName)) : nullptr;
		if (stateName && !state)
			return nullptr;
		std::map<NameString, UFunction*>& functions = state ? state->Functions : cls->Functions;
		auto it = functions.find(NameString(functionName));
		if (it == functions.end() || AllFlags(it->second->FuncFlags, FunctionFlags::Native))
			return nullptr;
		UFunction* original = it->second;

		// A native as Frame::CallNative runs it: found by name and state or class (NativeStruct), its frame one
		// ExpressionValue per property (what UFunction::Load sets up for natives loaded from a package)
		auto wrapper = GC::Alloc<UFunction>(original->Name, original->Class, original->Flags);
		wrapper->StructParent = original->StructParent; // Frame::GetName: Harry.PlayerWalking.PlayerMove in call stacks
		wrapper->BaseStruct = original->BaseStruct;
		wrapper->Line = original->Line;
		wrapper->Properties = original->Properties;
		wrapper->ParmsSize = original->ParmsSize;
		wrapper->NumParms = original->NumParms;
		wrapper->ReturnValueOffset = original->ReturnValueOffset;
		wrapper->FuncFlags = original->FuncFlags | FunctionFlags::Native;
		wrapper->NativeStruct = state ? static_cast<UStruct*>(state) : cls;
		wrapper->NativeFuncIndex = 0;
		wrapper->IsNativeFunc = true;
		wrapper->StructAlignment = alignof(ExpressionValue);
		wrapper->StructSize = wrapper->Properties.size() * sizeof(ExpressionValue);
		NativeFunctions::NativeByName[{ original->Name, wrapper->NativeStruct->Name }] = handler;
		it->second = wrapper;
		return original;
	}
}
