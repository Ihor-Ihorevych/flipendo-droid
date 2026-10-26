#include "Precomp.h"
#include "HP2.h"
#include "Utils/CommandLine.h"
#include "KW.h"
#include "HP1.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Audio/AudioDevice.h"
#include "Audio/AudioSource.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "Utils/StrTools.h"
#include <surrealwidgets/core/widget.h>
#include <surrealwidgets/core/canvas.h>
#include <surrealwidgets/core/image.h>
#include <surrealwidgets/core/font.h>
#include <surrealwidgets/core/span_layout.h>
#include <surrealwidgets/core/timer.h>
#include <surrealwidgets/window/window.h>
#include <surrealwidgets/widgets/dialog/messagebox.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

// HP2's front end (docs/re/hp2/startup.md): what Game.exe shows before the engine starts. The EA and Warner Bros.
// splashes, then a dialog whose pages are Main Menu, New Game, Load Game, Options, Video and Sound. Reversed from the
// No-CD Game.exe (the retail one's code is encrypted by SafeDisc) and the classes and dialog templates it uses from
// HP2's Window.dll. Game.exe hands the choice to a second run of itself (PrivetDr.unr -SAVESLOT=n, or -LOADGAME=0
// -SAVESLOT=n); here the same run goes on with it.

namespace HP2
{
	static int loadGameSave = -1;
	static int soundDisabled = -1; // not read yet

	// IDA Engine.dll: ?Init@UGameEngine@@UAEHXZ [HP2 0x103A0FA0] (the LOADGAME= option, at its end)
	// Set by Load Game in the front end, or by --loadgame=<n> on the command line (Game.exe's -LOADGAME=<n>)
	int LoadGameSave()
	{
		if (loadGameSave < 0)
		{
			std::string arg = commandline->GetArg("", "--loadgame", "");
			if (!arg.empty())
				return std::atoi(arg.c_str());
		}
		return loadGameSave;
	}

	// IDA Engine.dll: ?InitAudio@UEngine@@QAEXXZ [HP2 0x1039A990]
	// With UseSound off (or -NOSOUND) the engine makes no audio subsystem. The front end's "Disable all sound" writes it.
	bool SoundDisabled()
	{
		if (soundDisabled < 0)
			soundDisabled = engine && engine->packages && StrTools::equals_ignore_case(engine->packages->GetIniValue("System", "Engine.GameEngine", "UseSound", "True"), "False");
		return soundDisabled == 1;
	}

	namespace
	{
		// The dialogs use MS Sans Serif 8 (dialog base units 6 x 13 pixels). Template 152's page area (319x252) holds
		// Background.bmp (480x412); WFrontEndPageMainMenu::OnInitDialog sizes the window to 486x566 with its frame.
		Rect DlgRect(int x, int y, int width, int height)
		{
			return Rect::xywh(x * 6 / 4.0, y * 13 / 8.0, width * 6 / 4.0, height * 13 / 8.0);
		}
		const double ClientWidth = 480.0, ClientHeight = 536.0;

		// Every page's OnCurrent sets the dialog's colour to COLORREF 0x090B0C (WFrontEndDialog's own, 0x0B0309, is only
		// seen before the first page). Labels and buttons are created with white text (WColorLabel, WCoolButton), which a
		// disabled button keeps.
		const Colorf DialogBackground = Colorf::fromRgba8(12, 11, 9);
		const Colorf TextColor = Colorf::fromRgba8(255, 255, 255);
		const Colorf DisabledTextColor = Colorf::fromRgba8(128, 128, 128);
		const Colorf ListSelection = Colorf::fromRgba8(40, 60, 140);

		std::shared_ptr<Image> LoadBitmap(const fs::path& folder, const std::string& name)
		{
			fs::path path = KW::FindIgnoringCase(folder, name);
			if (path.empty())
				return nullptr;
			try
			{
				Array<uint8_t> file = File::read_all_bytes(path.string());
				int width = 0, height = 0;
				Array<uint8_t> pixels;
				if (KW::ReadBMP(file, width, height, pixels))
					return Image::Create(width, height, ImageFormat::B8G8R8A8, pixels.data());
			}
			catch (const std::exception&)
			{
			}
			LogMessage("HP2 front end: can't read " + path.string());
			return nullptr;
		}

		std::string Localize(const char* package, const char* section, const char* key, const std::string& fallback)
		{
			std::string text = engine->packages->Localize(package, section, key);
			return text.empty() ? fallback : text;
		}

		fs::path HelpFolder()
		{
			return KW::FindIgnoringCase(engine->LaunchInfo.gameRootFolder, "Help");
		}

		// Window.dll's WinPlaySound (PlaySoundW with SND_FILENAME) for ..\Help's wav files. Game.exe passes
		// SND_ASYNC | SND_NOSTOP (0x11): nothing new starts while a sound plays; 0 waits for the end. The engine's audio
		// device isn't up yet, so the front end opens its own and plays each file as its one stream.
		class FrontEndSounds
		{
		public:
			~FrontEndSounds()
			{
				if (device)
					device->PlayMusic(nullptr);
			}

			// Opening the device takes a moment: done with the window, not at the first click
			void Open()
			{
				try
				{
					device = AudioDevice::Create(44100, 8, 16, 256);
					device->SetMusicVolume(1.0f);
				}
				catch (const std::exception& e)
				{
					LogMessage(std::string("HP2 front end: no sound: ") + e.what());
				}
			}

			void Play(const std::string& name, bool wait)
			{
				auto now = std::chrono::steady_clock::now();
				if (now < playingUntil)
					return;

				fs::path path = KW::FindIgnoringCase(HelpFolder(), name);
				if (path.empty())
					return;
				try
				{
					auto source = AudioSource::CreateWav(File::read_all_bytes(path.string()));
					if (!source || source->GetFrequency() <= 0 || !device)
						return;
					double seconds = source->GetSamples() / (double)source->GetFrequency();
					device->PlayMusic(std::move(source));
					playingUntil = now + std::chrono::milliseconds((int)(seconds * 1000.0) + 50);
					if (wait)
						std::this_thread::sleep_until(playingUntil);
				}
				catch (const std::exception& e)
				{
					LogMessage(std::string("HP2 front end: can't play ") + name + ": " + e.what());
				}
			}

			// IDA Game.exe: not exported: sub_10903990 [HP2 Game 0x10903990] (No-CD exe; the "..\Help\Button0.wav" string)
			// Going to a page, Back, a renderer: Button0, 1 or 2 at random
			void Button(bool wait = false)
			{
				static const char* names[] = { "Button0.wav", "Button1.wav", "Button2.wav" };
				Play(names[std::rand() % 3], wait);
			}

			// IDA Game.exe: not exported: sub_10903A10 [HP2 Game 0x10903A10] (No-CD exe; the "..\Help\Rollover0.wav" string)
			// Picking a slot, Quit, the sound page's OK: Rollover0 or 1 at random
			void Rollover(bool wait = false)
			{
				Play(std::rand() % 2 ? "Rollover1.wav" : "Rollover0.wav", wait);
			}

		private:
			std::unique_ptr<AudioDevice> device;
			std::chrono::steady_clock::time_point playingUntil;
		};

		struct Control
		{
			enum class Type { Button, SlotButton, CheckBox, Radio, Slider, ListBox, Label };

			Type type = Type::Label;
			Rect box;
			std::string text;
			bool enabled = true;
			bool checked = false;              // CheckBox, Radio
			int value = 0;                     // Slider: 0..100
			std::shared_ptr<Image> image;      // Button, SlotButton: its bitmap
			std::vector<std::string> items;    // ListBox
			int current = -1;
			int top = 0;
			std::function<void()> onClick;
		};

		struct Page
		{
			std::string title;                 // the window title and the page's IDC_Title label, drawn when showTitle
			Rect titleBox;
			bool showTitle = true;
			std::string instructions;          // shown in the dialog's Instructions label (3022)
			std::string okText;                // OK is hidden without one
			std::vector<Control> controls;
			std::function<void()> onOk;
		};

		struct Slot
		{
			fs::path folder;
			bool empty = true;
			std::shared_ptr<Image> thumbnail;
		};

		// A window run with its own DisplayWindow::RunLoop until EndModal. The loop is ended once only: an ExitLoop while
		// no loop runs would end the next one (the next window's) at once. A tick every 50 ms keeps X11's loop waking up:
		// it looks at ExitLoop only after it wakes for an event or a timer, and with no timer it may wait for the next
		// mouse move. The owner destroys the window.
		class ModalWindow : public Widget
		{
		public:
			ModalWindow(WidgetType type) : Widget(nullptr, type), tick(this) { }

			void RunModal()
			{
				running = true;
				tick.FuncExpired = [this] { OnTick(); };
				tick.Start(50, true);
				DisplayWindow::RunLoop();
				tick.Stop();
				running = false;
			}

			void EndModal()
			{
				if (running)
				{
					running = false;
					DisplayWindow::ExitLoop();
				}
			}

			bool Closed = false;

		protected:
			virtual void OnTick() { }

			void OnClose() override
			{
				Closed = true;
				EndModal();
			}

		private:
			Timer tick;
			bool running = false;
		};

		class SplashWindow : public ModalWindow
		{
		public:
			SplashWindow(std::shared_ptr<Image> image, WidgetType type) : ModalWindow(type), image(std::move(image)) { }

			void OnPaint(Canvas* canvas) override
			{
				canvas->fillRect(Rect::xywh(0.0, 0.0, GetWidth(), GetHeight()), Colorf::fromRgba8(0, 0, 0));
				if (image)
					canvas->drawImage(image, Rect::xywh(0.0, 0.0, GetWidth(), GetHeight()));
			}

			bool OnMouseDown(const Point& pos, InputKey key) override { EndModal(); return true; }
			void OnKeyDown(InputKey key) override { EndModal(); }
			void OnTick() override
			{
				if (std::chrono::steady_clock::now() >= end)
					EndModal();
			}

			std::shared_ptr<Image> image;
			std::chrono::steady_clock::time_point end;
		};

		// Shows a splash bitmap from ..\Help centred on the screen for the given seconds, in a window without a frame.
		// Game.exe tries <name><Language>.bmp first ([Engine.Engine] Language), then the plain name. A click or a key
		// ends it early here. False when the window was closed.
		bool ShowSplash(const std::string& localized, const std::string& fallback, int seconds)
		{
			auto image = LoadBitmap(HelpFolder(), localized);
			if (!image)
				image = LoadBitmap(HelpFolder(), fallback);
			if (!image || seconds <= 0)
				return true;

			std::unique_ptr<SplashWindow> splash;
			try
			{
				splash = std::make_unique<SplashWindow>(image, WidgetType::Popup);
			}
			catch (const std::exception&)
			{
				// Wayland only makes popups for an owner window: a normal window then
				splash = std::make_unique<SplashWindow>(image, WidgetType::Window);
			}
			Size screen = Widget::GetScreenSize();
			double width = image->GetWidth(), height = image->GetHeight();
			splash->SetFrameGeometry((screen.width - width) * 0.5, (screen.height - height) * 0.5, width, height);
			splash->Show();

			splash->end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
			splash->RunModal();
			return !splash->Closed;
		}

		class FrontEndWindow : public ModalWindow
		{
		public:
			FrontEndWindow();

			bool Finished = false;     // a game was chosen
			bool NewGame = false;
			int SlotNumber = 0;        // 1 to 6

		protected:
			void OnPaint(Canvas* canvas) override;
			bool OnMouseDown(const Point& pos, InputKey key) override;
			bool OnMouseUp(const Point& pos, InputKey key) override;
			bool OnMouseWheel(const Point& pos, InputKey key) override;
			void OnMouseMove(const Point& pos) override;
			void OnMouseLeave() override;
			void OnKeyDown(InputKey key) override;
			void OnClose() override;
			void OnGeometryChanged() override;

		private:
			bool sizeLocked = false;
			Page MainMenuPage();
			Page NewGamePage();
			Page LoadGamePage();
			Page OptionsPage();
			Page VideoPage();
			Page SoundPage();

			void Advance(Page page);
			void Back();
			void Ok();
			void Quit();
			void ScanSlots();
			void ClearSlot(int number);
			void StartGame(bool newGame, int number);

			Page& CurrentPage() { return pages.back(); }
			// The dialog's own controls, Back (3007) and OK (3000), then the page's: index 0 / 1 / 2 + n
			Control* ControlByIndex(int index);
			Control* ControlAt(const Point& pos, int& index);
			void SetSlider(Control& control, double x);

			void DrawText(Canvas* canvas, const Rect& box, const std::string& text, const Colorf& color, bool center);
			void DrawButton(Canvas* canvas, const Control& control, bool pressed);
			void DrawControl(Canvas* canvas, const Control& control, bool pressed);

			std::vector<Page> pages;
			Control backButton, okButton;
			Slot slots[7];             // 1 to 6
			FrontEndSounds sounds;
			int pressedIndex = -1;
			bool sliderDrag = false;

			std::shared_ptr<Font> font = Font::Create("NotoSans", 12.0);
			std::shared_ptr<Image> background, buttonUp, thumbUp, thumbDown, slider, sliderKnob, checkOn, checkOff, radioOn, radioOff;
		};

		// IDA Game.exe: not exported: sub_10911320 [HP2 Game 0x10911320] (No-CD exe; WFrontEnd::OnInitDialog, the "..\Help\ButtonUp.bmp" string)
		// Loads the bitmaps (Window.dll's LoadFileToBitmap: 8-bit BMPs) and checks the six slots. ButtonDown.bmp is loaded
		// too but never used.
		FrontEndWindow::FrontEndWindow() : ModalWindow(WidgetType::Window)
		{
			fs::path help = HelpFolder();
			background = LoadBitmap(help, "Background.bmp");
			buttonUp = LoadBitmap(help, "ButtonUp.bmp");
			thumbUp = LoadBitmap(help, "ButtonThumbUp.bmp");
			thumbDown = LoadBitmap(help, "ButtonThumbDown.bmp");
			slider = LoadBitmap(help, "HP2Slider.bmp");
			sliderKnob = LoadBitmap(help, "HP2SliderKnob.bmp");
			checkOn = LoadBitmap(help, "CheckBoxOn.bmp");
			checkOff = LoadBitmap(help, "CheckBoxOff.bmp");
			radioOn = LoadBitmap(help, "RadioOn.bmp");
			radioOff = LoadBitmap(help, "RadioOff.bmp");

			auto icons = KW::GameIcons(engine->LaunchInfo.gameRootFolder, engine->LaunchInfo.gameExecutableName);
			if (!icons.empty())
				SetWindowIcon(icons);

			// Dialog 152: Back and OK sit under the page, WBitmapButtons over ButtonUp.bmp
			backButton.type = Control::Type::Button;
			backButton.box = DlgRect(64, 252, 85, 38);
			backButton.text = Localize("HPMenu", "All", "Back_Button_0000", "Back");
			backButton.image = buttonUp;
			backButton.onClick = [this] { Back(); };
			okButton.type = Control::Type::Button;
			okButton.box = DlgRect(171, 252, 85, 38);
			okButton.image = buttonUp;
			okButton.onClick = [this] { Ok(); };

			sounds.Open();
			ScanSlots();
			Advance(MainMenuPage());
		}

		// The slot folders are SavePath\Slot1 to Slot6 (made if missing), the same that Core.dll's appInit makes from
		// -SAVESLOT=<n>. A slot is used when its Save0.usa isn't empty; then its picture is the Save0.bmp the engine copied
		// from Thumbnails\ when it saved. An empty slot is cleared of whatever files are left in its folder.
		void FrontEndWindow::ScanSlots()
		{
			for (int number = 1; number <= 6; number++)
			{
				Slot& slot = slots[number];
				slot.folder = engine->packages->GetSaveFolderPath() / ("Slot" + std::to_string(number));
				std::error_code ec;
				fs::create_directories(slot.folder, ec);
				fs::path save = slot.folder / ("Save0." + engine->packages->GetSaveExtension());
				slot.empty = !(fs::is_regular_file(save, ec) && fs::file_size(save, ec) > 0);
				slot.thumbnail = nullptr;
				if (slot.empty)
					ClearSlot(number);
				else
					slot.thumbnail = LoadBitmap(slot.folder, "Save0.bmp");
			}
		}

		// IDA Game.exe: not exported: sub_109124F0 [HP2 Game 0x109124F0] (No-CD exe; called from WFrontEnd::OnInitDialog and the New Game slot buttons)
		// Deletes every file in the slot's folder and marks it empty
		void FrontEndWindow::ClearSlot(int number)
		{
			Slot& slot = slots[number];
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(slot.folder, ec))
			{
				if (entry.is_regular_file(ec))
					fs::remove(entry.path(), ec);
			}
			slot.empty = true;
			slot.thumbnail = nullptr;
		}

		// IDA Game.exe: not exported: sub_10912FB0 [HP2 Game 0x10912FB0] (No-CD exe; the "IDC_NewGame" string)
		// IDA Game.exe: not exported: sub_10912EB0 [HP2 Game 0x10912EB0] (No-CD exe; WFrontEndPageMainMenu's OnCurrent)
		// IDA Game.exe: not exported: sub_109131D0 [HP2 Game 0x109131D0] (No-CD exe; its GetOkText, the "InGameMenu_0025" string)
		// IDA Game.exe: not exported: sub_10913200 [HP2 Game 0x10913200] (No-CD exe; its OnOk, the next vtable slot)
		// Template 162: three buttons over ButtonUp.bmp down the left, the title label hidden. OK is Quit: a rollover
		// sound, played to the end, then the dialog closes.
		Page FrontEndWindow::MainMenuPage()
		{
			const char* section = "IDDIALOG_FrontEndPageMainMenu";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "Main Menu");
			page.titleBox = DlgRect(24, 107, 271, 10);
			page.showTitle = false;
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");
			page.okText = Localize("HPMenu", "All", "InGameMenu_0025", "Quit");
			page.onOk = [this] { Quit(); };

			auto button = [&](int y, const char* key, const char* fallback, std::function<Page()> next) {
				Control c;
				c.type = Control::Type::Button;
				c.box = DlgRect(40, y, 85, 38);
				c.text = Localize("Startup", section, key, fallback);
				c.image = buttonUp;
				c.onClick = [this, next] { sounds.Button(); Advance(next()); };
				page.controls.push_back(c);
			};
			button(124, "IDC_NewGame", "New Game", [this] { return NewGamePage(); });
			button(166, "IDC_LoadGame", "Load Game", [this] { return LoadGamePage(); });
			button(208, "IDC_Options", "Options", [this] { return OptionsPage(); });
			return page;
		}

		static const int SlotBoxes[7][4] = {
			{ 0, 0, 0, 0 },
			{ 24, 124, 81, 57 }, { 120, 124, 80, 57 }, { 215, 124, 82, 57 },
			{ 24, 190, 81, 57 }, { 120, 190, 80, 57 }, { 215, 190, 82, 57 }
		};

		// IDA Game.exe: not exported: sub_109136B0 [HP2 Game 0x109136B0] (No-CD exe; the "IDDIALOG_FrontEndPageNewGame" and "%d - %s" strings)
		// IDA Game.exe: not exported: sub_10913C10 [HP2 Game 0x10913C10] (No-CD exe; slot 1's button, the "Select_Game_0001" string; slots 2-6 follow it)
		// Template 163: six slot buttons, "1 - Empty" over ButtonThumbUp.bmp or "1 - Used" over the slot's Save0.bmp; no OK.
		// A click: a rollover sound; a used slot asks "Replace Game?" first (No keeps it); the slot is cleared, a button
		// sound, and the game starts in it.
		Page FrontEndWindow::NewGamePage()
		{
			const char* section = "IDDIALOG_FrontEndPageNewGame";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "New Game");
			page.titleBox = DlgRect(24, 108, 271, 10);
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");

			std::string empty = Localize("Startup", section, "IDC_SlotEmpty", "Empty");
			std::string used = Localize("Startup", section, "IDC_SlotUsed", "Used");
			for (int number = 1; number <= 6; number++)
			{
				Control c;
				c.type = Control::Type::SlotButton;
				c.box = DlgRect(SlotBoxes[number][0], SlotBoxes[number][1], SlotBoxes[number][2], SlotBoxes[number][3]);
				c.text = std::to_string(number) + " - " + (slots[number].empty ? empty : used);
				c.image = slots[number].empty ? thumbUp : slots[number].thumbnail;
				c.onClick = [this, number] {
					sounds.Rollover();
					if (!slots[number].empty)
					{
						std::string question = Localize("HPMenu", "All", "Select_Game_0001", "Are you sure you want to replace this game?");
						std::string title = Localize("HPMenu", "All", "Select_Game_0002", "Replace Game");
						if (MessageBox::Question(this, question, title) != DialogButton::Yes)
							return;
					}
					ClearSlot(number);
					sounds.Button();
					StartGame(true, number);
				};
				page.controls.push_back(c);
			}
			return page;
		}

		// IDA Game.exe: not exported: sub_10914CE0 [HP2 Game 0x10914CE0] (No-CD exe; the "IDDIALOG_FrontEndPageLoadGame" and "%d - %s" strings)
		// IDA Game.exe: not exported: sub_10915370 [HP2 Game 0x10915370] (No-CD exe; slot 1's button, the "UseSaveSlot" string; slots 2-6 follow it)
		// Template 163 again: an empty slot is disabled over ButtonThumbDown.bmp. A used one: a rollover sound and the game
		// loads.
		Page FrontEndWindow::LoadGamePage()
		{
			const char* section = "IDDIALOG_FrontEndPageLoadGame";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "Loading Game");
			page.titleBox = DlgRect(24, 108, 271, 10);
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");

			std::string empty = Localize("Startup", section, "IDC_SlotEmpty", "Empty");
			std::string used = Localize("Startup", section, "IDC_SlotUsed", "Used");
			for (int number = 1; number <= 6; number++)
			{
				Control c;
				c.type = Control::Type::SlotButton;
				c.box = DlgRect(SlotBoxes[number][0], SlotBoxes[number][1], SlotBoxes[number][2], SlotBoxes[number][3]);
				c.text = std::to_string(number) + " - " + (slots[number].empty ? empty : used);
				c.image = slots[number].empty ? thumbDown : slots[number].thumbnail;
				c.enabled = !slots[number].empty;
				c.onClick = [this, number] {
					sounds.Rollover();
					StartGame(false, number);
				};
				page.controls.push_back(c);
			}
			return page;
		}

		// IDA Game.exe: not exported: sub_10916080 [HP2 Game 0x10916080] (No-CD exe; the "IDC_JoystickConfig" string)
		// IDA Game.exe: not exported: sub_1091B0A0 [HP2 Game 0x1091B0A0] (No-CD exe; Joystick, the "joy.cpl" string)
		// Template 165: Video, Sound and Joystick; title hidden, no OK. Joystick opens Windows' game controller settings
		// (ShellExecute "control.exe joy.cpl"), which other systems don't have: it is shown disabled.
		Page FrontEndWindow::OptionsPage()
		{
			const char* section = "IDDIALOG_FrontEndPageConfig";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "Options");
			page.titleBox = DlgRect(24, 108, 271, 10);
			page.showTitle = false;
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");

			auto button = [&](int y, const char* key, const char* fallback, std::function<Page()> next) {
				Control c;
				c.type = Control::Type::Button;
				c.box = DlgRect(40, y, 85, 38);
				c.text = Localize("Startup", section, key, fallback);
				c.image = buttonUp;
				c.enabled = next != nullptr;
				if (next)
					c.onClick = [this, next] { sounds.Button(); Advance(next()); };
				page.controls.push_back(c);
			};
			button(124, "IDC_VideoConfig", "Video", [this] { return VideoPage(); });
			button(166, "IDC_SoundConfig", "Sound", [this] { return SoundPage(); });
			button(208, "IDC_JoystickConfig", "Joystick", nullptr);
			return page;
		}

		// IDA Game.exe: not exported: sub_10916810 [HP2 Game 0x10916810] (No-CD exe; the "IDC_D3DRenderer" string)
		// IDA Game.exe: not exported: sub_109187A0 [HP2 Game 0x109187A0] (No-CD exe; the "WFrontEndPageConfigVideo::OnBack" string, its OnOk)
		// Template 161: the renderer radios ([Engine.Engine] GameRenderDevice), the resolution list and "Run the game in a
		// window" ([WinDrv.WindowsClient] StartupFullscreen); "Disable Pentium III/3DNow" only shows with bDebugMode.
		// Game.exe lists 640x480, 800x600 and 1024x768 (1280x1024 with bDebugMode); Flipendo lists the common modes up to
		// the desktop (KW::ResolutionModes), as its in-game options page does. Flipendo has one renderer: Direct3D's
		// radio is on, Software's disabled. OK writes the settings and goes back; Back drops them.
		Page FrontEndWindow::VideoPage()
		{
			const char* section = "IDDIALOG_FrontEndPageConfigVideo";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "Video Options");
			page.titleBox = DlgRect(24, 124, 271, 10);
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");
			page.okText = Localize("HPMenu", "All", "Shared_Menu_0008", "OK");

			Control d3d;
			d3d.type = Control::Type::Radio;
			d3d.box = DlgRect(40, 143, 243, 10);
			d3d.text = Localize("Startup", section, "IDC_D3DRenderer", "Direct3D Renderer");
			d3d.checked = true;
			page.controls.push_back(d3d);

			Control software = d3d;
			software.box = DlgRect(40, 155, 243, 10);
			software.text = Localize("Startup", section, "IDC_SoftwareRenderer", "Software Renderer");
			software.checked = false;
			software.enabled = false;
			page.controls.push_back(software);

			Control label;
			label.type = Control::Type::Label;
			label.box = DlgRect(40, 172, 243, 12);
			label.text = Localize("Startup", section, "IDC_Resolution", "Desired Resolution");
			page.controls.push_back(label);

			// The current entry is the window size when the game runs in a window, else the full screen size
			USurrealClient* client = engine->client;
			int currentX = client->StartupFullscreen ? client->FullscreenViewportX : client->WindowedViewportX;
			int currentY = client->StartupFullscreen ? client->FullscreenViewportY : client->WindowedViewportY;
			Control list;
			list.type = Control::Type::ListBox;
			list.box = DlgRect(40, 184, 213, 35);
			auto modes = KW::ResolutionModes(GetDpiScale(), currentX, currentY);
			for (auto& mode : modes)
			{
				if (mode.first == currentX && mode.second == currentY)
					list.current = (int)list.items.size();
				list.items.push_back(std::to_string(mode.first) + "x" + std::to_string(mode.second));
			}
			list.top = std::max(list.current - 1, 0);
			page.controls.push_back(list);
			size_t listIndex = page.controls.size() - 1;

			Control window;
			window.type = Control::Type::CheckBox;
			window.box = DlgRect(40, 227, 243, 10);
			window.text = Localize("Startup", section, "IDC_Window", "Run the game in a window (rather than fullscreen)");
			window.checked = !client->StartupFullscreen;
			page.controls.push_back(window);
			size_t windowIndex = page.controls.size() - 1;

			page.onOk = [this, modes, listIndex, windowIndex] {
				USurrealClient* client = engine->client;
				const Page& page = CurrentPage();
				int current = page.controls[listIndex].current;
				if (current >= 0 && current < (int)modes.size())
				{
					client->FullscreenViewportX = modes[current].first;
					client->FullscreenViewportY = modes[current].second;
					client->WindowedViewportX = modes[current].first;
					client->WindowedViewportY = modes[current].second;
				}
				client->StartupFullscreen = !page.controls[windowIndex].checked;
				client->SaveConfig();
				engine->packages->SaveAllIniFiles();
				sounds.Rollover();
				Back();
			};
			return page;
		}

		// IDA Game.exe: not exported: sub_10919E40 [HP2 Game 0x10919E40] (No-CD exe; the "IDC_NoSound" string)
		// IDA Game.exe: not exported: sub_1091A950 [HP2 Game 0x1091A950] (No-CD exe; the "WFrontEndPageConfigSound::OnBack" string, its OnOk)
		// Template 159: "Disable all sound" is [Engine.GameEngine] UseSound turned around; the sliders (0 to 100, one tick
		// per step) are ALAudio's SoundVolume and MusicVolume times 100, with HPMenu's "Low" (Options_0005) and "High"
		// (Options_0002) under them. "Disable 3D sound hardware" is hidden. OK writes them and a rollover sound plays.
		Page FrontEndWindow::SoundPage()
		{
			const char* section = "IDDIALOG_FrontEndPageConfigSound";
			Page page;
			page.title = Localize("Startup", section, "IDC_Title", "Sound Options");
			page.titleBox = DlgRect(24, 122, 271, 10);
			page.instructions = Localize("Startup", section, "IDC_Instructions", "");
			page.okText = Localize("HPMenu", "All", "Shared_Menu_0008", "OK");

			USurrealAudioDevice* audio = engine->audiodev;
			Control noSound;
			noSound.type = Control::Type::CheckBox;
			noSound.box = DlgRect(34, 142, 209, 8);
			noSound.text = Localize("Startup", section, "IDC_NoSound", "Disable all sound");
			noSound.checked = SoundDisabled();
			page.controls.push_back(noSound);

			std::string low = Localize("HPMenu", "All", "Options_0005", "Low");
			std::string high = Localize("HPMenu", "All", "Options_0002", "High");
			auto volume = [&](int labelY, int sliderY, int lowHighY, const char* key, const char* fallback, uint8_t value) {
				Control label;
				label.type = Control::Type::Label;
				label.box = DlgRect(34, labelY, 280, 10);
				label.text = Localize("Startup", section, key, fallback);
				page.controls.push_back(label);

				Control slider;
				slider.type = Control::Type::Slider;
				slider.box = DlgRect(40, sliderY, 139, 15);
				slider.value = (int)std::lround(value * 100 / 255.0);
				page.controls.push_back(slider);

				label.box = DlgRect(40, lowHighY, 121, 10);
				label.text = low;
				page.controls.push_back(label);
				label.box = DlgRect(161, lowHighY, 153, 10);
				label.text = high;
				page.controls.push_back(label);
			};
			volume(172, 182, 198, "IDC_SoundVolume", "Sound Volume", audio->GetSoundVolume());
			size_t soundIndex = page.controls.size() - 3;
			volume(209, 220, 234, "IDC_MusicVolume", "Music Volume", audio->GetMusicVolume());
			size_t musicIndex = page.controls.size() - 3;

			page.onOk = [this, soundIndex, musicIndex] {
				const Page& page = CurrentPage();
				USurrealAudioDevice* audio = engine->audiodev;
				soundDisabled = page.controls[0].checked ? 1 : 0;
				engine->packages->SetIniValue("System", "Engine.GameEngine", "UseSound", soundDisabled ? "False" : "True");
				audio->SetSoundVolume((uint8_t)std::lround(page.controls[soundIndex].value * 255 / 100.0));
				audio->SetMusicVolume((uint8_t)std::lround(page.controls[musicIndex].value * 255 / 100.0));
				audio->SaveConfig();
				engine->packages->SaveAllIniFiles();
				sounds.Rollover();
				Back();
			};
			return page;
		}

		// IDA Window.dll: ?Advance@WFrontEndDialog@@UAEXPAVWFrontEndPage@@@Z [HP2 Window 0x1103FB00]
		// IDA Window.dll: ?RefreshPage@WFrontEndDialog@@UAEXXZ [HP2 Window 0x1103FC20]
		// Advance pushes the page and RefreshPage shows it: Back gets the page's back text only when there is a page to go
		// back to, OK the page's OK text; a button with no text is hidden. The page's OnCurrent puts its title in the
		// window title and its instructions in the dialog's label.
		void FrontEndWindow::Advance(Page page)
		{
			pages.push_back(std::move(page));
			SetWindowTitle(CurrentPage().title);
			pressedIndex = -1;
			Update();
		}

		// IDA Window.dll: ?OnBack@WFrontEndDialog@@UAEXXZ [HP2 Window 0x11040070]
		// IDA Game.exe: not exported: sub_10912B20 [HP2 Game 0x10912B20] (No-CD exe; WFrontEnd's OnBack, the vtable after WFrontEnd::OnDestroy's)
		// A button sound when there's a page to go back to, then the page under the current one.
		void FrontEndWindow::Back()
		{
			if (pages.size() > 1)
			{
				sounds.Button();
				pages.pop_back();
				SetWindowTitle(CurrentPage().title);
				pressedIndex = -1;
				Update();
			}
		}

		// IDA Window.dll: ?OnOk@WFrontEndDialog@@UAEXXZ [HP2 Window 0x1103FF90]
		// The current page's OnOk
		void FrontEndWindow::Ok()
		{
			auto onOk = CurrentPage().onOk;
			if (onOk)
				onOk();
		}

		// Game.exe keeps the dialog on screen while the sound plays to the end; here it goes away first, so the click
		// doesn't look ignored for the second and a half the sound lasts
		void FrontEndWindow::Quit()
		{
			Hide();
			DisplayWindow::ProcessEvents(); // sends the hide to the display before the wait
			sounds.Rollover(true);
			Finished = false;
			EndModal();
		}

		// The slot buttons set [FrontEnd] LaunchCode (1 New Game, 2 Load Game) and UseSaveSlot and end the dialog
		void FrontEndWindow::StartGame(bool newGame, int number)
		{
			Finished = true;
			NewGame = newGame;
			SlotNumber = number;
			EndModal();
		}

		// X11 fixes a window that can't be resized at its size in pixels as the window knows it, which is 0 until the
		// display has reported the first geometry: lock it then, not right after Show
		void FrontEndWindow::OnGeometryChanged()
		{
			if (!sizeLocked && GetWidth() >= ClientWidth - 1.0 && GetHeight() >= ClientHeight - 1.0)
			{
				sizeLocked = true;
				SetWindowResizable(false);
			}
		}

		void FrontEndWindow::OnClose()
		{
			Finished = false;
			EndModal();
		}

		Control* FrontEndWindow::ControlByIndex(int index)
		{
			if (index == 0)
				return pages.size() > 1 ? &backButton : nullptr;
			if (index == 1)
				return CurrentPage().okText.empty() ? nullptr : &okButton;
			if (index >= 2 && index - 2 < (int)CurrentPage().controls.size())
				return &CurrentPage().controls[index - 2];
			return nullptr;
		}

		Control* FrontEndWindow::ControlAt(const Point& pos, int& index)
		{
			int count = 2 + (int)CurrentPage().controls.size();
			for (index = 0; index < count; index++)
			{
				Control* c = ControlByIndex(index);
				if (c && c->type != Control::Type::Label && c->enabled && c->box.contains(pos))
					return c;
			}
			index = -1;
			return nullptr;
		}

		void FrontEndWindow::SetSlider(Control& control, double x)
		{
			// The knob's centre follows the mouse along the bar
			double knob = sliderKnob ? sliderKnob->GetWidth() : 11.0;
			double left = control.box.x + knob * 0.5;
			double width = std::max(control.box.width - knob, 1.0);
			control.value = (int)std::lround(std::clamp((x - left) / width, 0.0, 1.0) * 100.0);
			Update();
		}

		bool FrontEndWindow::OnMouseDown(const Point& pos, InputKey key)
		{
			if (key != InputKey::LeftMouse)
				return false;
			int index;
			Control* c = ControlAt(pos, index);
			if (!c)
				return true;

			if (c->type == Control::Type::ListBox)
			{
				double rowHeight = 16.0;
				int row = c->top + (int)((pos.y - c->box.y - 1.0) / rowHeight);
				if (row >= 0 && row < (int)c->items.size())
					c->current = row;
				Update();
				return true;
			}

			// No SetPointerCapture: on X11 it is a cursor lock (hidden, pulled back to the window's centre on every move),
			// so the button would be released somewhere else
			pressedIndex = index;
			if (c->type == Control::Type::Slider)
			{
				sliderDrag = true;
				SetSlider(*c, pos.x);
			}
			Update();
			return true;
		}

		bool FrontEndWindow::OnMouseUp(const Point& pos, InputKey key)
		{
			if (key != InputKey::LeftMouse || pressedIndex < 0)
				return false;
			int index = pressedIndex;
			pressedIndex = -1;
			sliderDrag = false;

			Control* c = ControlByIndex(index);
			if (c && c->box.contains(pos))
			{
				if (c->type == Control::Type::CheckBox)
				{
					c->checked = !c->checked;
				}
				else if (c->type == Control::Type::Radio)
				{
					// The renderer radios play a button sound
					for (Control& other : CurrentPage().controls)
					{
						if (other.type == Control::Type::Radio)
							other.checked = &other == c;
					}
					sounds.Button();
				}
				else if (c->onClick)
				{
					auto onClick = c->onClick; // may replace the page the control belongs to
					onClick();
				}
			}
			Update();
			return true;
		}

		bool FrontEndWindow::OnMouseWheel(const Point& pos, InputKey key)
		{
			int index;
			Control* c = ControlAt(pos, index);
			if (c && c->type == Control::Type::ListBox)
			{
				int visible = std::max((int)(c->box.height / 16.0), 1);
				int maxTop = std::max((int)c->items.size() - visible, 0);
				c->top = std::clamp(c->top + (key == InputKey::MouseWheelUp ? -1 : 1), 0, maxTop);
				Update();
				return true;
			}
			return false;
		}

		void FrontEndWindow::OnMouseMove(const Point& pos)
		{
			if (sliderDrag && pressedIndex >= 0)
			{
				if (Control* c = ControlByIndex(pressedIndex))
					SetSlider(*c, pos.x);
			}
		}

		// Without a capture the release outside the window never arrives: leaving ends the press (and a slider drag)
		void FrontEndWindow::OnMouseLeave()
		{
			if (pressedIndex >= 0)
			{
				pressedIndex = -1;
				sliderDrag = false;
				Update();
			}
		}

		void FrontEndWindow::OnKeyDown(InputKey key)
		{
			// Escape is the dialog's IDCANCEL: back, or quit on the first page; Enter is OK when the page has one
			if (key == InputKey::Escape)
			{
				if (pages.size() > 1)
					Back();
				else
					Quit();
			}
			else if (key == InputKey::Enter && !CurrentPage().okText.empty())
			{
				Ok();
			}
		}

		void FrontEndWindow::DrawText(Canvas* canvas, const Rect& box, const std::string& text, const Colorf& color, bool center)
		{
			// DT_WORDBREAK for labels; buttons draw one line centred both ways (DT_CENTER | DT_VCENTER | DT_SINGLELINE)
			SpanLayout layout;
			layout.AddText(text, font, color);
			layout.Layout(canvas, center ? 10000.0 : box.width);
			Size size = layout.GetSize();
			double x = center ? box.x + (box.width - size.width) * 0.5 : box.x;
			double y = center ? box.y + (box.height - size.height) * 0.5 : box.y;
			layout.SetPosition(Point(x, y));
			layout.DrawLayout(canvas);
		}

		// IDA Window.dll: ?OnDrawItem@WBitmapButton@@UAEXPAUtagDRAWITEMSTRUCT@@@Z [HP2 Window 0x11017B70]
		// IDA Window.dll: ?DrawItemText@WCoolButton@@QAEXPAUtagDRAWITEMSTRUCT@@@Z [HP2 Window 0x11016B20]
		// The bitmap centred on the button and the text centred over it, in the button's colour (white, also when
		// disabled); pressed, both move down and right by one pixel.
		void FrontEndWindow::DrawButton(Canvas* canvas, const Control& control, bool pressed)
		{
			double offset = pressed ? 1.0 : 0.0;
			Rect box = control.box;
			if (control.image)
			{
				double w = control.image->GetWidth(), h = control.image->GetHeight();
				canvas->drawImage(control.image, Rect::xywh(std::round(box.x + (box.width - w) * 0.5 + offset), std::round(box.y + (box.height - h) * 0.5 + offset), w, h));
			}
			// Joystick (no page here) is the one button Flipendo disables that the original doesn't: its text greys
			bool grey = !control.enabled && control.type == Control::Type::Button;
			DrawText(canvas, Rect::xywh(box.x + offset, box.y + offset, box.width, box.height), control.text, grey ? DisabledTextColor : TextColor, true);
		}

		void FrontEndWindow::DrawControl(Canvas* canvas, const Control& control, bool pressed)
		{
			const Colorf& color = control.enabled ? TextColor : DisabledTextColor;
			switch (control.type)
			{
			case Control::Type::Button:
			case Control::Type::SlotButton:
				DrawButton(canvas, control, pressed);
				break;

			case Control::Type::Label:
				DrawText(canvas, control.box, control.text, color, false);
				break;

			// IDA Window.dll: ?OnDrawItem@WBitmapCheckBox@@UAEXPAUtagDRAWITEMSTRUCT@@@Z [HP2 Window 0x11013D70]
			// The on or off bitmap at the left, the text 5 pixels after it (WBitmapCheckBox::PaintText)
			case Control::Type::CheckBox:
			case Control::Type::Radio:
			{
				bool radio = control.type == Control::Type::Radio;
				std::shared_ptr<Image> image = radio ? (control.checked ? radioOn : radioOff) : (control.checked ? checkOn : checkOff);
				double w = image ? image->GetWidth() : 12.0, h = image ? image->GetHeight() : 12.0;
				double y = std::round(control.box.y + (control.box.height - h) * 0.5);
				if (image)
					canvas->drawImage(image, Rect::xywh(control.box.x, y, w, h));
				DrawText(canvas, Rect::xywh(control.box.x + w + 5.0, control.box.y - 2.0, control.box.width - w - 5.0, control.box.height + 4.0), control.text, color, false);
				break;
			}

			// A WColorTrackBar drawn with HP2Slider.bmp and HP2SliderKnob.bmp
			case Control::Type::Slider:
			{
				if (slider)
				{
					double w = slider->GetWidth(), h = slider->GetHeight();
					canvas->drawImage(slider, Rect::xywh(std::round(control.box.x + (control.box.width - w) * 0.5), std::round(control.box.y + (control.box.height - h) * 0.5), w, h));
				}
				if (sliderKnob)
				{
					double w = sliderKnob->GetWidth(), h = sliderKnob->GetHeight();
					double x = control.box.x + (control.box.width - w) * control.value / 100.0;
					canvas->drawImage(sliderKnob, Rect::xywh(std::round(x), std::round(control.box.y + (control.box.height - h) * 0.5), w, h));
				}
				break;
			}

			// A plain Windows list box (LBS_NOTIFY | WS_VSCROLL): one row per mode, the current one highlighted
			case Control::Type::ListBox:
			{
				canvas->fillRect(control.box, Colorf::fromRgba8(12, 11, 9));
				canvas->fillRect(Rect::xywh(control.box.x, control.box.y, control.box.width, 1.0), DisabledTextColor);
				canvas->fillRect(Rect::xywh(control.box.x, control.box.y + control.box.height - 1.0, control.box.width, 1.0), DisabledTextColor);
				canvas->fillRect(Rect::xywh(control.box.x, control.box.y, 1.0, control.box.height), DisabledTextColor);
				canvas->fillRect(Rect::xywh(control.box.x + control.box.width - 1.0, control.box.y, 1.0, control.box.height), DisabledTextColor);
				canvas->pushClip(control.box);
				double rowHeight = 16.0;
				for (int i = control.top; i < (int)control.items.size(); i++)
				{
					Rect row = Rect::xywh(control.box.x + 1.0, control.box.y + 1.0 + (i - control.top) * rowHeight, control.box.width - 2.0, rowHeight);
					if (row.y >= control.box.y + control.box.height)
						break;
					if (i == control.current)
						canvas->fillRect(row, ListSelection);
					DrawText(canvas, Rect::xywh(row.x + 4.0, row.y, row.width - 4.0, row.height), control.items[i], TextColor, false);
				}
				canvas->popClip();
				break;
			}
			}
		}

		// IDA Window.dll: ?OnPaint@WFrontEndDialog@@UAEXXZ [HP2 Window 0x1103FF00]
		// IDA Window.dll: ?OnPaint@WFrontEndPage@@UAEXXZ [HP2 Window 0x1103ED00]
		// The dialog fills itself with its colour; the page (placed on control 1061, 319x252) blits Background.bmp at its
		// top left, clipped to its size.
		void FrontEndWindow::OnPaint(Canvas* canvas)
		{
			canvas->fillRect(Rect::xywh(0.0, 0.0, GetWidth(), GetHeight()), DialogBackground);

			Rect pageBox = DlgRect(0, 0, 319, 252);
			if (background)
			{
				canvas->pushClip(pageBox);
				canvas->drawImage(background, Rect::xywh(0.0, 0.0, background->GetWidth(), background->GetHeight()));
				canvas->popClip();
			}

			Page& page = CurrentPage();
			if (page.showTitle)
				DrawText(canvas, page.titleBox, page.title, TextColor, false);
			for (size_t i = 0; i < page.controls.size(); i++)
				DrawControl(canvas, page.controls[i], pressedIndex == (int)i + 2);

			okButton.text = page.okText;
			if (pages.size() > 1)
				DrawControl(canvas, backButton, pressedIndex == 0);
			if (!page.okText.empty())
				DrawControl(canvas, okButton, pressedIndex == 1);

			// Dialog 152's Instructions label (3022)
			DrawText(canvas, DlgRect(24, 301, 272, 28), page.instructions, TextColor, false);
		}
	}

	// IDA Game.exe: not exported: _WinMain@16 [HP2 Game 0x1090B700] (No-CD exe; the "EASplashWaitTime" string)
	// IDA Game.exe: not exported: sub_10902BA0 [HP2 Game 0x10902BA0] (No-CD exe; the "PrivetDr.unr " string)
	// IDA Game.exe: not exported: sub_10903290 [HP2 Game 0x10903290] (No-CD exe; the " -LOADGAME=0 " string)
	// IDA Core.dll: ?appInit@@YAXPBG0PAVFMalloc@@PAVFOutputDevice@@PAVFOutputDeviceError@@PAVFFeedbackContext@@PAVFFileManager@@P6APAVFConfigCache@@XZH@Z [HP2 Core 0x10156850]
	// IDA Engine.dll: ?Init@UGameEngine@@UAEXXZ [HP2 0x103A0FA0]
	// Engine::Run, before the game window opens, for a run without a URL. Game.exe without -SAVESLOT or -NOFRONTEND
	// (and without -LOG) shows ..\Help\EALogo<Language>.bmp (else EALogo1.bmp) for [FrontEnd] EASplashWaitTime seconds,
	// then WBLegal<Language>.bmp (else WBLegal.bmp) for WBSplashWaitTime, then the dialog. When the dialog has set
	// [FrontEnd] LaunchCode, Game.exe starts itself again:
	//   1 (New Game in slot n):  PrivetDr.unr -SAVESLOT=n
	//   2 (Load Game slot n):    -LOADGAME=0 -SAVESLOT=n (the Startup map, then the slot's Save0.usa: LoadGameSave())
	// and that run's appInit makes SavePath\Slot<n> the save folder. Here the same run goes on with both.
	bool RunFrontEnd()
	{
		if (!HP1::ModsSkipSplash()) // src/hp1/mods/LaunchSkips.cpp
		{
			std::string language = engine->packages->GetLanguage();
			int eaSeconds = std::atoi(engine->packages->GetIniValue("System", "FrontEnd", "EASplashWaitTime", "3").c_str());
			int wbSeconds = std::atoi(engine->packages->GetIniValue("System", "FrontEnd", "WBSplashWaitTime", "3").c_str());
			if (!ShowSplash("EALogo" + language + ".bmp", "EALogo1.bmp", eaSeconds) || !ShowSplash("WBLegal" + language + ".bmp", "WBLegal.bmp", wbSeconds))
				return false;
		}

		auto window = std::make_unique<FrontEndWindow>();
		Size screen = Widget::GetScreenSize();
		window->SetFrameGeometry((screen.width - ClientWidth) * 0.5, (screen.height - ClientHeight) * 0.5, ClientWidth, ClientHeight);
		window->Show();
		window->RunModal();

		bool finished = window->Finished;
		bool newGame = window->NewGame;
		int slot = window->SlotNumber;
		window.reset();

		if (!finished)
		{
			LogMessage("HP2 front end: Quit");
			return false;
		}

		fs::path folder = engine->packages->GetSaveFolderPath() / ("Slot" + std::to_string(slot));
		std::error_code ec;
		fs::create_directories(folder, ec);
		engine->packages->SetSaveFolderPath(folder);
		LogMessage("Save Slot Path: " + folder.string());

		if (newGame)
		{
			engine->LaunchInfo.url = NewGameURL;
			loadGameSave = -1;
		}
		else
		{
			loadGameSave = 0;
		}
		return true;
	}
}
