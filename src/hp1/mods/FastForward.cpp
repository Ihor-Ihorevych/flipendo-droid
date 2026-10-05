#include "Precomp.h"
#include "HP1Mods.h"
#include "GameWindow.h"
#include "Engine.h"

// Holding Shift runs the game 2.5 times as fast. Shift is bound to nothing in HP1 (DefUser.ini: "Shift=", "LShift=",
// "RShift="), so it can't clash with the controls. Only the game's time is scaled (the level tick and the console's
// Tick), not LevelInfo.TimeDilation: that one is saved in save games and set by scripts, and an autosave while Shift is
// held would load into a game stuck at the faster speed.
namespace HP1::Mods
{
	static const float FastForwardSpeed = 2.5f;

	float FastForwardTimeScale()
	{
		if (!Enabled() || !engine->window)
			return 1.0f;
		return engine->window->GetKeyState(IK_Shift) ? FastForwardSpeed : 1.0f;
	}
}
