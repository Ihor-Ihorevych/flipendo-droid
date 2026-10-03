#include "Precomp.h"
#include "HP1.h"
#include "VM/NativeFunc.h"

namespace HP1
{
	void RegisterAnimNatives();
	void RegisterTraceNatives();
	void RegisterGestureNatives();
	void RegisterParticleNatives();

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
	}
}
