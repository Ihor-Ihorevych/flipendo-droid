#include "Precomp.h"
#include "KW.h"
#include "KWGame.h"
#include "HP2.h"
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

		// The natives above have HP1's script signatures, which HP2 shares for almost all of them. HP2 then adds its
		// own natives and overrides the few whose signature changed (hp2/README.md).
		if (IsHP2())
			HP2::RegisterNatives();
	}
}
