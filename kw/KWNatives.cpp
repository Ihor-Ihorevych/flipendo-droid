#include "Precomp.h"
#include "KW.h"
#include "VM/NativeFunc.h"

namespace KW
{
	void RegisterAnimNatives();
	void RegisterTraceNatives();
	void RegisterGestureNatives();
	void RegisterParticleNatives();
	void RegisterWindNatives();
	void RegisterCollisionNatives();
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
		RegisterNavigationNatives();
		RegisterSoundNatives();
		RegisterAttachNatives();
		RegisterSaveNatives();
		RegisterPlayerNatives();
	}
}
