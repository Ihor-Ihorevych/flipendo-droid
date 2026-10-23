#!/usr/bin/env python3
"""Checks the rules in CLAUDE.md that a script can check, without the games or a build (run by CI on every push).

  - src/surreal-patches/: every patch has a routes.txt section and starts with its description, every file a patch
    changes is routed to that patch (the first matching glob), and no added line carries a `flipendo:` marker
  - no game data or extracted scripts are tracked
  - every `// IDA ` tag names a DLL or exe and says which game's address it is (or that it isn't verified)

Usage: tools/check_repo.py (exit status 1 lists the problems)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCHES = ROOT / "src" / "surreal-patches"
OUR_CODE = ["src/knowwonder", "src/hp1", "src/hp2"]
GAME_DATA = re.compile(r"\.(u|unr|utx|uax|umx|usa|uc|exe|dll)$", re.IGNORECASE)
IDA_TAG = re.compile(r"^// IDA [A-Za-z0-9_]+\.(dll|exe): ")
IDA_GAME = re.compile(r"\[HP[12]( [A-Za-z0-9]+)? 0x[0-9A-Fa-f]+\]|NOT yet verified|: none:")

problems = []


def glob_regex(glob):
    """routes.txt globs: `*` within a folder, `**` across folders."""
    out = ""
    i = 0
    while i < len(glob):
        if glob.startswith("**", i):
            out += ".*"
            i += 2
        elif glob[i] == "*":
            out += "[^/]*"
            i += 1
        else:
            out += re.escape(glob[i])
            i += 1
    return re.compile(out + "$")


def read_routes():
    routes = []  # (glob, regex, patch), in file order
    descriptions = {}
    section = None
    for n, line in enumerate((PATCHES / "routes.txt").read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^\[([^\]]+\.patch)\]\s*(.*)$", line)
        if m:
            section = m.group(1)
            descriptions[section] = m.group(2)
        elif section is None:
            problems.append(f"routes.txt:{n}: glob before any [patch] section: {line}")
        else:
            routes.append((line, glob_regex(line), section))
    return routes, descriptions


def check_patches():
    routes, descriptions = read_routes()
    patch_files = sorted(p.name for p in PATCHES.glob("*.patch"))
    for name in patch_files:
        if name not in descriptions:
            problems.append(f"{name}: no [{name}] section in routes.txt")
    for name in descriptions:
        if name not in patch_files:
            problems.append(f"routes.txt: [{name}] has no patch file")

    for name in patch_files:
        lines = (PATCHES / name).read_text(encoding="utf-8").splitlines()
        if name in descriptions and (not lines or lines[0] != descriptions[name]):
            problems.append(f"{name}:1: the first line isn't routes.txt's description for it")
        if not lines or not lines[0].startswith("flipendo: "):
            problems.append(f"{name}:1: doesn't start with a `flipendo: ` line")
        changed = []
        for n, line in enumerate(lines, 1):
            m = re.match(r"^diff --git a/(\S+) b/(\S+)$", line)
            if m:
                changed.append((n, m.group(2)))
            elif line.startswith("+") and not line.startswith("+++") and "flipendo:" in line:
                problems.append(f"{name}:{n}: a `flipendo:` marker in engine code: {line[1:].strip()}")
        if not changed:
            problems.append(f"{name}: changes no files")
        for n, path in changed:
            owner = next((patch for _, regex, patch in routes if regex.match(path)), None)
            if owner is None:
                problems.append(f"{name}:{n}: {path} matches no glob in routes.txt")
            elif owner != name:
                problems.append(f"{name}:{n}: {path} is routed to {owner}")


def tracked_files():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], capture_output=True, check=True).stdout
    return [f for f in out.decode("utf-8").split("\0") if f]


def check_game_data(files):
    for f in files:
        if GAME_DATA.search(f):
            problems.append(f"{f}: game data or an extracted script is tracked")


def check_ida_tags(files):
    for f in files:
        if not any(f.startswith(d + "/") for d in OUR_CODE) or not f.endswith((".cpp", ".h")):
            continue
        lines = (ROOT / f).read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            text = line.strip()
            if not text.startswith("// IDA "):
                continue
            if not IDA_TAG.match(text):
                problems.append(f"{f}:{i + 1}: an IDA tag reads `// IDA <Dll>.dll: ...`: {text}")
                continue
            # The tag with the comment lines that continue it (up to the next tag or the code)
            tag = text
            for more in lines[i + 1:]:
                more = more.strip()
                if not more.startswith("//") or more.startswith("// IDA "):
                    break
                tag += " " + more
            if not IDA_GAME.search(tag):
                problems.append(f"{f}:{i + 1}: the IDA tag doesn't say which game ([HP1 0x...] / [HP2 0x...]) or "
                                f"that it's NOT yet verified: {text}")


def main():
    check_patches()
    files = tracked_files()
    check_game_data(files)
    check_ida_tags(files)
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} problems")
        return 1
    print("check_repo: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
