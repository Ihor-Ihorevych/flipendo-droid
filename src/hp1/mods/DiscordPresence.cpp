#include "Precomp.h"
#include "HP1Mods.h"
#include "Packages/Core/UObject.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Package/PackageManager.h"
#include "Utils/CommandLine.h"
#include "Utils/StrTools.h"
#include "Engine.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// Discord Rich Presence (on by default, off with --vanilla or --no-discord): friends see the level Harry is in, whether
// he is in a cutscene or the menu book, Gryffindor's house points and how long the game has been running.
//
// Talks to the Discord client over its local IPC (\\.\pipe\discord-ipc-N on Windows, a unix socket
// $XDG_RUNTIME_DIR/discord-ipc-N elsewhere) with Discord's documented frame format, so no SDK is needed. A worker
// thread owns the connection; when Discord isn't running the mod retries every few seconds and does nothing else.
//
// Level names come from the game's own tables, as HPConsole.DrawLevelInfo (the loading screen) finds them:
// Localize("text", "n_" $ map, "Dobby") gives the level's index, Localize("text", "level_name_" $ index, "HPMenu")
// its title. The map is Level.LevelEnterText (the map the level was travelled to, also in a save: src/knowwonder/KWSave.cpp).
// Cutscenes: the HUD's bCutSceneMode + curCutScene (see CutsceneSkip.cpp); the menu: HPConsole.menuBook (FEBook);
// points: baseHarry.numHousePointsGryffindor.

namespace HP1::Mods
{
	// The Discord application whose name shows as "Playing ..."; the status shows its App Icon (no Rich Presence art
	// assets). --discord-app=<id> overrides it.
	static const char* DefaultAppId = "1556393306051846345";

	static std::string JsonString(const std::string& s)
	{
		std::string out = "\"";
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
				{
					out += (char)c;
				}
			}
		}
		return out + "\"";
	}

	// Discord's IPC: frames of [uint32 opcode][uint32 length][JSON], little endian.
	class DiscordIpc
	{
	public:
		enum Opcode : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

		~DiscordIpc() { Disconnect(); }

		bool Connect()
		{
			for (int i = 0; i < 10; i++)
			{
#ifdef _WIN32
				std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
				HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
				if (h != INVALID_HANDLE_VALUE)
				{
					pipe = h;
					return true;
				}
#else
				const char* dir = nullptr;
				for (const char* var : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" })
				{
					if ((dir = std::getenv(var)) != nullptr)
						break;
				}
				std::string path = std::string(dir ? dir : "/tmp") + "/discord-ipc-" + std::to_string(i);
				int fd = socket(AF_UNIX, SOCK_STREAM, 0);
				if (fd < 0)
					return false;
				sockaddr_un addr = {};
				addr.sun_family = AF_UNIX;
				snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
				if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0)
				{
					sock = fd;
					return true;
				}
				close(fd);
#endif
			}
			return false;
		}

		void Disconnect()
		{
#ifdef _WIN32
			if (pipe != INVALID_HANDLE_VALUE)
				CloseHandle(pipe);
			pipe = INVALID_HANDLE_VALUE;
#else
			if (sock >= 0)
				close(sock);
			sock = -1;
#endif
			received.clear();
		}

		bool Send(uint32_t opcode, const std::string& json)
		{
			std::string frame(8, '\0');
			uint32_t length = (uint32_t)json.size();
			for (int i = 0; i < 4; i++)
			{
				frame[i] = (char)((opcode >> (i * 8)) & 0xff);
				frame[4 + i] = (char)((length >> (i * 8)) & 0xff);
			}
			frame += json;
			return Write(frame.data(), frame.size());
		}

		// Reads what Discord sent without blocking; answers pings. False when the connection is gone.
		bool Poll()
		{
			char buf[4096];
			while (true)
			{
				int n = ReadAvailable(buf, sizeof(buf));
				if (n < 0)
					return false;
				if (n == 0)
					break;
				received.append(buf, n);
			}
			while (received.size() >= 8)
			{
				uint32_t opcode = 0, length = 0;
				for (int i = 0; i < 4; i++)
				{
					opcode |= (uint32_t)(uint8_t)received[i] << (i * 8);
					length |= (uint32_t)(uint8_t)received[4 + i] << (i * 8);
				}
				if (received.size() < 8 + (size_t)length)
					break;
				std::string payload = received.substr(8, length);
				received.erase(0, 8 + (size_t)length);
				if (opcode == Close)
				{
					closeReason = payload;
					return false;
				}
				if (opcode == Ping && !Send(Pong, payload))
					return false;
				if (opcode == Frame && payload.find("\"evt\":\"ERROR\"") != std::string::npos)
					errors.push_back(payload);
			}
			return true;
		}

	private:
		bool Write(const char* data, size_t size)
		{
#ifdef _WIN32
			while (size > 0)
			{
				DWORD written = 0;
				if (!WriteFile(pipe, data, (DWORD)size, &written, nullptr))
					return false;
				data += written;
				size -= written;
			}
			return true;
#else
#ifdef MSG_NOSIGNAL
			const int flags = MSG_NOSIGNAL;
#else
			const int flags = 0;
#endif
			while (size > 0)
			{
				ssize_t written = send(sock, data, size, flags);
				if (written <= 0)
					return false;
				data += written;
				size -= (size_t)written;
			}
			return true;
#endif
		}

		// Bytes read (0 when nothing is waiting), or -1 when the connection is gone.
		int ReadAvailable(char* buf, int size)
		{
#ifdef _WIN32
			DWORD available = 0;
			if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
				return -1;
			if (available == 0)
				return 0;
			DWORD read = 0;
			if (!ReadFile(pipe, buf, std::min<DWORD>(available, (DWORD)size), &read, nullptr))
				return -1;
			return (int)read;
#else
			ssize_t n = recv(sock, buf, size, MSG_DONTWAIT);
			if (n > 0)
				return (int)n;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				return 0;
			return -1;
#endif
		}

#ifdef _WIN32
		HANDLE pipe = INVALID_HANDLE_VALUE;
#else
		int sock = -1;
#endif
		std::string received;

	public:
		std::string closeReason; // the payload of Discord's Close frame
		std::vector<std::string> errors; // Discord's replies to commands it refused
	};

	// The worker thread: connects, sends the latest activity when it changes, keeps the connection alive.
	class DiscordPresence
	{
	public:
		void Start(const std::string& id)
		{
			appId = id;
			std::thread([this] { Run(); }).detach(); // ends with the process; Discord clears the presence then
		}

		// Log lines from the worker, for the game thread to write (the logger isn't thread safe).
		std::vector<std::string> TakeMessages()
		{
			std::lock_guard<std::mutex> lock(mutex);
			return std::move(messages);
		}

		// The activity as a JSON object ("null" clears it). Only the latest one is sent.
		void SetActivity(const std::string& json)
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (json == pending)
				return;
			pending = json;
			changed.notify_one();
		}

	private:
		void Log(const std::string& message)
		{
			std::lock_guard<std::mutex> lock(mutex);
			messages.push_back("Discord presence: " + message);
		}

		void Run()
		{
			using namespace std::chrono;
			const auto retryDelay = seconds(10);
			const auto minSendInterval = seconds(2); // Discord allows about 5 activity updates per 20 seconds

			DiscordIpc ipc;
			bool connected = false;
			bool loggedWaiting = false;
			std::string sent;
			int nonce = 0;
			auto nextConnect = steady_clock::now();
			auto nextSend = steady_clock::now();

			while (true)
			{
				if (!connected && steady_clock::now() >= nextConnect)
				{
					if (ipc.Connect() && ipc.Send(DiscordIpc::Handshake, "{\"v\":1,\"client_id\":" + JsonString(appId) + "}"))
					{
						connected = true;
						loggedWaiting = false;
						sent.clear();
						Log("connected");
					}
					else
					{
						ipc.Disconnect();
						if (!loggedWaiting)
							Log("Discord isn't running, retrying every 10 seconds");
						loggedWaiting = true;
						nextConnect = steady_clock::now() + retryDelay;
					}
				}

				if (connected && !ipc.Poll())
				{
					ipc.Disconnect();
					connected = false;
					nextConnect = steady_clock::now() + retryDelay;
					Log(ipc.closeReason.empty() ? "disconnected" : "Discord closed the connection: " + ipc.closeReason);
					ipc.closeReason.clear();
				}

				for (const std::string& error : ipc.errors)
					Log("Discord refused the activity: " + error);
				ipc.errors.clear();

				std::string activity;
				{
					std::unique_lock<std::mutex> lock(mutex);
					activity = pending;
				}

				if (connected && activity != sent && steady_clock::now() >= nextSend)
				{
					std::string pid =
#ifdef _WIN32
						std::to_string(GetCurrentProcessId());
#else
						std::to_string(getpid());
#endif
					std::string command = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + pid + ",\"activity\":" + activity +
						"},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
					if (ipc.Send(DiscordIpc::Frame, command))
					{
						sent = activity;
						Log("activity " + activity);
						nextSend = steady_clock::now() + minSendInterval;
					}
					else
					{
						ipc.Disconnect();
						connected = false;
						nextConnect = steady_clock::now() + retryDelay;
					}
				}

				std::unique_lock<std::mutex> lock(mutex);
				changed.wait_for(lock, milliseconds(500));
			}
		}

		std::string appId;
		std::mutex mutex;
		std::condition_variable changed;
		std::string pending = "null";
		std::vector<std::string> messages;
	};

	static int IntProperty(UObject* obj, const char* name, int defaultValue)
	{
		if (!obj || !obj->HasProperty(name))
			return defaultValue;
		return *static_cast<int32_t*>(obj->GetProperty(name));
	}

	// The level's title as the loading screen shows it, or "" when the game has none.
	static std::string LevelTitle(const std::string& map)
	{
		std::string lower = map;
		for (char& c : lower)
			c = (char)tolower((unsigned char)c);
		if (lower.rfind("quid_", 0) == 0) // the Quidditch League matches (Dobby.int lists them, HPMenu.int has no names)
			return engine->packages->Localize("HPMenu", "text", "quidditch_03");
		std::string index = engine->packages->Localize("Dobby", "text", "n_" + map);
		if (index.empty())
			return {};
		std::string title = engine->packages->Localize("HPMenu", "text", "level_name_" + index);
		return (title.empty() || title.front() == '<') ? std::string() : title;
	}

	static std::string BuildActivity(int64_t startTime)
	{
		std::string map = engine->LevelInfo ? engine->LevelInfo->LevelEnterText() : std::string();
		size_t dot = map.find('.');
		if (dot != std::string::npos)
			map = map.substr(0, dot);

		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		// The menu book is the main menu while no game runs (FEBook.bGamePlaying, set by Start Game, cleared by Quit;
		// a Quidditch League match sets bPlayingQuidditch instead), the pause menu while one does.
		UObject* menuBook = ObjectProperty(engine->console, "menuBook");
		bool bookOpen = menuBook && BoolProperty(menuBook, "bIsOpen");
		bool gamePlaying = menuBook && (BoolProperty(menuBook, "bGamePlaying") || BoolProperty(menuBook, "bPlayingQuidditch"));
		UObject* page = ObjectProperty(menuBook, "curPage");
		bool storybook = bookOpen && page && page == ObjectProperty(menuBook, "StoryBookPage");

		std::string details, state;
		if (storybook)
		{
			details = "Reading the storybook";
		}
		else if ((bookOpen && !gamePlaying) || map.empty() || StrTools::equals_ignore_case(map, "startup") || StrTools::equals_ignore_case(map, "entry"))
		{
			details = "In the main menu";
		}
		else
		{
			details = LevelTitle(map);
			if (details.empty())
				details = map;

			UObject* hud = player ? player->myHUD() : nullptr;
			if (bookOpen)
				state = "Paused";
			else if (hud && BoolProperty(hud, "bCutSceneMode") && ObjectProperty(hud, "curCutScene"))
				state = "Watching a cutscene";
			else if (int points = IntProperty(player, "numHousePointsGryffindor", -1); points >= 0)
				state = "Gryffindor: " + std::to_string(points) + " points";
		}

		std::string json = "{\"details\":" + JsonString(details);
		if (!state.empty())
			json += ",\"state\":" + JsonString(state);
		json += ",\"timestamps\":{\"start\":" + std::to_string(startTime) + "}}";
		return json;
	}

	void TickDiscordPresence(float realElapsed)
	{
		static DiscordPresence* presence = nullptr;
		static bool started = false;
		static int64_t startTime = 0;
		static float sinceUpdate = 0.0f;

		if (!started)
		{
			started = true;
			if (!Enabled() || HasFlag("--no-discord"))
				return;
			std::string appId = commandline ? commandline->GetArg("", "--discord-app", DefaultAppId) : DefaultAppId;
			if (appId.empty())
			{
				LogMessage("Discord presence: no Discord application id (--discord-app=<id>), off");
				return;
			}
			startTime = (int64_t)std::time(nullptr);
			presence = new DiscordPresence(); // lives as long as its detached thread, until the process ends
			presence->Start(appId);
			sinceUpdate = 1.0f;
		}
		if (!presence)
			return;
		for (const std::string& message : presence->TakeMessages())
			LogMessage(message);

		// The activity text changes rarely; once a second is plenty.
		sinceUpdate += realElapsed;
		if (sinceUpdate < 1.0f)
			return;
		sinceUpdate = 0.0f;
		presence->SetActivity(BuildActivity(startTime));
	}
}
