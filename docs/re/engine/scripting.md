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
check is the ~29 extra bytes of most HP2 `exec*` functions listed as "changed" in the HP1 vs HP2 code comparison; their behaviour
is otherwise HP1's.

### HP1 releases (not checked)

The US HP1 may be a patched build whose `.u` files don't load with the UK release's engine (changed bytecode
definitions), with tokens shifted like HP2's. Our disc already has the 1.1 script fixes and a stock token table, so
either the shift is in the US build only or there is none. Before calling a token table final, compare `HPBase.u` from a
US and a UK copy.

### How Flipendo reads it

Both of SurrealEngine's bytecode readers handle it, gated by `IsHarryPotter2()` (`0010-engine-fixes.patch`):

- `UStruct::ReadToken` (package load) maps each token with `HP2::ToStockToken` and copies a DebugInfo record into the
  in-memory bytecode unchanged, then reads the token after it in its place. The record stays, so jump and label
  offsets (in-memory offsets, counting the record) keep matching.
- `BytecodeStream::ReadToken` / `PeekToken` (building the expression tree) skip DebugInfo records and map the token.
  `Bytecode::ReadToken` keys an expression that follows a record under both offsets, the record's and its own, since a
  jump can land on either.
- HP2 also uses HP1's DynArrayCount at 0x37 (`DynArrayToInt_HP1`), so that case is open to both games.

## disable() and GotoState

In HP1's Core.dll `execDisable` (0x10141F30) only clears the bit in the state frame's ProbeMask, and
`UObject::GotoState` (0x10131BB0) rebuilds that mask on every call, also into the same state:
`(State.ProbeMask | Class.ProbeMask) & State.IgnoreMask`. So a `disable('Tick')` lasts until the next GotoState.
SurrealEngine kept disabled events per state name for good, which hung HP1's spell lesson
([spells.md](../hp1/spells.md#spell-lesson-flow)); fixed in `0010-engine-fixes.patch`. HP2 not checked.

A set bit is an enabled probe (`UObject::IsProbing`, Core 0x1015A2F0, tests it). Map actors are saved with the whole
mask set. SurrealEngine's `UObject::Load` read the saved bits as disabled, so a map actor ignored its probe functions
until its first GotoState. That matters during level start: SurrealEngine's VM also skips a script call to a disabled
probe function (`Frame::Call`), and `SetInitialState` runs actor by actor. Lev2_RemChase's `RemembrallReferee` enters
`GameIntro` from `SetInitialState` and triggers `Intro`; `CutScene0` (later in the list) dropped the `Trigger`, and its
`bDelayLevelFadeIn` left the screen black. Fixed in `0010-engine-fixes.patch` (only a clear bit disables). HP1's
`UObject::CallFunction` (Core 0x10142FE0) has no probe check at all; only `ProcessEvent` (engine-raised events) asks
`IsProbing`. SurrealEngine's check on script calls stays (not needed by anything found so far). `UObject::Save` wrote the
disabled probes too (nearly always none, a mask of 0); read the new way, every actor of a loaded save ignored its
probes until its first GotoState (the camera stopped following Harry after a reload). It now writes the enabled
ones, and Load takes a mask of 0 as nothing disabled, so saves from before still load.

## GotoState from inside EndState

`UObject::GotoState` (Core 0x10131BB0) takes the old state's name first. It calls EndState only when the new state
differs, `IsProbing('EndState')` and the object flag 0x2000 (in EndState) is clear; around that call it sets 0x2000 and
clears 0x1000 (state changed). If 0x1000 is set afterwards, a GotoState made inside EndState already switched state and
this call returns 2 (preempted) without touching the state frame. Then it sets the state, calls BeginState when the
name differs from the old one (0x1000 cleared before; set afterwards again means preempted), sets 0x1000 and
returns 1. `execGotoState` (0x10141C50) skips the call when the name equals the current state, and jumps to the label
only on 1. So an EndState that leads to another GotoState on the same object runs once. SurrealEngine called EndState
again for every nested GotoState and recursed until the stack overflowed: a player's Charms Class crash (stack
overflow in GotoState → EndState → script → GotoState, after levitating the first block) on a game whose scripts
differ from the retail disc's (Core.u and Engine.u SHA-1 `0c077b5c…` / `25945edf…`, `cHarryAnimChannel` with a
`stateCasting` state). Fixed in `0010-engine-fixes.patch`. HP2 not checked.

## Functions of an extended state

`state Beater extends Fly` inherits Fly's functions. HP1's `UObject::FindObjectField` (Core 0x1015A650, called by
`FindFunction` 0x1015A6C0) looks a name up in the current state's field hash, then the class's; UE1 builds a state's
hash with the fields of the state it extends. SurrealEngine's `UState::GetFunction` only looked in the state's own
functions, so an event like `BeginState` defined only in the super state never ran. In Lev3_Quid2 the beaters, goalies
and chasers (`QuidPlayer` states `Beater`, `Goalie`, `Chaser`, all `extends Fly`) never got `Fly.BeginState`'s
`LoopAnim('Fly')`: the beaters, whose state code only sleeps, flew in the mesh's bind pose (a T-pose). Fixed in
`0010-engine-fixes.patch` (`GetFunction` walks `BaseStruct` through the super states, any game). HP2 not checked.

## Latent calls on other actors

In HP1's Lev_Tut1, `SpellLearnTrigger` calls `Teacher.TurnToward(Cam)` on Quirrell, whose `Tut1Quirrell` `state idle {}` has no code.
`execTurnToward` (0x103D9130) sets the *target's* `StateFrame->LatentAction` (511), but UE1 only processes state
frames that have code, so a pawn in a code-less state never polls it and never turns. SurrealEngine resumed that
frame and ran off the end of the empty state. (HP1's Engine.dll; HP2 not checked.)

## Division by zero

`execDivide_IntInt` (Core.dll 0x10136FD0) returns 0 when the divisor is 0, and `execDivideEqual_ByteByte`
(0x10136710) returns 0 and leaves the variable alone. Scripts rely on it: `HPMenuOptionComboList` sizes its rows as
`(WinHeight-7)/Count`, so an options dropdown with no entries divides by zero. SurrealEngine used plain C++ division
there and crashed ("Integer divide by zero"). (HP1's Core.dll; HP2 not checked.)

## An out parameter through a None context

`Other.PlayerReplicationInfo.Score -= 1` with no PlayerReplicationInfo (HP2's `GameInfo.ScoreKill`, when a jelly bean
falls out of the world) is an Accessed None in UE1 and the native writes to a temporary. SurrealEngine handed the
native an empty value, which threw and logged a "Script error" with a callstack each time. `Frame::CallNative` now
passes a default-valued temporary for such a (non-optional) out parameter (`0010-engine-fixes.patch`, any game).

## switch on strings ignores case

`switch` matches its cases with the property's `Identical`, and `UStrProperty::Identical` compares with `appStricmp`
(checked in HP2's Core.dll [HP2 Core 0x10172510]; stock UE1 behaviour). HP2's `CutScript.ParseCommand` switches on the
first word of a cutscene line against `"CUE"`, `"WAITFOR"`, `"SLEEP"`, ... while the scripts write `waitfor` and `Cue`.
SurrealEngine compared strings exactly, so every such line was taken for an actor name ("Failed to find capture. Actor
not captured.") and PrivetDr's intro stayed black. `Frame::ProcessSwitch` now ignores case for strings
(`0010-engine-fixes.patch`, any game).

## Script recursion limit

`UObject::ProcessInternal` (HP1 Core 0x101431A0) counts nested script calls; past 250 it logs "Infinite script
recursion (250 calls) detected" as critical, and `FFrame::Serialize` (0x10131A10) sends a critical message to
`GError`, so the game stops with that error. Singular functions (FUNC_Singular) are skipped while they already run:
before the body, ProcessInternal tests the object's flags against the function's FUNC_Singular bit (0x20) and returns at
once if it is set, else sets it on the object for the call and clears it afterwards. So the flag is per object, not per
function: while any singular function runs on an object, every singular call on that object is skipped (any game; events
come through ProcessEvent into the same ProcessInternal). The skipped call's result is left as it was. SurrealEngine never
skipped them; `Frame::Call` now keeps the running objects in a side table (not in `UObject::Flags`, which SurrealEngine
loads unmasked from package files and writes into saves) and returns the return type's default for a skipped call
(`0010-engine-fixes.patch`). HP2 needs it: `Projectile.Touch` is singular, and the Aragog fight's web spell touches a web
that its own Touch moves onto it (seen in an Android fork's playtest, 2026-10-24).
HP2's `harry.Summon(ClassName)` calls itself (not `Super.Summon`), so the `summon` cheat ends the game this way in
the original too.


## Members of temporary struct expressions

A member read from a temporary vector/rotator/struct must own a copy of the field before
the parent expression is destroyed. ExpressionValue::Member previously returned a reference
into that temporary. HP2 cutscene rotations consequently read overwritten yaw values.
Member now loads the field when the parent is a value; fields of variables retain their
reference semantics so assignments still update the original struct. Regression tests
replace the parent after reading a member and check both value ownership and write-through.


## Empty class contexts

HP2 Core.dll execClassContext (0x10132C00) reports an empty class context, skips its
expression and returns zero/default data, as an empty object context does. Both VM
evaluators previously threw an exception. Harry.StopBossEncounter calls ChooseSpell(0),
whose SetCurrentSpell reads default.SpellType from None; the exception interrupted the
end of the Skurge Peeves fight. Both evaluators now return AccessedNone and NothingValue.
The regression test supplies an invalid inner expression to check that None skips it.
