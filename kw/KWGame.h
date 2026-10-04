#pragma once

#include "Engine.h"

// Which KnowWonder game is running, for the few places where shared kw/ code has to differ between HP1 and HP2.
// Use it at the exact line that differs (with both IDA addresses in the tag), never to fork a whole function:
// see hp2/README.md for the rules.

namespace KW
{
	inline bool IsHP1() { return engine->LaunchInfo.IsHarryPotter1(); }
	inline bool IsHP2() { return engine->LaunchInfo.IsHarryPotter2(); }
}
