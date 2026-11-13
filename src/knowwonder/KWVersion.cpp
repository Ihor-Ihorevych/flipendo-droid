#include "Precomp.h"
#include "KW.h"

// The version is set by CMake on this file only, so a new version rebuilds one file.
#ifndef FLIPENDO_VERSION_STRING
#define FLIPENDO_VERSION_STRING "dev"
#endif

namespace KW
{
	std::string FlipendoVersion()
	{
		return FLIPENDO_VERSION_STRING;
	}
}
