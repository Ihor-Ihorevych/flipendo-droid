// Libretro host for the PS1 emulator core (SwanStation) inside Flipendo: libps1host.so, the entry of Ps1Activity.
// It loads the core with dlopen, gives it an OpenGL ES context (SDL3), shows its frames, plays its audio and feeds it the
// pad (the touch overlay through nativeSetPad, gamepads and keys through SDL). Design: docs/superpowers/specs/2026-11-05-ps1-swanstation-design.md,
// what the core needs from a host: docs/ps1.md.
//
// argv (Ps1Activity.getArguments): [1] core .so, [2] disc (.cue/.chd), [3] system dir (BIOS, shader cache), [4] save dir
// (memory cards, save states), [5] resolution scale (1..4), [6] BIOS file name in the system dir, [7] renderer ("opengl" or
// "software"; older launchers pass none: OpenGL).
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"

#define TAG "ps1host"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace
{
// ---- state shared with Java (any thread) ----
std::atomic<uint32_t> g_touchPad{0};  // libretro joypad bits from the on-screen pad
std::atomic<int> g_command{0};        // CMD_*, taken by the main loop
std::atomic<bool> g_paused{false};    // the pause menu is open
enum { CMD_NONE = 0, CMD_SAVE_STATE = 1, CMD_LOAD_STATE = 2, CMD_QUIT = 3 };

// ---- state of the main thread ----
std::string g_systemDir, g_saveDir;
std::map<std::string, std::string> g_options;
retro_hw_render_callback g_hw{};
bool g_hwSet = false;
bool g_reinitVideo = false;
GLuint g_fbo = 0, g_tex = 0, g_depth = 0;
unsigned g_fboW = 0, g_fboH = 0;
unsigned g_frameW = 0, g_frameH = 0;
float g_aspect = 4.0f / 3.0f;
double g_fps = 60.0, g_sampleRate = 44100.0;
SDL_AudioStream* g_audio = nullptr;
uint32_t g_gamepadBits = 0;           // from gamepads, keys
uint32_t g_keyBits = 0;
bool g_dpadFromStick[4] = {false, false, false, false};
bool g_triggerL2 = false, g_triggerR2 = false;
bool g_software = false;               // the core draws on the CPU: its frames are uploaded into g_tex
bool g_softFrame = false;              // g_tex holds a software frame (top row first: shown flipped)
retro_pixel_format g_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
std::vector<uint32_t> g_rgba;          // a software frame converted to RGBA

void LogCallback(enum retro_log_level level, const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	int prio = level >= RETRO_LOG_ERROR ? ANDROID_LOG_ERROR : level == RETRO_LOG_WARN ? ANDROID_LOG_WARN : level == RETRO_LOG_INFO ? ANDROID_LOG_INFO : ANDROID_LOG_DEBUG;
	__android_log_write(prio, "ps1core", buf);
}

uintptr_t GetCurrentFramebuffer() { return g_fbo; }

// The framebuffer the core renders into: at least the size it said it needs (max geometry) and one PS1 VRAM at the scale.
void AllocateFramebuffer(unsigned w, unsigned h)
{
	if (g_fbo && w <= g_fboW && h <= g_fboH)
		return;
	if (g_fbo) { glDeleteFramebuffers(1, &g_fbo); glDeleteTextures(1, &g_tex); glDeleteRenderbuffers(1, &g_depth); }
	g_fboW = w;
	g_fboH = h;
	glGenTextures(1, &g_tex);
	glBindTexture(GL_TEXTURE_2D, g_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glGenRenderbuffers(1, &g_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, g_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
	glGenFramebuffers(1, &g_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_depth);
	LOGI("framebuffer %ux%u status 0x%x", w, h, glCheckFramebufferStatus(GL_FRAMEBUFFER));
}

bool Environment(unsigned cmd, void* data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		static_cast<retro_log_callback*>(data)->log = LogCallback;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
		*static_cast<const char**>(data) = g_systemDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*static_cast<const char**>(data) = g_saveDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		// the format of software frames (hardware frames are in the framebuffer and have none)
		g_pixelFormat = *static_cast<const retro_pixel_format*>(data);
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*static_cast<bool*>(data) = true;
		return true;
	case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
		*static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGLES_VERSION;
		return true;
	case RETRO_ENVIRONMENT_SET_HW_RENDER:
	{
		auto* cb = static_cast<retro_hw_render_callback*>(data);
		if (cb->context_type != RETRO_HW_CONTEXT_OPENGLES3 && cb->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION)
			return false; // only a GLES context is on offer
		cb->get_current_framebuffer = GetCurrentFramebuffer;
		cb->get_proc_address = reinterpret_cast<retro_hw_get_proc_address_t>(SDL_GL_GetProcAddress);
		g_hw = *cb;
		g_hwSet = true;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		auto* v = static_cast<retro_variable*>(data);
		auto it = g_options.find(v->key);
		if (it == g_options.end())
			return false;
		v->value = it->second.c_str();
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*static_cast<bool*>(data) = false;
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
		g_aspect = static_cast<retro_game_geometry*>(data)->aspect_ratio;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
	{
		auto* av = static_cast<retro_system_av_info*>(data);
		g_aspect = av->geometry.aspect_ratio;
		g_fps = av->timing.fps;
		AllocateFramebuffer(av->geometry.max_width, av->geometry.max_height);
		g_reinitVideo = true;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE:
		LOGI("core message: %s", static_cast<retro_message*>(data)->msg);
		return true;
	default:
		return false;
	}
}

// A software frame (rows of pixels in the core's format, pitch bytes apart) as RGBA in g_tex: the blit shows it as it does a
// hardware frame, flipped since its first row is the top one.
void UploadSoftwareFrame(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if (width == 0 || height == 0 || width > g_fboW || height > g_fboH)
		return;
	g_rgba.resize(static_cast<size_t>(width) * height);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	for (unsigned y = 0; y < height; y++, src += pitch)
	{
		uint32_t* out = &g_rgba[static_cast<size_t>(y) * width];
		if (g_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888)
		{
			const uint32_t* in = reinterpret_cast<const uint32_t*>(src);
			for (unsigned x = 0; x < width; x++)
			{
				uint32_t p = in[x]; // 0x00RRGGBB; RGBA bytes in memory are R, G, B, A
				out[x] = ((p >> 16) & 0xFF) | (p & 0xFF00) | ((p & 0xFF) << 16) | 0xFF000000u;
			}
		}
		else
		{
			const uint16_t* in = reinterpret_cast<const uint16_t*>(src);
			const bool rgb565 = g_pixelFormat == RETRO_PIXEL_FORMAT_RGB565;
			for (unsigned x = 0; x < width; x++)
			{
				uint16_t p = in[x];
				uint32_t r, g, b;
				if (rgb565) { r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31; g = (g << 2) | (g >> 4); }
				else { r = (p >> 10) & 31; g = (p >> 5) & 31; b = p & 31; g = (g << 3) | (g >> 2); }
				r = (r << 3) | (r >> 2);
				b = (b << 3) | (b >> 2);
				out[x] = r | (g << 8) | (b << 16) | 0xFF000000u;
			}
		}
	}
	glBindTexture(GL_TEXTURE_2D, g_tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_rgba.data());
	g_frameW = width;
	g_frameH = height;
	g_softFrame = true;
}

void VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if (data == RETRO_HW_FRAME_BUFFER_VALID) { g_frameW = width; g_frameH = height; g_softFrame = false; }
	else if (data) UploadSoftwareFrame(data, width, height, pitch);
	// no data: the same frame again (GET_CAN_DUPE), the last one stays
}

size_t AudioBatch(const int16_t* samples, size_t frames)
{
	if (g_audio)
		SDL_PutAudioStreamData(g_audio, samples, static_cast<int>(frames * 4));
	return frames;
}

void AudioSample(int16_t l, int16_t r)
{
	int16_t s[2] = {l, r};
	AudioBatch(s, 1);
}

void InputPoll() {}

int16_t InputState(unsigned port, unsigned device, unsigned, unsigned id)
{
	if (port != 0 || device != RETRO_DEVICE_JOYPAD || id > RETRO_DEVICE_ID_JOYPAD_R3)
		return 0;
	uint32_t bits = g_touchPad.load() | g_gamepadBits | g_keyBits;
	for (int i = 0; i < 4; i++)
		if (g_dpadFromStick[i])
			bits |= 1u << (RETRO_DEVICE_ID_JOYPAD_UP + i);
	if (g_triggerL2) bits |= 1u << RETRO_DEVICE_ID_JOYPAD_L2;
	if (g_triggerR2) bits |= 1u << RETRO_DEVICE_ID_JOYPAD_R2;
	return (bits >> id) & 1;
}

// ---- SDL input: gamepads and keys ----
int GamepadButtonToJoypad(SDL_GamepadButton b)
{
	switch (b)
	{
	case SDL_GAMEPAD_BUTTON_SOUTH: return RETRO_DEVICE_ID_JOYPAD_B;     // cross
	case SDL_GAMEPAD_BUTTON_EAST: return RETRO_DEVICE_ID_JOYPAD_A;      // circle
	case SDL_GAMEPAD_BUTTON_WEST: return RETRO_DEVICE_ID_JOYPAD_Y;      // square
	case SDL_GAMEPAD_BUTTON_NORTH: return RETRO_DEVICE_ID_JOYPAD_X;     // triangle
	case SDL_GAMEPAD_BUTTON_BACK: return RETRO_DEVICE_ID_JOYPAD_SELECT;
	case SDL_GAMEPAD_BUTTON_START: return RETRO_DEVICE_ID_JOYPAD_START;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK: return RETRO_DEVICE_ID_JOYPAD_L3;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return RETRO_DEVICE_ID_JOYPAD_R3;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return RETRO_DEVICE_ID_JOYPAD_L;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return RETRO_DEVICE_ID_JOYPAD_R;
	case SDL_GAMEPAD_BUTTON_DPAD_UP: return RETRO_DEVICE_ID_JOYPAD_UP;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return RETRO_DEVICE_ID_JOYPAD_DOWN;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return RETRO_DEVICE_ID_JOYPAD_LEFT;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
	default: return -1;
	}
}

int KeyToJoypad(SDL_Keycode k)
{
	switch (k)
	{
	case SDLK_UP: return RETRO_DEVICE_ID_JOYPAD_UP;
	case SDLK_DOWN: return RETRO_DEVICE_ID_JOYPAD_DOWN;
	case SDLK_LEFT: return RETRO_DEVICE_ID_JOYPAD_LEFT;
	case SDLK_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
	case SDLK_RETURN: case SDLK_Z: return RETRO_DEVICE_ID_JOYPAD_B;
	case SDLK_X: return RETRO_DEVICE_ID_JOYPAD_A;
	case SDLK_A: return RETRO_DEVICE_ID_JOYPAD_Y;
	case SDLK_S: return RETRO_DEVICE_ID_JOYPAD_X;
	case SDLK_Q: return RETRO_DEVICE_ID_JOYPAD_L;
	case SDLK_W: return RETRO_DEVICE_ID_JOYPAD_R;
	case SDLK_SPACE: return RETRO_DEVICE_ID_JOYPAD_START;
	case SDLK_TAB: return RETRO_DEVICE_ID_JOYPAD_SELECT;
	default: return -1;
	}
}

void HandleEvent(const SDL_Event& e, bool& quit, bool& background)
{
	switch (e.type)
	{
	case SDL_EVENT_QUIT: quit = true; break;
	case SDL_EVENT_DID_ENTER_BACKGROUND:
		background = true;
		if (g_audio) SDL_PauseAudioStreamDevice(g_audio);
		break;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		background = false;
		if (g_audio) SDL_ResumeAudioStreamDevice(g_audio);
		break;
	case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		g_gamepadBits = 0;
		for (bool& d : g_dpadFromStick) d = false;
		g_triggerL2 = g_triggerR2 = false;
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	{
		int id = GamepadButtonToJoypad(static_cast<SDL_GamepadButton>(e.gbutton.button));
		if (id >= 0)
		{
			if (e.gbutton.down) g_gamepadBits |= 1u << id;
			else g_gamepadBits &= ~(1u << id);
		}
		break;
	}
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	{
		const int v = e.gaxis.value, t = 16000;
		switch (e.gaxis.axis)
		{
		case SDL_GAMEPAD_AXIS_LEFTX: g_dpadFromStick[2] = v < -t; g_dpadFromStick[3] = v > t; break;   // left, right
		case SDL_GAMEPAD_AXIS_LEFTY: g_dpadFromStick[0] = v < -t; g_dpadFromStick[1] = v > t; break;   // up, down
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: g_triggerL2 = v > t; break;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: g_triggerR2 = v > t; break;
		default: break;
		}
		break;
	}
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	{
		int id = KeyToJoypad(e.key.key);
		if (id >= 0)
		{
			if (e.key.down) g_keyBits |= 1u << id;
			else g_keyBits &= ~(1u << id);
		}
		break;
	}
	default: break;
	}
}

// ---- display ----
void PresentFrame(SDL_Window* window)
{
	int ww = 0, wh = 0;
	SDL_GetWindowSizeInPixels(window, &ww, &wh);
	if (ww <= 0 || wh <= 0 || !g_fbo || g_frameW == 0 || g_frameH == 0)
		return;
	// letterbox to the core's aspect ratio
	float dw = static_cast<float>(ww), dh = dw / g_aspect;
	if (dh > wh) { dh = static_cast<float>(wh); dw = dh * g_aspect; }
	int x = static_cast<int>((ww - dw) / 2), y = static_cast<int>((wh - dh) / 2);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, ww, wh);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	// a software frame's first row is its top: drawn upside down onto the screen's bottom-up rows
	const int top = y + static_cast<int>(dh);
	glBlitFramebuffer(0, 0, g_frameW, g_frameH, x, g_softFrame ? top : y, x + static_cast<int>(dw), g_softFrame ? y : top, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	SDL_GL_SwapWindow(window);
}

void Toast(const char* text)
{
	SDL_ShowAndroidToast(text, 0, -1, 0, 0);
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? static_cast<size_t>(n) : 0);
	bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	return ok;
}

struct Core
{
	void* lib = nullptr;
	void (*set_environment)(retro_environment_t) = nullptr;
	void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
	void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
	void (*set_input_poll)(retro_input_poll_t) = nullptr;
	void (*set_input_state)(retro_input_state_t) = nullptr;
	void (*init)() = nullptr;
	void (*deinit)() = nullptr;
	bool (*load_game)(const retro_game_info*) = nullptr;
	void (*unload_game)() = nullptr;
	void (*get_system_av_info)(retro_system_av_info*) = nullptr;
	void (*run)() = nullptr;
	size_t (*serialize_size)() = nullptr;
	bool (*serialize)(void*, size_t) = nullptr;
	bool (*unserialize)(const void*, size_t) = nullptr;

	bool Load(const char* path)
	{
		lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
		if (!lib) { LOGE("dlopen %s: %s", path, dlerror()); return false; }
		bool ok = true;
#define SYM(field, name) field = reinterpret_cast<decltype(field)>(dlsym(lib, name)); if (!field) { LOGE("missing %s", name); ok = false; }
		SYM(set_environment, "retro_set_environment")
		SYM(set_video_refresh, "retro_set_video_refresh")
		SYM(set_audio_sample, "retro_set_audio_sample")
		SYM(set_audio_sample_batch, "retro_set_audio_sample_batch")
		SYM(set_input_poll, "retro_set_input_poll")
		SYM(set_input_state, "retro_set_input_state")
		SYM(init, "retro_init")
		SYM(deinit, "retro_deinit")
		SYM(load_game, "retro_load_game")
		SYM(unload_game, "retro_unload_game")
		SYM(get_system_av_info, "retro_get_system_av_info")
		SYM(run, "retro_run")
		SYM(serialize_size, "retro_serialize_size")
		SYM(serialize, "retro_serialize")
		SYM(unserialize, "retro_unserialize")
#undef SYM
		return ok;
	}
};

SDL_Window* CreateWindowWithGles(SDL_GLContext* ctxOut)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_Window* window = SDL_CreateWindow("Flipendo PS1", 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
	if (!window) { LOGE("SDL_CreateWindow: %s", SDL_GetError()); return nullptr; }
	// the core wants GLES 3.2 where the driver has it; older ones get what they have
	for (int minor : {2, 1, 0})
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
		*ctxOut = SDL_GL_CreateContext(window);
		if (*ctxOut) break;
	}
	if (!*ctxOut) { LOGE("no GLES 3 context: %s", SDL_GetError()); SDL_DestroyWindow(window); return nullptr; }
	SDL_GL_SetSwapInterval(1);
	LOGI("GL %s on %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
	return window;
}

int RunHost(int argc, char** argv)
{
	if (argc < 7)
	{
		LOGE("usage: core disc systemDir saveDir scale bios");
		return 2;
	}
	const char* corePath = argv[1];
	const char* disc = argv[2];
	g_systemDir = argv[3];
	g_saveDir = argv[4];
	int scale = atoi(argv[5]);
	if (scale < 1) scale = 1;
	if (scale > 4) scale = 4;
	const char* bios = argv[6];
	g_software = argc > 7 && strcmp(argv[7], "software") == 0;
	LOGI("renderer %s, scale %d", g_software ? "software" : "OpenGL ES", scale);

	g_options["swanstation_GPU_Renderer"] = g_software ? "Software" : "OpenGL";
	g_options["swanstation_GPU_ResolutionScale"] = std::to_string(scale);
	g_options["swanstation_BIOS_PathNTSCU"] = bios;
	g_options["swanstation_BIOS_PathPAL"] = bios;
	g_options["swanstation_BIOS_PathNTSCJ"] = bios;

	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
	// a steadier device period than the smallest burst some phones' drivers ask for (fewer, larger callbacks)
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
	{
		LOGE("SDL_Init: %s", SDL_GetError());
		return 3;
	}
	SDL_GLContext ctx = nullptr;
	SDL_Window* window = CreateWindowWithGles(&ctx);
	if (!window)
		return 3;

	Core core;
	if (!core.Load(corePath))
		return 4;
	core.set_environment(Environment);
	core.init();
	core.set_video_refresh(VideoRefresh);
	core.set_audio_sample(AudioSample);
	core.set_audio_sample_batch(AudioBatch);
	core.set_input_poll(InputPoll);
	core.set_input_state(InputState);

	retro_game_info game{};
	game.path = disc;
	if (!core.load_game(&game))
	{
		LOGE("the core refused %s", disc);
		Toast("The disc could not be loaded");
		return 5;
	}
	if (!g_hwSet && !g_software)
	{
		LOGE("the core asked for no hardware renderer");
		return 6;
	}
	retro_system_av_info av{};
	core.get_system_av_info(&av);
	g_fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
	g_sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
	g_aspect = av.geometry.aspect_ratio > 0 ? av.geometry.aspect_ratio : 4.0f / 3.0f;
	LOGI("av: base %ux%u max %ux%u fps %.3f rate %.0f", av.geometry.base_width, av.geometry.base_height, av.geometry.max_width,
	     av.geometry.max_height, g_fps, g_sampleRate);
	AllocateFramebuffer(av.geometry.max_width > 1024u * scale ? av.geometry.max_width : 1024u * scale,
	                    av.geometry.max_height > 512u * scale ? av.geometry.max_height : 512u * scale);
	if (g_hw.context_reset)
		g_hw.context_reset();

	SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(g_sampleRate)};
	g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (g_audio)
	{
		SDL_ResumeAudioStreamDevice(g_audio);
		SDL_AudioSpec dev{};
		int devFrames = 0;
		if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &devFrames))
			LOGI("audio device: %d Hz, %d channels, %d frames per period", dev.freq, dev.channels, devFrames);
	}
	else LOGE("no audio: %s", SDL_GetError());

	const std::string statePath = g_saveDir + "/state1.sav";
	bool quit = false, background = false, wasPaused = false;
	uint64_t frames = 0;
	const uint64_t startNs = SDL_GetTicksNS();
	int underruns = 0, minQueued = INT32_MAX, sinceResume = 0; // the sound's health, logged every 600 frames
	float lastRatio = 1.0f;
	while (!quit)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
			HandleEvent(e, quit, background);

		switch (g_command.exchange(CMD_NONE))
		{
		case CMD_QUIT: quit = true; break;
		case CMD_SAVE_STATE:
		{
			size_t n = core.serialize_size();
			std::vector<uint8_t> buf(n);
			if (n && core.serialize(buf.data(), n) && WriteFile(statePath, buf.data(), n)) Toast("State saved");
			else Toast("Saving the state failed");
			break;
		}
		case CMD_LOAD_STATE:
		{
			std::vector<uint8_t> buf;
			if (!ReadFile(statePath, buf)) Toast("No saved state yet");
			else if (core.unserialize(buf.data(), buf.size())) Toast("State loaded");
			else Toast("Loading the state failed");
			break;
		}
		default: break;
		}
		if (quit)
			break;

		const bool paused = g_paused.load() || background;
		if (paused != wasPaused)
		{
			if (g_audio) { if (paused) SDL_PauseAudioStreamDevice(g_audio); else SDL_ResumeAudioStreamDevice(g_audio); }
			wasPaused = paused;
			sinceResume = 0; // the queue refills after a pause: not an underrun
		}
		if (paused)
		{
			if (!background) PresentFrame(window);
			SDL_Delay(background ? 100 : 30);
			continue;
		}

		// The audio device is the clock: a frame runs once the queue is below the target (about four frames of sound, room for
		// a slow frame or a late vsync before the device runs dry). The playback rate follows the queue by up to half a percent
		// (not audible), so the queue stays near the target although the screen's refresh and the sound's clock differ.
		if (g_audio)
		{
			const int frameBytes = static_cast<int>(g_sampleRate / g_fps) * 4;
			const int target = frameBytes * 4;
			for (int i = 0; i < 50 && SDL_GetAudioStreamQueued(g_audio) > target; i++)
				SDL_Delay(1);
			const int queued = SDL_GetAudioStreamQueued(g_audio);
			if (sinceResume > 30 && queued == 0)
				underruns++;
			if (queued < minQueued)
				minQueued = queued;
			float ratio = 1.0f + 0.005f * static_cast<float>(queued - target) / static_cast<float>(target);
			ratio = ratio < 0.995f ? 0.995f : ratio > 1.005f ? 1.005f : ratio;
			SDL_SetAudioStreamFrequencyRatio(g_audio, ratio);
			lastRatio = ratio;
		}
		sinceResume++;
		core.run();
		if (++frames % 600 == 0)
		{
			const double secs = (SDL_GetTicksNS() - startNs) / 1e9;
			LOGI("frame %llu: %.1f fps, audio queued %d bytes (lowest %d), %d underruns, rate %.4f, frame %ux%u%s",
			     static_cast<unsigned long long>(frames), frames / secs, g_audio ? SDL_GetAudioStreamQueued(g_audio) : -1,
			     minQueued == INT32_MAX ? -1 : minQueued, underruns, lastRatio, g_frameW, g_frameH, g_softFrame ? " (software)" : "");
			minQueued = INT32_MAX;
		}
		if (g_reinitVideo)
		{
			g_reinitVideo = false;
			if (g_hw.context_destroy) g_hw.context_destroy();
			if (g_hw.context_reset) g_hw.context_reset();
		}
		PresentFrame(window);
	}

	core.unload_game();
	core.deinit();
	if (g_audio) SDL_DestroyAudioStream(g_audio);
	g_audio = nullptr;
	SDL_GL_DestroyContext(ctx);
	SDL_DestroyWindow(window);
	LOGI("host finished");
	return 0;
}
} // namespace

extern "C"
{
JNIEXPORT void JNICALL Java_io_github_flipendo_spike_Ps1Activity_nativeSetPad(JNIEnv*, jclass, jint mask)
{
	g_touchPad.store(static_cast<uint32_t>(mask));
}

JNIEXPORT void JNICALL Java_io_github_flipendo_spike_Ps1Activity_nativeCommand(JNIEnv*, jclass, jint command)
{
	g_command.store(command);
}

JNIEXPORT void JNICALL Java_io_github_flipendo_spike_Ps1Activity_nativePause(JNIEnv*, jclass, jboolean paused)
{
	g_paused.store(paused != JNI_FALSE);
}

__attribute__((visibility("default"))) int SDL_main(int argc, char** argv)
{
	return RunHost(argc, argv);
}
}
