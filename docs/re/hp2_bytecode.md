# HP2 script bytecode

HP2's UnrealScript bytecode uses a slightly different token table from HP1 (and stock UE1, which HP1 matches).
Found in HP2's Core.dll (`../ida/hp2/Core.dll`) from the static registration stubs that call `GRegisterNative` /
set `GNativeDuplicate` for each `UObject::exec*` opcode handler. Code: `hp2/HP2Bytecode.cpp`.

## Token table differences

| Token | HP1 / stock | HP2 |
|---|---|---|
| 0x37 | DynArrayCount (HP1) | DynArrayCount |
| 0x38 | GlobalFunction | **DebugInfo** (new) |
| 0x39 | RotatorToVector | GlobalFunction |
| 0x3A-0x46 | ByteToInt..FloatToBool, then unused 0x46 | RotatorToVector..FloatToBool (each stock token + 1) |
| 0x47-0x5A | ObjectToBool..StringToName | same |

So an HP2 token in 0x39-0x46 is the stock token one lower; everything else is the same value.

## DebugInfo (0x38)

`UObject::execDebugInfo` [HP2 Core 0x10132FE0], after the 0x38 byte:

| Field | Size |
|---|---|
| version, must be 100 | int32 |
| line | int32 |
| position | int32 |
| opcode name | NUL-terminated ANSI string |

With any other version it moves Code back to the 0x38 byte and returns. With 100 it calls `GDebugger` (vtable slot 0)
when a script debugger is attached, otherwise nothing. The line numbers could feed SurrealDebugger later.

Every HP2 `exec*` native wrapper, after reading its parameters and EndFunctionParms, checks whether the next byte is
0x38 and runs DebugInfo if so (e.g. `execWaitForLanding` [HP2 0x103E44F0], `execSetPhysics` [HP2 0x103F1F40]). That
check is the ~29 extra bytes of most HP2 `exec*` functions listed as "changed" in `hp2_compare.md`; their behaviour
is otherwise HP1's.

## What SurrealEngine needs

Its bytecode reader uses the stock values (`ExprToken`, Packages/Core/UStruct.h). For HP2, `BytecodeStream`'s
ReadToken / PeekToken must skip DebugInfo records and map tokens with `HP2::ToStockToken` (not hooked yet).
