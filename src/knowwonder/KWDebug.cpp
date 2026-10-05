#include "Precomp.h"
#include "KW.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/UConsole.h"
#include "KWActor.h"
#include "KWCheck.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/UHUD.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "Collision/TopLevel/CollisionSystem.h"
#include "GameWindow.h"
#include <chrono>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

// Development aids, all timed in seconds since the first rendered frame:
//   HP1_SHOTS="4.5,5.7,8"      in-engine screenshots (no desktop capture)
//   HP1_SHOT_DIR=<dir>         screenshot directory (default: current directory), files hp1shot_<sec>.bmp
//   HP1_KEYS="30:W:4,36:Up:1"  press <key> at <sec> and hold it for <dur> seconds. Keys: a letter or
//                              digit, Up/Down/Left/Right, Space, Shift, Ctrl, Enter, Escape, or a number
//                              (EInputKey value)
//   HP1_MOUSE="60:0:-40:1"      from <sec>, for <dur> seconds, move the mouse by <dx>,<dy> raw counts every
//                              frame (same path as real raw mouse input; dy>0 is towards the user)
//   HP1_TRACE="Harry,gen_"     log every actor whose name starts with one of these, every 0.5 s
//   HP1_CAMERA="x,y,z,p,y"     fixed camera location and rotation (pitch/yaw in Unreal units), from the first frame
//   HP1_DUMP="5,70"            log every actor (class, name, state, location, Tag, Event) at these times
//   HP1_GOTO="60:x,y;x,y"      from <sec>, steer the player to each waypoint in turn (turns the view, holds Up);
//                              several runs separated by '|'. A waypoint "x,y,J" jumps (Ctrl) on arrival, "x,y,w2" stops
//                              and waits 2 s. Logs arrival and when the player is stuck
//   HP1_SKIPCUTS=1             press Space whenever a cutscene holds Harry (the CutsceneSkip mod fast-forwards it;
//                              does nothing with --vanilla). Shifts later timings, so routes need their own times
//   HP1_HEIGHTMAP="5:x0,y0,x1,y1,step,ztop"  at <sec>, trace straight down (player-sized cylinder) from ztop over
//                              the grid and log one row of floor heights per y (blank = nothing within 2000 units)
//   HP1_BACKGROUND=1           open the game window windowed, at the bottom of the window stack and without activating
//                              it, so automated runs don't take over the screen (keys/mouse come from HP1_KEYS etc.)
//   HP1_EXEC="40:open save0.usa;90:SaveGame 3"  run a console command at <sec> (';' separates entries);
//                              "@console Fn" calls the console's script function Fn() instead (e.g. SaveSelectedSlot);
//                              "@console.MenuBook OpenBook Slot" follows object properties and passes one string;
//                              "@set CutScene bDebugScript True" sets a property on every actor whose name starts so; "@get harry numBeans" logs one
//                              "@teleport x y z" moves the player; "@trigger <tag>" triggers every actor with that Tag;
//                              "@bump GridMover0 harry0" raises Bump(harry0) on the actors whose name starts with GridMover0;
//                              "@polys Mover17" logs the brush polygons (normal, PolyFlags, texture) of the matching movers;
//                              "@sweep x y z x y z ex ey ez" runs KnowWonder's BSP line/point checks and the actor sweep with that box and logs them;
//                              "@hull Mover0" logs the brush's collision hulls (planes in world space)

namespace KW
{
	bool ShowWindowInBackground(GameWindow* window, int width, int height)
	{
		const char* s = getenv("HP1_BACKGROUND");
		if (!s || !*s || *s == '0')
			return false;
#ifdef _WIN32
		HWND hwnd = (HWND)window->GetNativeHandle();
		SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		LogMessage("HP1_BACKGROUND: window shown without activation");
		return true;
#else
		return false;
#endif
	}

	static float SecondsSinceFirstFrame()
	{
		static auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}

	static void TickDebugKeys(float now)
	{
		struct KeyPress { float Time; int Key; float Duration; bool Down = false; bool Done = false; };
		static bool parsed = false;
		static Array<KeyPress> presses;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_KEYS"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
				{
					std::stringstream parts(item);
					std::string t, k, d;
					if (!std::getline(parts, t, ':') || !std::getline(parts, k, ':'))
						continue;
					std::getline(parts, d, ':');
					int key = 0;
					static const std::pair<const char*, int> names[] = {
						{ "Up", 0x26 }, { "Down", 0x28 }, { "Left", 0x25 }, { "Right", 0x27 }, { "Space", 0x20 },
						{ "Shift", 0x10 }, { "Ctrl", 0x11 }, { "Enter", 0x0D }, { "Escape", 0x1B } };
					for (auto& n : names)
						if (k == n.first) key = n.second;
					if (!key && k.size() == 1 && isalnum((unsigned char)k[0]))
						key = toupper((unsigned char)k[0]);
					if (!key)
						key = std::atoi(k.c_str());
					if (key > 0 && key < 256)
						presses.push_back({ std::stof(t), key, d.empty() ? 0.1f : std::stof(d) });
				}
			}
		}
		for (KeyPress& p : presses)
		{
			if (!p.Down && !p.Done && now >= p.Time)
			{
				p.Down = true;
				engine->OnWindowKeyDown((EInputKey)p.Key);
				LogMessage("HP1 key down " + std::to_string(p.Key));
			}
			else if (p.Down && now >= p.Time + p.Duration)
			{
				p.Down = false;
				p.Done = true;
				engine->OnWindowKeyUp((EInputKey)p.Key);
				LogMessage("HP1 key up " + std::to_string(p.Key));
			}
		}
	}

	static void TickDebugMouse(float now)
	{
		struct MouseMove { float Time; int DX, DY; float Duration; };
		static bool parsed = false;
		static Array<MouseMove> moves;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_MOUSE"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
				{
					float t = 0.0f, d = 0.0f;
					int dx = 0, dy = 0;
					if (sscanf(item.c_str(), "%f:%d:%d:%f", &t, &dx, &dy, &d) == 4)
						moves.push_back({ t, dx, dy, d });
				}
			}
		}
		for (const MouseMove& m : moves)
			if (now >= m.Time && now < m.Time + m.Duration)
				engine->OnWindowRawMouseMove(m.DX, m.DY);
	}

	static void TickDebugTrace(float now)
	{
		static bool parsed = false;
		static Array<std::string> prefixes;
		static float next = 0.0f;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_TRACE"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
					if (!item.empty()) prefixes.push_back(item);
			}
		}
		if (prefixes.empty() || now < next || !engine->Level)
			return;
		next = now + 0.5f;

		for (UActor* a : engine->Level->Actors)
		{
			if (!a)
				continue;
			std::string name = a->Name.ToString();
			bool match = false;
			for (const std::string& p : prefixes)
				match = match || name.compare(0, p.size(), p) == 0;
			if (!match)
				continue;

			char buf[1000];
			int n = snprintf(buf, sizeof(buf), "HP1 trace t=%.1f %s state=%s zone=%d loc=(%.0f,%.0f,%.0f) vel=(%.0f,%.0f,%.0f) acc=(%.0f,%.0f) phys=%d rot=%d pitch=%d drot=%d anim=%s rate=%.2f frame=%.2f tween=%.2f",
				now, name.c_str(), a->GetStateName().ToString().c_str(), (int)a->Region().ZoneNumber, a->Location().x, a->Location().y, a->Location().z,
				a->Velocity().x, a->Velocity().y, a->Velocity().z, a->Acceleration().x, a->Acceleration().y, (int)a->Physics(),
				a->Rotation().Yaw & 0xffff, a->Rotation().Pitch & 0xffff, a->DesiredRotation().Yaw & 0xffff, a->AnimSequence().ToString().c_str(), a->AnimRate(), a->AnimFrame(), TweenAlpha(a));
			if (UPawn* pawn = UObject::TryCast<UPawn>(a))
				n += snprintf(buf + n, sizeof(buf) - n, " ground=%.0f desired=%.2f rrate=%d walking=%d",
					pawn->GroundSpeed(), pawn->DesiredSpeed(), pawn->RotationRate().Yaw, (int)pawn->bIsWalking());
			if (UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(a))
				snprintf(buf + n, sizeof(buf) - n, " bRun=%d bDuck=%d aForward=%.0f aBaseY=%.0f view=(%d,%d) smoothY=%.0f aLookUp=%.0f",
					(int)player->bRun(), (int)player->bDuck(), player->aForward(), player->aBaseY(),
					player->ViewRotation().Pitch & 0xffff, player->ViewRotation().Yaw & 0xffff, player->SmoothMouseY(), player->aLookUp());
			LogMessage(buf);
		}
	}

	static void TickDebugDump(float now)
	{
		static bool parsed = false;
		static Array<float> times;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_DUMP"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
					if (!item.empty()) times.push_back(std::stof(item));
			}
		}
		if (times.empty() || now < times.front() || !engine->Level)
			return;
		times.erase(times.begin());

		LogMessage("HP1 dump t=" + std::to_string(now));
		for (UActor* a : engine->Level->Actors)
		{
			if (!a)
				continue;
			char buf[512];
			snprintf(buf, sizeof(buf), "HP1 dump %s %s state=%s loc=(%.0f,%.0f,%.0f) tag=%s event=%s",
				a->Class->Name.ToString().c_str(), a->Name.ToString().c_str(), a->GetStateName().ToString().c_str(),
				a->Location().x, a->Location().y, a->Location().z, a->Tag().ToString().c_str(), a->Event().ToString().c_str());
			LogMessage(buf);
		}
	}

	static void TickDebugExec(float now)
	{
		struct Command { float Time; std::string Text; };
		static bool parsed = false;
		static Array<Command> commands;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_EXEC"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ';'))
				{
					size_t colon = item.find(':');
					if (colon != std::string::npos)
						commands.push_back({ std::stof(item.substr(0, colon)), item.substr(colon + 1) });
				}
			}
		}
		if (commands.empty() || now < commands.front().Time || !engine->viewport || !engine->viewport->Actor())
			return;
		std::string text = commands.front().Text;
		commands.erase(commands.begin());

		LogMessage("HP1 exec t=" + std::to_string(now) + ": " + text);
		if (text.rfind("@teleport ", 0) == 0)
		{
			// "@teleport x y z": move the player there (touches whatever is at the new spot, like a real move would not skip).
			// The game camera (HPBase.BaseCam) moves by the same offset, with its buffer of the player's past locations it
			// steers by, so it doesn't fly to the new spot through the walls.
			vec3 pos;
			if (sscanf(text.c_str() + 10, "%f %f %f", &pos.x, &pos.y, &pos.z) == 3)
			{
				UPlayerPawn* player = engine->viewport->Actor();
				vec3 old = player->Location();
				bool ok = player->SetLocation(pos);
				LogMessage(std::string("HP1 exec: teleport ") + (ok ? "ok" : "blocked"));
				UClass* camClass = engine->packages->FindClass("HPBase.BaseCam");
				if (ok && camClass)
				{
					vec3 delta = player->Location() - old;
					size_t previous = camClass->GetPropertyDataOffset("previousLocations").DataOffset;
					for (UActor* a : engine->Level->Actors)
					{
						if (!a || !a->IsA(camClass->Name))
							continue;
						a->SetLocation(a->Location() + delta);
						for (int i = 0; i < 16; i++) // BaseCam: var vector previousLocations[16]
						{
							PropertyDataOffset element;
							element.DataOffset = previous + i * sizeof(vec3);
							a->Value<vec3>(element) += delta;
						}
					}
				}
			}
			return;
		}
		if (text.rfind("@trigger ", 0) == 0)
		{
			// "@trigger <tag>": Trigger every actor with that Tag, instigated by the player (what Actor.TriggerEvent does).
			NameString tag(text.substr(9));
			UPlayerPawn* player = engine->viewport->Actor();
			int count = 0;
			for (UActor* a : engine->Level->Actors)
			{
				if (a && a->Tag() == tag)
				{
					CallEvent(a, EventName::Trigger, { ExpressionValue::ObjectValue(player), ExpressionValue::ObjectValue(player) });
					count++;
				}
			}
			LogMessage("HP1 exec: triggered " + std::to_string(count) + " actors");
			return;
		}
		if (text.rfind("@bump ", 0) == 0)
		{
			// "@bump <actor name prefix> <other actor name>": raise Bump(other) on every matching actor (push a mover).
			std::stringstream parts(text.substr(6));
			std::string prefix, otherName;
			parts >> prefix >> otherName;
			UActor* other = nullptr;
			for (UActor* a : engine->Level->Actors)
				if (a && a->Name.ToString() == otherName)
					other = a;
			int count = 0;
			for (UActor* a : engine->Level->Actors)
			{
				if (other && a && a->Name.ToString().compare(0, prefix.size(), prefix) == 0)
				{
					CallEvent(a, EventName::Bump, { ExpressionValue::ObjectValue(other) });
					count++;
				}
			}
			LogMessage("HP1 exec: bumped " + std::to_string(count) + " actors");
			return;
		}
		if (text.rfind("@polys ", 0) == 0)
		{
			// "@polys <actor name prefix>": log every brush polygon of the matching actors (which sides are mountable).
			std::string prefix = text.substr(7);
			for (UActor* a : engine->Level->Actors)
			{
				if (!a || a->Name.ToString().compare(0, prefix.size(), prefix) != 0 || !a->Brush() || !a->Brush()->Polys)
					continue;
				int i = 0;
				for (const Poly& poly : a->Brush()->Polys->Polys)
				{
					char buf[256];
					snprintf(buf, sizeof(buf), "HP1 polys %s %d: normal=(%.2f,%.2f,%.2f) flags=0x%08x tex=%s", a->Name.ToString().c_str(), i++,
						poly.Normal.x, poly.Normal.y, poly.Normal.z, poly.PolyFlags, poly.Texture ? poly.Texture->Name.ToString().c_str() : "None");
					LogMessage(buf);
				}
			}
			return;
		}
		if (text.rfind("@sweep ", 0) == 0)
		{
			// "@sweep sx sy sz ex ey ez bx by bz": KnowWonder's level checks from start to end with a box (debugging collision)
			float v[9] = {};
			if (sscanf(text.c_str() + 7, "%f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9)
			{
				vec3 start(v[0], v[1], v[2]), end(v[3], v[4], v[5]), extent(v[6], v[7], v[8]);
				CheckResult hit;
				bool clear = ModelLineCheck(hit, ModelFrame::Level(engine->Level->Model), end, start, extent, 0);
				char buf[400];
				snprintf(buf, sizeof(buf), "HP1 sweep line: %s time=%.4f loc=(%.2f,%.2f,%.2f) normal=(%.3f,%.3f,%.3f) item=%d", clear ? "clear" : "hit", hit.Time, hit.Location.x, hit.Location.y, hit.Location.z, hit.Normal.x, hit.Normal.y, hit.Normal.z, hit.Item);
				LogMessage(buf);
				for (const vec3& p : { start, end })
				{
					CheckResult ph;
					bool free = ModelPointCheck(ph, ModelFrame::Level(engine->Level->Model), p, extent, 0);
					PointRegion region = engine->Level->Model->FindRegion(p, engine->LevelInfo);
					snprintf(buf, sizeof(buf), "HP1 sweep point (%.2f,%.2f,%.2f): %s zone=%d leaf=%d push=(%.2f,%.2f,%.2f) normal=(%.3f,%.3f,%.3f)", p.x, p.y, p.z, free ? "free" : "blocked", region.ZoneNumber, region.BspLeaf, ph.Location.x, ph.Location.y, ph.Location.z, ph.Normal.x, ph.Normal.y, ph.Normal.z);
					LogMessage(buf);
				}
				for (const CheckResult& h : MultiLineCheck(end, start, extent, true, engine->LevelInfo, 0))
				{
					snprintf(buf, sizeof(buf), "HP1 sweep actor %s time=%.4f loc=(%.2f,%.2f,%.2f) normal=(%.3f,%.3f,%.3f)", h.Actor ? h.Actor->Name.ToString().c_str() : "none", h.Time, h.Location.x, h.Location.y, h.Location.z, h.Normal.x, h.Normal.y, h.Normal.z);
					LogMessage(buf);
				}
			}
			return;
		}
		if (text.rfind("@hull ", 0) == 0)
		{
			// "@hull <brush actor>": every collision leaf hull of the brush, its planes in world space and its box
			std::string name = text.substr(6);
			for (UActor* a : engine->Level->Actors)
			{
				if (!a || a->Name.ToString() != name || !a->Brush())
					continue;
				ModelFrame frame = ModelFrame::Brush(a);
				UModel* model = a->Brush();
				char buf[400];
				for (int node = 0; node < (int)model->Nodes.size(); node++)
				{
					int bound = model->Nodes[node].CollisionBound;
					if (bound == -1)
						continue;
					const int32_t* list = &model->LeafHulls[bound];
					int count = 0;
					for (; list[count] != -1; count++)
					{
						int index = list[count];
						const BspNode& n = model->Nodes[index & ~0x40000000];
						vec4 w = frame.WorldPlane(vec4(n.PlaneX, n.PlaneY, n.PlaneZ, n.PlaneW));
						if (index & 0x40000000)
							w = vec4(-w.x, -w.y, -w.z, -w.w);
						snprintf(buf, sizeof(buf), "HP1 hull %s node=%d plane %d (node %d%s): (%.3f,%.3f,%.3f,%.2f)", a->Name.ToString().c_str(), node, count, index & ~0x40000000, (index & 0x40000000) ? " flipped" : "", w.x, w.y, w.z, w.w);
						LogMessage(buf);
					}
					const float* box = (const float*)&list[count + 1];
					snprintf(buf, sizeof(buf), "HP1 hull %s node=%d box (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)", a->Name.ToString().c_str(), node, box[0], box[1], box[2], box[3], box[4], box[5]);
					LogMessage(buf);
				}
			}
			return;
		}
		if (text.rfind("@get ", 0) == 0)
		{
			// "@get <actor name prefix> <property>": log a property of every matching actor.
			std::stringstream parts(text.substr(5));
			std::string prefix, prop;
			parts >> prefix >> prop;
			for (UActor* a : engine->Level->Actors)
				if (a && a->Name.ToString().compare(0, prefix.size(), prefix) == 0)
					LogMessage("HP1 get " + a->Name.ToString() + "." + prop + " = " + a->GetPropertyAsString(NameString(prop)));
			return;
		}
		if (text.rfind("@set ", 0) == 0)
		{
			// "@set <actor name prefix> <property> <value>": set a property on every matching actor.
			std::stringstream parts(text.substr(5));
			std::string prefix, prop, value;
			parts >> prefix >> prop;
			std::getline(parts >> std::ws, value);
			int count = 0;
			for (UActor* a : engine->Level->Actors)
			{
				if (a && a->Name.ToString().compare(0, prefix.size(), prefix) == 0)
				{
					a->SetPropertyFromString(NameString(prop), value);
					count++;
				}
			}
			LogMessage("HP1 exec: set " + prop + " on " + std::to_string(count) + " actors");
			return;
		}
		if (text.rfind("@console", 0) == 0)
		{
			// "@console[.Prop[.Prop]] Fn [string arg]": follow object properties from the console, then call Fn.
			std::stringstream parts(text);
			std::string path, fn, arg;
			parts >> path >> fn;
			std::getline(parts >> std::ws, arg);
			UObject* obj = engine->console;
			std::stringstream props(path.substr(8));
			std::string prop;
			while (obj && std::getline(props, prop, '.'))
				if (!prop.empty()) obj = obj->GetUObject(prop);
			if (!obj || fn.empty())
				LogMessage("HP1 exec: no object for " + path);
			else if (arg.empty())
				CallEvent(obj, NameString(fn));
			else
				CallEvent(obj, NameString(fn), { ExpressionValue::StringValue(arg) });
			return;
		}
		uint32_t foundBits = 0;
		BitfieldBool found{ &foundBits, 1 };
		engine->ConsoleCommand(engine->viewport->Actor(), text, found);
	}

	static void TickDebugHeightmap(float now)
	{
		static bool parsed = false;
		static float when = -1.0f;
		static float x0, y0, x1, y1, step, ztop;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_HEIGHTMAP"))
			{
				if (sscanf(s, "%f:%f,%f,%f,%f,%f,%f", &when, &x0, &y0, &x1, &y1, &step, &ztop) != 7 || step <= 0.0f)
					when = -1.0f;
			}
		}
		if (when < 0.0f || now < when || !engine->Level)
			return;
		when = -1.0f;

		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		vec3 extent = player ? vec3(player->CollisionRadius(), player->CollisionRadius(), player->CollisionHeight()) : vec3(17.0f, 17.0f, 39.0f);
		TraceFlags flags;
		flags.world = true;
		flags.movers = true;
		LogMessage("HP1 heightmap x " + std::to_string((int)x0) + ".." + std::to_string((int)x1) + " step " + std::to_string((int)step) + " (player center z)");
		for (float y = y1; y >= y0; y -= step)
		{
			std::string row = "HP1 heightmap y=" + std::to_string((int)y) + ":";
			for (float x = x0; x <= x1; x += step)
			{
				vec3 from(x, y, ztop), to(x, y, ztop - 2000.0f);
				CollisionHit hit = engine->Level->Collision.TraceFirstHit(from, to, player, extent, flags);
				char cell[16];
				if (hit.Fraction < 1.0f)
					snprintf(cell, sizeof(cell), " %5d", (int)(from.z + (to.z - from.z) * hit.Fraction));
				else
					snprintf(cell, sizeof(cell), "      ");
				row += cell;
			}
			LogMessage(row);

			// The same column traced with KnowWonder's own BSP check (level geometry only), to compare
			std::string kwRow = "HP1 heightmap kw y=" + std::to_string((int)y) + ":";
			for (float x = x0; x <= x1; x += step)
			{
				vec3 from(x, y, ztop), to(x, y, ztop - 2000.0f);
				CheckResult hit;
				hit.Time = 1.0f;
				char cell[16];
				if (!ModelLineCheck(hit, ModelFrame::Level(engine->Level->Model), to, from, extent, 0))
					snprintf(cell, sizeof(cell), " %5d", (int)hit.Location.z);
				else
					snprintf(cell, sizeof(cell), "      ");
				kwRow += cell;
			}
			LogMessage(kwRow);
		}
	}

	// Taps Space (as a player would) whenever a cutscene holds Harry, so the CutsceneSkip mod fast-forwards it.
	static void TickDebugSkipCutscenes(float now)
	{
		static int enabled = -1;
		static float nextTap = 0.0f, keyUp = 0.0f;
		if (enabled < 0)
		{
			const char* s = getenv("HP1_SKIPCUTS");
			enabled = s && *s && *s != '0';
		}
		if (!enabled)
			return;
		if (keyUp != 0.0f && now >= keyUp)
		{
			engine->OnWindowKeyUp((EInputKey)0x20);
			keyUp = 0.0f;
		}
		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* hud = player ? player->myHUD() : nullptr;
		if (!hud || !BoolProperty(hud, "bCutSceneMode") || !ObjectProperty(hud, "curCutScene") || now < nextTap)
			return;
		LogMessage("HP1 skipcuts: Space during " + ObjectProperty(hud, "curCutScene")->Name.ToString());
		engine->OnWindowKeyDown((EInputKey)0x20);
		keyUp = now + 0.1f;
		nextTap = now + 1.0f;
	}

	static void TickDebugGoto(float now)
	{
		struct Waypoint { vec2 Pos; char Action = 0; float Arg = 0.0f; };
		struct Run { float Time; Array<Waypoint> Points; };
		static bool parsed = false;
		static Array<Run> runs;
		static size_t point = 0;
		static bool holding = false;
		static float lastProgress = 0.0f, bestDist = 0.0f, waitUntil = 0.0f, jumpUp = 0.0f;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_GOTO"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, '|'))
				{
					Run run;
					size_t colon = item.find(':');
					if (colon == std::string::npos)
						continue;
					run.Time = std::stof(item.substr(0, colon));
					std::stringstream pts(item.substr(colon + 1));
					std::string p;
					while (std::getline(pts, p, ';'))
					{
						Waypoint w;
						char action[16] = {};
						int n = sscanf(p.c_str(), "%f,%f,%15s", &w.Pos.x, &w.Pos.y, action);
						if (n < 2)
							continue;
						if (n == 3)
						{
							w.Action = action[0];
							w.Arg = action[1] ? std::stof(action + 1) : 0.0f;
						}
						run.Points.push_back(w);
					}
					if (!run.Points.empty())
						runs.push_back(run);
				}
			}
		}
		auto release = [&]() { if (holding) { engine->OnWindowKeyUp((EInputKey)0x26); holding = false; } };
		if (jumpUp != 0.0f && now >= jumpUp)
		{
			engine->OnWindowKeyUp((EInputKey)0x11);
			jumpUp = 0.0f;
		}
		if (runs.empty() || now < runs.front().Time || now < waitUntil)
			return;

		UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		Run& run = runs.front();
		if (!player || point >= run.Points.size())
			return;

		vec2 pos(player->Location().x, player->Location().y);
		const Waypoint& target = run.Points[point];
		vec2 delta = target.Pos - pos;
		float dist = length(delta);
		if (dist < (target.Action == 'J' ? 8.0f : 40.0f)) // jumps go off at the exact spot (a ledge edge)
		{
			char buf[200];
			snprintf(buf, sizeof(buf), "HP1 goto reached %d at t=%.1f (%.0f,%.0f,%.0f)", (int)point, now,
				player->Location().x, player->Location().y, player->Location().z);
			LogMessage(buf);
			if (target.Action == 'J') // jump towards the next waypoint, still running
			{
				engine->OnWindowKeyDown((EInputKey)0x11);
				jumpUp = now + 0.2f;
			}
			else if (target.Action == 'w') // stop and wait
			{
				release();
				waitUntil = now + target.Arg;
			}
			point++;
			bestDist = 0.0f;
			if (point >= run.Points.size())
			{
				release();
				runs.erase(runs.begin());
				point = 0;
			}
			return;
		}
		if (bestDist == 0.0f || dist < bestDist - 8.0f)
		{
			bestDist = dist;
			lastProgress = now;
		}
		else if (now - lastProgress > 3.0f)
		{
			char buf[200];
			snprintf(buf, sizeof(buf), "HP1 goto stuck on %d at (%.0f,%.0f,%.0f) state=%s", (int)point,
				player->Location().x, player->Location().y, player->Location().z, player->GetStateName().ToString().c_str());
			LogMessage(buf);
			lastProgress = now;
		}

		// Only steer in the player-controlled states: Mounting etc. TurnTo their own target and never finish if the
		// rotation is overwritten every frame.
		if (player->GetStateName().ToString().rfind("Player", 0) == 0)
		{
			int yaw = (int)(std::atan2(delta.y, delta.x) * 32768.0f / 3.14159265f) & 0xffff;
			player->ViewRotation().Yaw = yaw;
			player->Rotation().Yaw = yaw;
		}
		if (!holding)
		{
			engine->OnWindowKeyDown((EInputKey)0x26);
			holding = true;
		}
	}

	void OnFrameRendered(RenderDevice* device)
	{
		float frameTime = SecondsSinceFirstFrame();
		TickDebugKeys(frameTime);
		TickDebugMouse(frameTime);
		TickDebugTrace(frameTime);
		TickDebugDump(frameTime);
		TickDebugHeightmap(frameTime);
		TickDebugSkipCutscenes(frameTime);
		TickDebugGoto(frameTime);
		TickDebugExec(frameTime);

		static bool parsed = false;
		static Array<float> times;
		static std::string dir;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_SHOTS"))
			{
				std::stringstream ss(s);
				std::string item;
				while (std::getline(ss, item, ','))
					if (!item.empty()) times.push_back(std::stof(item));
			}
			const char* d = getenv("HP1_SHOT_DIR");
			dir = d ? d : ".";
		}
		if (times.empty())
			return;

		float now = frameTime;
		if (now < times.front())
			return;
		float label = times.front();
		times.erase(times.begin());

		int width = engine->viewport->ViewportWidth();
		int height = engine->viewport->ViewportHeight();
		Array<TextureColor> pixels((size_t)width * height);
		device->ReadPixels(pixels.data());

		// 24-bit bottom-up BMP
		int rowSize = (width * 3 + 3) & ~3;
		uint32_t dataSize = rowSize * height;
		uint8_t header[54] = { 'B', 'M' };
		auto put32 = [&](int off, uint32_t v) { memcpy(header + off, &v, 4); };
		put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, width); put32(22, height);
		header[26] = 1; header[28] = 24; put32(34, dataSize);

		char name[64];
		snprintf(name, sizeof(name), "/hp1shot_%05.1f.bmp", label);
		std::ofstream f(dir + name, std::ios::binary);
		f.write((const char*)header, 54);
		Array<uint8_t> row((size_t)rowSize);
		for (int y = height - 1; y >= 0; y--) // ReadPixels is top-down
		{
			for (int x = 0; x < width; x++)
			{
				const TextureColor& c = pixels[(size_t)y * width + x];
				row[x * 3 + 0] = c.R; row[x * 3 + 1] = c.G; row[x * 3 + 2] = c.B; // ReadPixels returns BGRA
			}
			f.write((const char*)row.data(), rowSize);
		}
		LogMessage("HP1 screenshot " + dir + name);
	}

	void DebugCamera(vec3& location, Rotator& rotation)
	{
		static bool parsed = false;
		static bool enabled = false;
		static vec3 camLocation;
		static Rotator camRotation;
		if (!parsed)
		{
			parsed = true;
			if (const char* s = getenv("HP1_CAMERA"))
			{
				float v[5] = {};
				std::stringstream ss(s);
				std::string item;
				int n = 0;
				while (n < 5 && std::getline(ss, item, ','))
					v[n++] = std::stof(item);
				if (n == 5)
				{
					enabled = true;
					camLocation = vec3(v[0], v[1], v[2]);
					camRotation = Rotator((int)v[3], (int)v[4], 0);
				}
			}
		}
		if (enabled)
		{
			location = camLocation;
			rotation = camRotation;
		}
	}
}
