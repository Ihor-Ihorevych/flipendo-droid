
#include "Precomp.h"
#include "GameApp.h"
#include <android/log.h>
#include <SDL3/SDL_hints.h>
#include <stdlib.h>
#include <atomic>
#include <jni.h>
#include <fstream>
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL_system.h>

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

// ---- Logs the user can send us -------------------------------------------------------------------------------
// flipendo.log (the engine log) is kept for the previous run too; a fatal signal writes its backtrace to crash.txt
// (frames as module+offset: symbolicate with the libmain.so that android/build-apk.sh keeps in android/dist/symbols/);
// when the game stops, the Java side hears about it and offers to send a report (LogReport.java).

static int g_crashFd = -1;

struct Backtrace
{
	void* frames[48];
	int count = 0;
};

static _Unwind_Reason_Code CollectFrame(_Unwind_Context* context, void* arg)
{
	Backtrace* bt = static_cast<Backtrace*>(arg);
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc != 0 && bt->count < 48)
		bt->frames[bt->count++] = reinterpret_cast<void*>(pc);
	return bt->count < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void WriteCrashText(const char* text)
{
	if (g_crashFd >= 0)
		(void)!write(g_crashFd, text, strlen(text));
}

static void CrashHandler(int sig, siginfo_t* info, void*)
{
	char line[512];
	snprintf(line, sizeof(line), "signal %d (%s), code %d, fault addr %p, tid %d\n", sig, strsignal(sig), info ? info->si_code : 0, info ? info->si_addr : nullptr, (int)gettid());
	WriteCrashText(line);

	Backtrace bt;
	_Unwind_Backtrace(CollectFrame, &bt);
	for (int i = 0; i < bt.count; i++)
	{
		Dl_info dl = {};
		if (dladdr(bt.frames[i], &dl) && dl.dli_fname)
		{
			uintptr_t offset = reinterpret_cast<uintptr_t>(bt.frames[i]) - reinterpret_cast<uintptr_t>(dl.dli_fbase);
			snprintf(line, sizeof(line), "#%02d pc %016llx %s (%s)\n", i, (unsigned long long)offset, dl.dli_fname, dl.dli_sname ? dl.dli_sname : "?");
		}
		else
		{
			snprintf(line, sizeof(line), "#%02d pc %p\n", i, bt.frames[i]);
		}
		WriteCrashText(line);
	}
	// SA_RESETHAND restored the default action: returning re-runs the faulting instruction and ends the process
	// (so Android still records its own tombstone/exit reason).
}

static void InstallCrashHandler()
{
	rename("/sdcard/FlipendoHP/crash.txt", "/sdcard/FlipendoHP/crash.prev.txt");
	g_crashFd = open("/sdcard/FlipendoHP/crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

	struct sigaction action = {};
	action.sa_sigaction = CrashHandler;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&action.sa_mask);
	for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSYS })
		sigaction(sig, &action, nullptr);
}

// Tell FlipendoActivity the game thread is over (code 0: the player quit), so it can show the way to the logs.
static void NotifyGameStopped(int code)
{
	JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity)
		return;
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(cls, "onGameStopped", "(I)V");
	if (method)
		env->CallVoidMethod(activity, method, (jint)code);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv)
{
	int code = 1;
	try
	{
		setenv("HOME", "/sdcard/FlipendoHP", 1);
		rename("/sdcard/FlipendoHP/flipendo.log", "/sdcard/FlipendoHP/flipendo.prev.log");
		InstallCrashHandler();

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
		code = app.main(std::move(args));
	}
	catch (const std::exception& e)
	{
		__android_log_print(ANDROID_LOG_ERROR, "flipendo", "%s", e.what());
		std::ofstream log("/sdcard/FlipendoHP/flipendo.log", std::ios::app);
		log << "FATAL: " << e.what() << std::endl;
		code = 1;
	}
	NotifyGameStopped(code);
	return code;
}
