#include "Precomp.h"
#include "HP2.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Utils/StrTools.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// HP2's console commands that the scripts send through ConsoleCommand and that the engine's own Exec answers
// (UViewport / audio subsystem). NOT reversed: the effect of each is taken from the hgame scripts that send it.
//   MUTESOUNDS / UNMUTESOUNDS  HPConsole.HandleFastForward, around a cutscene skip. Not muted yet: the sounds of a
//                              fast-forwarded cutscene still play.
//   SETVOLUMES SOUND=x MUSIC=y FESoundVideoPage sliders, x and y in 0..1.
//   MLOD x                     FESoundVideoPage "object detail" (mesh LOD bias). No LOD change here.
//   ISFULLSCREEN               FESoundVideoPage.LoadAvailableSettings: asks whether the window is fullscreen.
namespace HP2
{
	static const char* const AudioSection = "ALAudio.ALAudioSubsystem";

	static uint8_t ToByte(float volume)
	{
		return (uint8_t)std::clamp((int)(volume * 255.0f + 0.5f), 0, 255);
	}

	// HP2 keeps its volumes in [ALAudio.ALAudioSubsystem] as floats (MusicVolume=0.53, SoundVolume=0.9); the stock
	// audio device reads them as bytes (0..255) from another section, and a "0.9" reads as 0.
	void LoadAudioVolumes()
	{
		if (!engine->audiodev || !engine->packages)
			return;
		const std::string music = engine->packages->GetIniValue("System", AudioSection, "MusicVolume", "");
		const std::string sound = engine->packages->GetIniValue("System", AudioSection, "SoundVolume", "");
		if (!music.empty())
			engine->audiodev->SetMusicVolume(ToByte((float)atof(music.c_str())));
		if (!sound.empty())
			engine->audiodev->SetSoundVolume(ToByte((float)atof(sound.c_str())));

	}

	bool ConsoleCommand(const std::string& commandline, std::string& result)
	{
		std::istringstream words(commandline);
		std::string name;
		words >> name;
		if (name.empty())
			return false;

		if (StrTools::equals_ignore_case(name, "mutesounds") || StrTools::equals_ignore_case(name, "unmutesounds")
			|| StrTools::equals_ignore_case(name, "mlod"))
		{
			result.clear();
			return true;
		}
		if (StrTools::equals_ignore_case(name, "isfullscreen"))
		{
			result = "True";
			return true;
		}
		// FESoundVideoPage reads and writes the volumes as "get ini:Engine.Engine.AudioDevice SoundVolume" and "set ini:... SoundVolume 0.9",
		// fractions 0..1. The stock get/set treat the device's byte properties: the page read 255, showed 25500 and wrote back 1.0,
		// which the byte property took as 1 of 255: every sound started after that was silent.
		if (StrTools::equals_ignore_case(name, "get") || StrTools::equals_ignore_case(name, "set"))
		{
			std::string target, key, value;
			words >> target >> key >> value;
			std::string lowerTarget = target;
			std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			const bool music = StrTools::equals_ignore_case(key, "MusicVolume");
			const bool sound = StrTools::equals_ignore_case(key, "SoundVolume");
			if (lowerTarget.find("audiodevice") == std::string::npos || (!music && !sound) || !engine->audiodev)
				return false;
			uint8_t volume = music ? engine->audiodev->GetMusicVolume() : engine->audiodev->GetSoundVolume();
			if (StrTools::equals_ignore_case(name, "get"))
			{
				result = std::to_string(volume / 255.0f);
				return true;
			}
			if (!value.empty())
			{
				volume = ToByte((float)atof(value.c_str()));
				if (music) engine->audiodev->SetMusicVolume(volume); else engine->audiodev->SetSoundVolume(volume);
				engine->packages->SetIniValue("System", AudioSection, music ? "MusicVolume" : "SoundVolume", value);
			}
			result.clear();
			return true;
		}
		if (StrTools::equals_ignore_case(name, "setvolumes"))
		{
			std::string word;
			while (words >> word)
			{
				size_t eq = word.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = word.substr(0, eq);
				std::string value = word.substr(eq + 1);
				bool music = StrTools::equals_ignore_case(key, "music");
				bool sound = StrTools::equals_ignore_case(key, "sound");
				if (!music && !sound)
					continue;
				uint8_t volume = ToByte((float)atof(value.c_str()));
				if (engine->audiodev)
					{ if (music) engine->audiodev->SetMusicVolume(volume); else engine->audiodev->SetSoundVolume(volume); }
				engine->packages->SetIniValue("System", AudioSection, music ? "MusicVolume" : "SoundVolume", value);
			}
			result.clear();
			return true;
		}
		return false;
	}
}

namespace HP2
{
	// HP2 walks with A/D as strafe and turns with the arrow keys (RotLeft/RotRight). The touch overlay and a gamepad
	// press the arrows for left and right, so on the phone they strafe and the camera is turned by dragging the screen.
	void TweakKeybindings()
	{
#ifdef __ANDROID__
		auto replace = [](std::string& text, const std::string& from, const std::string& to)
		{
			size_t pos = text.find(from);
			if (pos != std::string::npos)
				text.replace(pos, from.size(), to);
		};
		for (const char* key : { "Left", "Right" })
		{
			auto it = engine->keybindings.find(key);
			if (it == engine->keybindings.end())
				continue;
			replace(it->second, "RotLeft", "StrafeLeft");
			replace(it->second, "RotRight", "StrafeRight");
		}
#endif
	}
}
