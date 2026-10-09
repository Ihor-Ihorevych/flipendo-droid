
#include "Precomp.h"
#include "GameApp.h"
#include <android/log.h>
#include <SDL3/SDL_hints.h>
#include <stdlib.h>
#include <atomic>
#include <jni.h>
#include <fstream>

std::atomic<bool> g_androidMenuActive{false};
std::atomic<bool> g_lessonDrawing{false};
std::atomic<bool> g_touchDown{false};
std::atomic<float> g_touchX{0.5f}, g_touchY{0.5f};

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeLessonDrawing(JNIEnv*, jclass)
{
	return g_lessonDrawing.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetTouch(JNIEnv*, jclass, jfloat x, jfloat y, jboolean down)
{
	g_touchX = x;
	g_touchY = y;
	g_touchDown = down == JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeMenuActive(JNIEnv*, jclass)
{
	return g_androidMenuActive.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv)
{
	try
	{
		setenv("HOME", "/sdcard/FlipendoHP", 1);
		Array<std::string> args;
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
		SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0"); // no soft keyboard: all input comes from the touch overlay
		args.push_back("--autolaunch");
		args.push_back("--skip-splash");
		args.push_back("--skip-intro");
		// Default: the main menu. For development, a file with a map name starts that level instead
		// (adb shell "echo Lev2_Inc_A > /sdcard/FlipendoHP/start_level.txt"; delete the file to get the menu back).
		{
			std::ifstream startLevel("/sdcard/FlipendoHP/start_level.txt");
			std::string level;
			if (startLevel && std::getline(startLevel, level))
			{
				while (!level.empty() && (level.back() == 0x0d || level.back() == 0x20)) level.pop_back();
				if (!level.empty())
					args.push_back("--level=" + level);
			}
		}
		args.push_back("--logfile=/sdcard/FlipendoHP/flipendo.log");
		args.push_back("/sdcard/FlipendoHP");
		GameApp app;
		return app.main(std::move(args));
	}
	catch (const std::exception& e)
	{
		__android_log_print(ANDROID_LOG_ERROR, "flipendo", "%s", e.what());
		return 1;
	}
}
