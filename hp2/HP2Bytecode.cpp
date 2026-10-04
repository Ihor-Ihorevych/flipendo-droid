#include "Precomp.h"
#include "HP2.h"
#include <cstring>

// HP2's script bytecode differs from HP1's (and stock UE1's) in the token table only (docs/re/hp2_bytecode.md).
// HP2's Core.dll registers its opcodes with GRegisterNative; compared with HP1:
//   0x38       DebugInfo (new)            stock 0x38 GlobalFunction
//   0x39       GlobalFunction             stock 0x39 RotatorToVector
//   0x3A-0x46  RotatorToVector..FloatToBool, each one higher than stock (0x39-0x45)
//   0x47-0x5A  same as stock (the shift ends in stock's unused 0x46)
// Everything else, including HP1's DynArrayCount at 0x37 and StringToName at 0x5A, is the same.
//
// Planned hook (not applied yet): BytecodeStream::ReadToken/PeekToken in engine/SurrealEngine/VM/Bytecode.h, gated by
// IsHarryPotter2(), skip DebugInfoSize() bytes while the next byte is a DebugInfo record and return
// ToStockToken(byte). The rest of SurrealEngine's bytecode reader then works unchanged for both games.

namespace HP2
{
	// IDA Core.dll: ?GRegisterNative@@YAEHABQ8UObject@@AEXAAUFFrame@@QAX@Z@Z [HP2 Core 0x10144550]
	// not exported: the static registration stubs that set GNativeDuplicate (sub_101331E0 for DebugInfo = 0x38,
	// sub_10134570 for GlobalFunction = 0x39 [HP2 Core]); find them as the data xrefs of intUObjectexec<Name>.
	uint8_t ToStockToken(uint8_t hp2Token)
	{
		if (hp2Token >= 0x39 && hp2Token <= 0x46)
			return hp2Token - 1;
		return hp2Token;
	}

	// IDA Core.dll: ?execDebugInfo@UObject@@QAEXAAUFFrame@@QAX@Z [HP2 Core 0x10132FE0]
	// After the 0x38 byte: int32 version, which must be 100 (otherwise execDebugInfo rewinds Code to the 0x38 byte and
	// does nothing), int32 line, int32 position, then a NUL-terminated ANSI string (the opcode name). The original
	// hands it to GDebugger (vtable slot 0) if a debugger is attached; the game ignores it. Native wrappers check for
	// a DebugInfo record right after EndFunctionParms, so it can follow any native call, not only statements. (That
	// check is the "+29 bytes" in most HP2 exec* functions in docs/re/hp2_compare.md, not a behaviour change.)
	size_t DebugInfoSize(const uint8_t* data, size_t available)
	{
		if (available < 13 || data[0] != DebugInfoToken)
			return 0;
		int32_t version;
		memcpy(&version, data + 1, sizeof(version));
		if (version != 100)
			return 0;
		const uint8_t* text = data + 13;
		const void* end = memchr(text, 0, available - 13);
		if (!end)
			return 0;
		return (const uint8_t*)end - data + 1;
	}
}
