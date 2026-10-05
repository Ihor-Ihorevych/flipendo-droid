# UnrealScript VM

Where KnowWonder's script VM (Core.dll) behaves differently from what SurrealEngine assumed, and how the two games'
bytecode differs. Both games' gameplay is pure UnrealScript, so a VM difference shows up as a script state that hangs
or never runs.

## Bytecode: HP1 vs HP2

HP1's token table is stock UE1's. HP2's differs slightly: found in HP2's Core.dll (`../ida/hp2/Core.dll`) from the
static registration stubs that call `GRegisterNative` / set `GNativeDuplicate` for each `UObject::exec*` opcode
handler. Code: `src/hp2/HP2Bytecode.cpp`.

### Token table differences

| Token | HP1 / stock | HP2 |
|---|---|---|
| 0x37 | DynArrayCount (HP1) | DynArrayCount |
| 0x38 | GlobalFunction | **DebugInfo** (new) |
| 0x39 | RotatorToVector | GlobalFunction |
| 0x3A-0x46 | ByteToInt..FloatToBool, then unused 0x46 | RotatorToVector..FloatToBool (each stock token + 1) |
| 0x47-0x5A | ObjectToBool..StringToName | same |

So an HP2 token in 0x39-0x46 is the stock token one lower; everything else is the same value.

### DebugInfo (0x38)

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
check is the ~29 extra bytes of most HP2 `exec*` functions listed as "changed" in [hp2_compare.md](../reports/hp2_compare.md); their behaviour
is otherwise HP1's.

### HP1 releases (not checked)

The US HP1 may be a patched build whose `.u` files don't load with the UK release's engine (changed bytecode
definitions), with tokens shifted like HP2's. Our disc already has the 1.1 script fixes and a stock token table, so
either the shift is in the US build only or there is none. Before calling a token table final, compare `HPBase.u` from a
US and a UK copy.

### What SurrealEngine needs

Its bytecode reader uses the stock values (`ExprToken`, Packages/Core/UStruct.h). For HP2, `BytecodeStream`'s
ReadToken / PeekToken must skip DebugInfo records and map tokens with `HP2::ToStockToken` (not hooked yet).

## disable() and GotoState

In HP1's Core.dll `execDisable` (0x10141F30) only clears the bit in the state frame's ProbeMask, and
`UObject::GotoState` (0x10131BB0) rebuilds that mask on every call, also into the same state:
`(State.ProbeMask | Class.ProbeMask) & State.IgnoreMask`. So a `disable('Tick')` lasts until the next GotoState.
SurrealEngine kept disabled events per state name for good, which hung HP1's spell lesson
([spells.md](../hp1/spells.md#spell-lesson-flow)); fixed in `0010-engine-fixes.patch`. HP2 not checked.

## Latent calls on other actors

In HP1's Lev_Tut1, `SpellLearnTrigger` calls `Teacher.TurnToward(Cam)` on Quirrell, whose `Tut1Quirrell` `state idle {}` has no code.
`execTurnToward` (0x103D9130) sets the *target's* `StateFrame->LatentAction` (511), but UE1 only processes state
frames that have code, so a pawn in a code-less state never polls it and never turns. SurrealEngine resumed that
frame and ran off the end of the empty state. (HP1's Engine.dll; HP2 not checked.)
