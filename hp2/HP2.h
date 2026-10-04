#pragma once

#include <cstddef>
#include <cstdint>

// Harry Potter 2 only: what HP2's engine has that HP1's doesn't (hp2/README.md). The shared KnowWonder engine code is
// in kw/KW.h, HP1-only code in hp1/HP1.h. Nothing here runs yet: IsKnowWonder() is still HP1 only (GameFolder.h).

namespace HP2
{
	// Called at the end of KW::RegisterNatives() when HP2 runs: HP2-only natives, and the natives whose script
	// signature changed in HP2 (they override kw/'s HP1-signature registration at the same index).
	void RegisterNatives();

	// HP2's script bytecode (hp2/HP2Bytecode.cpp, docs/re/hp2_bytecode.md). HP2 inserted a DebugInfo token at 0x38
	// and moved GlobalFunction..FloatToBool up by one. ToStockToken maps an HP2 token byte to the stock UE1 value
	// SurrealEngine's ExprToken uses.
	constexpr uint8_t DebugInfoToken = 0x38;
	uint8_t ToStockToken(uint8_t hp2Token);
	// Size in bytes of the DebugInfo record starting at `data` (the 0x38 byte), or 0 if it isn't one.
	size_t DebugInfoSize(const uint8_t* data, size_t available);
}
