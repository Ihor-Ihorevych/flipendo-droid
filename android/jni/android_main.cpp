#include "Precomp.h"
#include "GameApp.h"
#include <android/log.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_events.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <stdlib.h>
#include <atomic>
#include <mutex>
#include <jni.h>
#include <fstream>
#include <dirent.h>
#include <string>
#include <vector>
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL_system.h>

std::atomic<bool> g_androidMenuActive{false};
std::atomic<bool> g_lessonDrawing{false};
std::atomic<bool> g_cutsceneActive{false}; // a cutscene holds Harry (HP1::TickLessonTouch): the overlay hides the gameplay controls
std::atomic<int> g_frameCount{0}; // frames drawn, for the FPS counter (HP1::TickTouchDefaults counts, the overlay reads it)
std::atomic<float> g_uiScale{1.0f}; // subtitles and HUD size from the menu slider (HP1Canvas.cpp)
std::atomic<float> g_fovOffset{0.0f}; // degrees added to the field of view (settings panel; KW::ViewFovAngle)
std::atomic<bool> g_debugMode{false}; // HP's debug mode (settings panel; HP1::TickTouchDefaults)
std::atomic<bool> g_autoJump{true}; // HP's Auto Jump option (settings panel; HP1::TickTouchDefaults)
std::atomic<bool> g_saveRequested{false}; // the SAVE button: the game thread saves at its next tick (HP1::TickSaveRequest)

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetUiScale(JNIEnv*, jclass, jfloat scale)
{
	g_uiScale = scale;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetFovOffset(JNIEnv*, jclass, jfloat degrees)
{
	g_fovOffset = degrees;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetAutoJump(JNIEnv*, jclass, jboolean on)
{
	g_autoJump = on == JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetDebugMode(JNIEnv*, jclass, jboolean on)
{
	g_debugMode = on == JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeRequestSave(JNIEnv*, jclass)
{
	g_saveRequested = true;
}
std::atomic<bool> g_touchDown{false};
std::atomic<float> g_touchX{0.5f}, g_touchY{0.5f};

// Gamepad support for the game's menus (HP1::TickMenuItems): the game thread lists the menu items it can see, the overlay
// moves the cursor between them; a slider is changed on the game thread at the overlay's request.
static std::mutex g_menuItemsMutex;
static std::vector<float> g_menuItems; // per item: left, top, right, bottom (0..1 of the viewport), kind (0 button, 1 slider)
std::atomic<int> g_menuAdjustIndex{-1}, g_menuAdjustDir{0};
std::atomic<float> g_lessonTimeScale{1.0f}; // spell lessons give this much more drawing time (with a gamepad)

void AndroidSetMenuItems(std::vector<float>&& items)
{
	std::lock_guard<std::mutex> lock(g_menuItemsMutex);
	g_menuItems = std::move(items);
}

extern "C" JNIEXPORT jfloatArray JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeMenuItems(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(g_menuItemsMutex);
	jfloatArray out = env->NewFloatArray((jsize)g_menuItems.size());
	if (out && !g_menuItems.empty())
		env->SetFloatArrayRegion(out, 0, (jsize)g_menuItems.size(), g_menuItems.data());
	return out;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeMenuAdjust(JNIEnv*, jclass, jint index, jint dir)
{
	g_menuAdjustDir = dir;
	g_menuAdjustIndex = index;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeSetLessonTimeScale(JNIEnv*, jclass, jfloat scale)
{
	g_lessonTimeScale = scale;
}

static std::mutex g_levelMutex;
static std::string g_levelName; // the map being played (HP1::TickLessonTouch), for the overlay's prompts

void AndroidSetLevelName(const std::string& name)
{
	std::lock_guard<std::mutex> lock(g_levelMutex);
	g_levelName = name;
}

extern "C" JNIEXPORT jstring JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeLevelName(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(g_levelMutex);
	return env->NewStringUTF(g_levelName.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeFrameCount(JNIEnv*, jclass)
{
	return g_frameCount.load();
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_flipendo_spike_FlipendoActivity_nativeCutsceneActive(JNIEnv*, jclass)
{
	return g_cutsceneActive.load() ? JNI_TRUE : JNI_FALSE;
}

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

static void WriteFrame(const char* label, uintptr_t pc)
{
	char line[512];
	Dl_info dl = {};
	if (pc != 0 && dladdr(reinterpret_cast<void*>(pc), &dl) && dl.dli_fname)
	{
		uintptr_t offset = pc - reinterpret_cast<uintptr_t>(dl.dli_fbase);
		snprintf(line, sizeof(line), "%s pc %016llx %s (%s)\n", label, (unsigned long long)offset, dl.dli_fname, dl.dli_sname ? dl.dli_sname : "?");
	}
	else
	{
		snprintf(line, sizeof(line), "%s pc %p\n", label, reinterpret_cast<void*>(pc));
	}
	WriteCrashText(line);
}

static std::atomic<int> g_crashEntries{ 0 };

static void ResetCrashSignals()
{
	struct sigaction action = {};
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSYS })
		sigaction(sig, &action, nullptr);
}

static void CrashHandler(int sig, siginfo_t* info, void* context)
{
	// Once only. ART's signal chain ignores SA_RESETHAND for our handler: returning re-runs the faulting instruction and
	// lands here again, forever (a Redmi Note 9 Pro wrote a 1 GB crash.txt that way). A second entry ends the process.
	if (g_crashEntries.fetch_add(1) > 0)
	{
		WriteCrashText("(the crash handler was entered again: exiting)\n");
		_exit(128 + sig);
	}
	char line[512];
	snprintf(line, sizeof(line), "signal %d (%s), code %d, fault addr %p, tid %d\n", sig, strsignal(sig), info ? info->si_code : 0, info ? info->si_addr : nullptr, (int)gettid());
	WriteCrashText(line);

#if defined(__aarch64__)
	// Where it crashed, from the signal's context: _Unwind_Backtrace stops at the kernel's sigreturn frame on many phones.
	// Then the caller chain through the frame pointers (x29), which arm64 Android code keeps.
	if (context)
	{
		const mcontext_t& mc = static_cast<ucontext_t*>(context)->uc_mcontext;
		WriteFrame("crash", mc.pc);
		WriteFrame("lr   ", mc.regs[30]);
		uintptr_t sp = mc.sp, fp = mc.regs[29];
		for (int i = 0; i < 32; i++)
		{
			// a frame record is {previous fp, return address}, above the stack pointer and within 8 MB of it
			if (fp < sp || fp - sp > (8u << 20) || (fp & 15) != 0)
				break;
			const uintptr_t* record = reinterpret_cast<const uintptr_t*>(fp);
			uintptr_t next = record[0], ret = record[1];
			if (ret == 0)
				break;
			snprintf(line, sizeof(line), "#%02d", i);
			WriteFrame(line, ret);
			if (next <= fp)
				break;
			fp = next;
		}
	}
#endif

	Backtrace bt;
	_Unwind_Backtrace(CollectFrame, &bt);
	WriteCrashText("unwinder:\n");
	for (int i = 0; i < bt.count; i++)
	{
		snprintf(line, sizeof(line), "u%02d", i);
		WriteFrame(line, reinterpret_cast<uintptr_t>(bt.frames[i]));
	}
	// Back to the default action, then returning re-runs the faulting instruction and the process ends with the signal
	// (so Android still records its own tombstone and exit reason).
	ResetCrashSignals();
}

static void InstallCrashHandler()
{
	rename("/sdcard/FlipendoHP/crash.txt", "/sdcard/FlipendoHP/crash.prev.txt");
	// older builds could write a crash.txt of gigabytes (the handler looped): keep its start
	struct stat prev = {};
	if (stat("/sdcard/FlipendoHP/crash.prev.txt", &prev) == 0 && prev.st_size > 256 * 1024)
		(void)!truncate("/sdcard/FlipendoHP/crash.prev.txt", 256 * 1024);
	g_crashFd = open("/sdcard/FlipendoHP/crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

	struct sigaction action = {};
	action.sa_sigaction = CrashHandler;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&action.sa_mask);
	for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSYS })
		sigaction(sig, &action, nullptr);
}

// The folder with the game (System/HP.exe): /sdcard/FlipendoHP, or the first folder inside it, or inside that.
// A game copied one level too deep (/sdcard/FlipendoHP/FlipendoHP/System/...) is a common mistake.
static bool HasGame(const std::string& dir)
{
	return access((dir + "/System/HP.exe").c_str(), F_OK) == 0 || access((dir + "/system/HP.exe").c_str(), F_OK) == 0;
}

static std::string FindGame(const std::string& dir, int depth)
{
	if (HasGame(dir))
		return dir;
	if (depth == 0)
		return "";
	std::vector<std::string> subdirs;
	if (DIR* d = opendir(dir.c_str()))
	{
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] != '.' && e->d_type == DT_DIR)
				subdirs.push_back(dir + "/" + e->d_name);
		}
		closedir(d);
	}
	for (const std::string& sub : subdirs)
	{
		std::string found = FindGame(sub, depth - 1);
		if (!found.empty())
			return found;
	}
	return "";
}

static std::string FindGameDir()
{
	std::string found = FindGame("/sdcard/FlipendoHP", 2);
	return found.empty() ? "/sdcard/FlipendoHP" : found;
}

// The app in the background: the sound stops with it (SDL already holds the game thread in its event pump).
static void PauseAudio(bool pause)
{
	ALCcontext* context = alcGetCurrentContext();
	ALCdevice* device = context ? alcGetContextsDevice(context) : nullptr;
	if (!device || !alcIsExtensionPresent(device, "ALC_SOFT_pause_device"))
		return;
	auto pauseDevice = reinterpret_cast<LPALCDEVICEPAUSESOFT>(alcGetProcAddress(device, "alcDevicePauseSOFT"));
	auto resumeDevice = reinterpret_cast<LPALCDEVICERESUMESOFT>(alcGetProcAddress(device, "alcDeviceResumeSOFT"));
	if (pause && pauseDevice)
		pauseDevice(device);
	else if (!pause && resumeDevice)
		resumeDevice(device);
}

static bool SDLCALL LifecycleWatch(void*, SDL_Event* event)
{
	if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
		PauseAudio(true);
	else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
		PauseAudio(false);
	return true;
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

// Tells the player the save is done (a toast): called on the game thread by HP1::TickSaveRequest.
void AndroidNotifySaved(int slot)
{
	JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!env || !activity)
		return;
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(cls, "onSaved", "(I)V");
	if (method)
		env->CallVoidMethod(activity, method, (jint)slot);
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
		SDL_AddEventWatch(LifecycleWatch, nullptr);

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
		// The mod switch asks to load a save once the game is back up: the slot in .selfpack-autoload, used once.
		{
			const char* path = "/sdcard/FlipendoHP/.selfpack-autoload";
			std::ifstream autoload(path);
			std::string slot;
			if (autoload && std::getline(autoload, slot))
			{
				while (!slot.empty() && (slot.back() == 0x0d || slot.back() == 0x20)) slot.pop_back();
				if (!slot.empty())
					args.push_back("--autoload=" + slot);
			}
			autoload.close();
			unlink(path);
		}
		args.push_back("--logfile=/sdcard/FlipendoHP/flipendo.log");
		// The game folder: the one the launcher found or the player picked (SetupActivity passes it as the first argument),
		// else the default place.
		std::string gameDir = argc > 1 && argv[1] && argv[1][0] ? FindGame(argv[1], 0) : std::string();
		if (gameDir.empty())
			gameDir = FindGameDir();
		// the Russian text translation is installed in that folder (SetupActivity): its Cyrillic needs drawn glyphs
		if (access((gameDir + "/.flipendo-text").c_str(), F_OK) == 0)
			args.push_back("--cyrillic");
		__android_log_print(ANDROID_LOG_INFO, "flipendo", "game folder: %s", gameDir.c_str());
		args.push_back(gameDir);
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
