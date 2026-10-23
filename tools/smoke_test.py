#!/usr/bin/env python3
"""Smoke tests that need HP1's files, so they run locally and not in CI: short scripted runs of the game
(docs/debug-tools.md) whose logs are checked.

  main_menu      the game starts to the main menu and quits cleanly
  walk_and_save  --level=Lev_Tut1: the cutscene is skipped, Harry runs forward under player control, the game saves
  load_save      the main menu loads that save and Harry is back where it was made

Usage: tools/smoke_test.py [--exe <SurrealEngine>] [--only <name>] <HP1 folder>

The HP1 folder must be a disposable copy (like ../eagames/hp1-work): the runs write its ini files and its save
slot 5. Logs go to build/smoke/. Exit status 1 if any test failed.
"""
import argparse
import math
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXES = [  # the build folders of CONTRIBUTING.md and CI
    "build/Release/SurrealEngine.exe",
    "build/RelWithDebInfo/SurrealEngine.exe",
    "build-mac/SurrealEngine.app/Contents/MacOS/SurrealEngine",
    "build/SurrealEngine.app/Contents/MacOS/SurrealEngine",
    "build-linux/SurrealEngine",
    "build/SurrealEngine",
]
SLOT = 5
TRACE = re.compile(r"HP1 trace t=([\d.]+) (\S+) state=(\S+) .*?loc=\(([-\d.]+),([-\d.]+),([-\d.]+)\)")


class Run:
    def __init__(self, name, returncode, log):
        self.name = name
        self.returncode = returncode
        self.log = log
        self.traces = []  # (time, actor, state, (x, y, z))
        for m in TRACE.finditer(log):
            self.traces.append((float(m.group(1)), m.group(2), m.group(3),
                                (float(m.group(4)), float(m.group(5)), float(m.group(6)))))

    def trace_at(self, actor, time):
        """The actor's last trace at or before time."""
        found = None
        for t, a, state, loc in self.traces:
            if a == actor and t <= time:
                found = (t, state, loc)
        return found


def run_game(args, name, game_args, end, env=None):
    log_dir = ROOT / "build" / "smoke"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"{name}.log"
    run_env = dict(os.environ)
    run_env.update(env or {})
    run_env["HP1_BACKGROUND"] = "1"
    run_env["HP1_EXEC"] = (run_env.get("HP1_EXEC", "") + f";{end}:exit").lstrip(";")
    command = [str(args.exe), "--autolaunch", f"--logfile={log_path}", *game_args, str(args.game)]
    try:
        returncode = subprocess.run(command, env=run_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                    timeout=end + 90).returncode
    except subprocess.TimeoutExpired:
        returncode = "timeout"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    return Run(name, returncode, log)


def common_problems(run):
    problems = []
    if run.returncode != 0:
        problems.append(f"exit status {run.returncode} (the run ends with the exit console command)")
    for line in run.log.splitlines():
        if "FATAL:" in line:
            problems.append(line.strip())
    return problems


def distance(a, b):
    return math.dist(a, b)


def test_main_menu(args, state):
    run = run_game(args, "main_menu", ["--skip-splash"], 8)
    problems = common_problems(run)
    if "Game: Harry Potter and the Philosopher's" not in run.log:
        problems.append("HP1 wasn't recognised (no `Game: Harry Potter and the Philosopher's` line)")
    if "FESlotPage.Created" not in run.log:
        problems.append("the front end's pages weren't created (no FESlotPage.Created)")
    return problems


def test_walk_and_save(args, state):
    save = args.game / "Save" / f"Save{SLOT}.usa"
    before = save.stat().st_mtime if save.exists() else None
    run = run_game(args, "walk_and_save", ["--level=Lev_Tut1", f"--slot={SLOT}"], 13, {
        "HP1_SKIPCUTS": "1",
        "HP1_TRACE": "harry0",
        "HP1_TRACE_INTERVAL": "0.1",
        "HP1_KEYS": "8:Up:2",
        "HP1_EXEC": "11:@console SaveSelectedSlot",
    })
    problems = common_problems(run)
    start = run.trace_at("harry0", 7.9)
    stop = run.trace_at("harry0", 10.9)
    if not start or not stop:
        return problems + ["no harry0 traces before 8 s and 11 s"]
    if start[1] != "PlayerWalking":
        problems.append(f"harry0 is in state {start[1]} at 8 s, not PlayerWalking (the opening cutscene wasn't skipped?)")
    moved = distance(start[2], stop[2])
    if moved < 150:
        problems.append(f"harry0 moved {moved:.0f} units holding Up for 2 s (expected 150 or more)")
    if not save.exists() or save.stat().st_mtime == before:
        problems.append(f"{save} wasn't written")
    else:
        state["saved_at"] = stop[2]
    return problems


def test_load_save(args, state):
    if "saved_at" not in state:
        return ["needs walk_and_save to pass first"]
    run = run_game(args, "load_save", ["--skip-splash"], 12, {
        "HP1_TRACE": "harry0",
        "HP1_EXEC": f"4:@console.MenuBook.SlotPage SetSelectedSlot {SLOT};4.5:@console.MenuBook.SlotPage LoadSelectedSlot",
    })
    problems = common_problems(run)
    last = run.trace_at("harry0", 1e9)
    if not last:
        return problems + ["no harry0 traces: the save didn't load"]
    off = distance(last[2], state["saved_at"])
    if off > 32:
        problems.append(f"harry0 is {off:.0f} units from where the save was made")
    return problems


TESTS = [
    ("main_menu", test_main_menu),
    ("walk_and_save", test_walk_and_save),
    ("load_save", test_load_save),
]


def find_exe():
    for candidate in EXES:
        if (ROOT / candidate).exists():
            return ROOT / candidate
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path, help="a disposable copy of the HP1 folder")
    parser.add_argument("--exe", type=Path, default=None, help="the SurrealEngine executable (default: the first build found)")
    parser.add_argument("--only", default=None, help="run only the tests whose name contains this")
    args = parser.parse_args()
    args.exe = args.exe or find_exe()
    if not args.exe or not args.exe.exists():
        sys.exit("SurrealEngine not found in build/, build-mac/ or build-linux/; build it or pass --exe")
    args.game = args.game.resolve()
    if not (args.game / "System" / "HP.exe").exists() and not any((args.game / "System").glob("*.exe")):
        sys.exit(f"{args.game} isn't an HP1 folder (no System/HP.exe)")

    state = {}
    failed = 0
    for name, test in TESTS:
        if args.only and args.only not in name:
            continue
        problems = test(args, state)
        print(("FAIL " if problems else "ok   ") + name)
        for p in problems:
            print("  " + p)
        failed += bool(problems)
    print(f"logs in {ROOT / 'build' / 'smoke'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
