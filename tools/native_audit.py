#!/usr/bin/env python3
"""Audit a game's native functions against what SurrealEngine (and Flipendo's hp1/) implements.

Usage: python tools/native_audit.py [hp1|hp2]   (default hp1; writes docs/re/native_audit_<game>.md)
For HP2 a kw/ override is reported as HP1_PORT: the native exists for HP1 (gated to it) and may carry over
(see docs/hp2_compare.md).


Reads every `native` function/event declared in our disc's scripts (reference/<game>/ScriptSource, from
tools/extract_scripts.sh) and every RegisterVMNativeFunc_N(...) call in
engine/SurrealEngine/Native, plus our overrides in kw/ and hp1/ (which win for HP1), then classifies each native:

  MISSING     declared in HP1 script, never registered by the engine
  OTHER_GAME  registered, but only inside a branch for some other game
  STUB        registered, but the handler (or the UActor/UObject method it
              forwards to) calls LogUnimplemented
  INDEX       registered, but with a different native index than HP1 uses
  OK          registered and has a real body

Output: docs/re/native_audit_<game>.md (and a summary on stdout).
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = sys.argv[1] if len(sys.argv) > 1 else "hp1"
if GAME not in ("hp1", "hp2"):
    sys.exit("usage: native_audit.py [hp1|hp2]")
SCRIPTS = os.path.join(ROOT, "reference", GAME, "ScriptSource")
ENGINE = os.path.join(ROOT, "engine", "SurrealEngine")
PORT_DIRS = [os.path.join(ROOT, "kw"), os.path.join(ROOT, "hp1")]  # our overrides: shared engine code, HP1-only
OUT = os.path.join(ROOT, "docs", "re", "native_audit_%s.md" % GAME)

# Conditions in SurrealEngine's RegisterFunctions() that are true for HP1.
HP1_TRUE = [r"IsHarryPotter1\(\)" if GAME == "hp1" else r"IsHarryPotter2\(\)", r"ue1Version\s*>=?\s*(2\d\d|3\d\d|4[0-9]\d)\b"]


def strip_comments(text):
    # One left-to-right pass: a "//*****" banner line is a line comment, not the start of a block comment.
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def parse_scripts():
    """-> list of dicts: package, cls, name, index, kind, decl"""
    out = []
    decl_re = re.compile(
        r"^[ \t]*((?:\w+(?:\(\d+\))?[ \t]+)*?)native(?:\((\d+)\))?((?:[ \t]+\w+)*?)[ \t]+(function|event)[ \t]+"
        r"(?:[\w<>]+[ \t]+)?(\w+)\s*\(",
        re.M,
    )
    for pkg in sorted(os.listdir(SCRIPTS)):
        cdir = os.path.join(SCRIPTS, pkg, "Classes")
        if not os.path.isdir(cdir):
            continue
        for fn in sorted(os.listdir(cdir)):
            if not fn.lower().endswith(".uc"):
                continue
            with open(os.path.join(cdir, fn), encoding="latin-1") as f:
                text = strip_comments(f.read())
            cls = fn[:-3]
            for m in decl_re.finditer(text):
                line = text[m.start():text.find("\n", m.start())].strip()
                out.append(dict(package=pkg, cls=cls, name=m.group(5),
                                index=int(m.group(2)) if m.group(2) else 0,
                                kind=m.group(4), decl=line))
    return out


def read_engine_sources():
    files = {}
    for dirpath, _, names in os.walk(ENGINE):
        for n in names:
            if n.endswith(".cpp"):
                p = os.path.join(dirpath, n)
                with open(p, encoding="utf-8", errors="replace") as f:
                    files[p] = f.read()
    return files


def parse_registrations(files):
    """-> dict (cls, name) -> list of (handler 'NX::Fn', index, condition, file:line)"""
    regs = defaultdict(list)
    reg_re = re.compile(r'RegisterVMNativeFunc_\d+\(\s*"(\w+)"\s*,\s*"(\w+)"\s*,\s*&(\w+)::(\w+)\s*,\s*(\d+)\s*\)')
    for path, text in files.items():
        if os.sep + "Native" + os.sep not in path:
            continue
        lines = text.split("\n")
        # Track enclosing if/else conditions by indentation depth.
        stack = []  # (indent, condition)
        last_if = {}  # indent -> condition text of the most recent if at that indent
        for i, line in enumerate(lines):
            stripped = line.strip()
            indent = len(line) - len(line.lstrip("\t"))
            if not stripped or stripped in ("{", "}"):
                continue
            while stack and stack[-1][0] >= indent:
                stack.pop()
            m_if = re.match(r"(?:else\s+)?if\s*\((.*)\)\s*$", stripped)
            if m_if:
                cond = m_if.group(1)
                if stripped.startswith("else"):
                    cond = f"else-if ({cond}) [prev: {last_if.get(indent, '?')}]"
                last_if[indent] = m_if.group(1)
                stack.append((indent, cond))
                continue
            if stripped == "else":
                stack.append((indent, f"else of ({last_if.get(indent, '?')})"))
                continue
            for m in reg_re.finditer(line):
                cond = " && ".join(c for _, c in stack)
                regs[(m.group(1), m.group(2))].append(dict(
                    handler=f"{m.group(3)}::{m.group(4)}", index=int(m.group(5)),
                    cond=cond, where=f"{os.path.relpath(path, ROOT)}:{i + 1}"))
    return regs


def read_hp1_sources():
    files = {}
    for dirpath, _, names in (w for d in PORT_DIRS for w in os.walk(d)):
        for n in names:
            if n.endswith(".cpp"):
                p = os.path.join(dirpath, n)
                with open(p, encoding="utf-8", errors="replace") as f:
                    files[p] = f.read()
    return files


def parse_hp1_registrations(files):
    """hp1/ registers HP1-only natives (OverrideNative) as RegisterVMNativeFunc_N("Class", "Fn", &Handler, idx)
    or NativeFunctions::RegisterHandler("Class", "Fn", idx, &Handler). -> dict (cls, name) -> registration"""
    regs = {}
    vm_re = re.compile(r'RegisterVMNativeFunc_\d+\(\s*"(\w+)"\s*,\s*"(\w+)"\s*,\s*&(\w+)\s*,\s*(\d+)\s*\)')
    raw_re = re.compile(r'RegisterHandler\(\s*"(\w+)"\s*,\s*"(\w+)"\s*,\s*(\d+)\s*,\s*&(\w+)\s*\)')
    for path, text in files.items():
        for i, line in enumerate(text.split("\n")):
            for m in vm_re.finditer(line):
                regs[(m.group(1), m.group(2))] = dict(handler=m.group(3), index=int(m.group(4)), cond="",
                                                      where=f"{os.path.relpath(path, ROOT)}:{i + 1}")
            for m in raw_re.finditer(line):
                regs[(m.group(1), m.group(2))] = dict(handler=m.group(4), index=int(m.group(3)), cond="",
                                                      where=f"{os.path.relpath(path, ROOT)}:{i + 1}")
    return regs


def applies_to_hp1(cond):
    if not cond:
        return True
    if cond.startswith("else"):
        # Else branch: applies to HP1 only if the guarded condition was false for HP1.
        prev = re.search(r"\((.*)\)", cond).group(1)
        return not any(re.search(p, prev) for p in HP1_TRUE) and ("IsHarryPotter1" if GAME == "hp1" else "IsHarryPotter2") not in prev
    return any(re.search(p, cond) for p in HP1_TRUE)


def function_body(files, qualname):
    """Return the body text of `qualname` (e.g. 'NActor::PlayAnim_HP') from any .cpp."""
    pat = re.compile(r"\b" + re.escape(qualname) + r"\s*\([^;{]*\)\s*(?:const\s*)?\{")
    for text in files.values():
        m = pat.search(text)
        if not m:
            continue
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    return text[m.end():i]
            i += 1
    return None


def is_stub(files, handler, seen=None):
    seen = seen or set()
    if handler in seen:
        return False
    seen.add(handler)
    body = function_body(files, handler)
    if body is None:
        return False
    if "LogUnimplemented" in body:
        return True
    code = [l for l in body.strip().split("\n") if l.strip()]
    # Follow a thin forwarder: SelfX->Method(...) or UX::Method(...)
    fwd = re.findall(r"->(\w+)\s*\(", body)
    if len(code) <= 4 and fwd:
        cast = re.search(r"Cast<(U\w+)>", body)
        if cast:
            return any(is_stub(files, f"{cast.group(1)}::{fn}", seen) for fn in fwd)
    return False


def compute():
    """-> (natives, rows): rows[status] = [(native dict, note)]. Also used by tools/dll_report.py."""
    if not os.path.isdir(SCRIPTS):
        sys.exit(f"missing {SCRIPTS} - run tools/extract_scripts.sh {GAME} first")
    natives = parse_scripts()
    files = read_engine_sources()
    regs = parse_registrations(files)
    hp1_files = read_hp1_sources()
    hp1_regs = parse_hp1_registrations(hp1_files)
    files.update(hp1_files)

    rows = defaultdict(list)
    for n in natives:
        cands = regs.get((n["cls"], n["name"]), [])
        hp1 = [r for r in cands if applies_to_hp1(r["cond"])]
        if (n["cls"], n["name"]) in hp1_regs and GAME == "hp1":
            cands = cands + [hp1_regs[(n["cls"], n["name"])]]
            hp1 = [hp1_regs[(n["cls"], n["name"])]]
        if GAME != "hp1" and (n["cls"], n["name"]) in hp1_regs and (not hp1 or is_stub(files, hp1[-1]["handler"])):
            r = hp1_regs[(n["cls"], n["name"])]
            status, note = "HP1_PORT", f'{r["handler"]} ({r["where"]})'
        elif not cands:
            status, note = "MISSING", ""
        elif not hp1:
            status, note = "OTHER_GAME", "; ".join(f'{r["cond"]} ({r["where"]})' for r in cands)
        else:
            r = hp1[-1]
            if is_stub(files, r["handler"]):
                status, note = "STUB", f'{r["handler"]} ({r["where"]})'
            elif n["index"] and r["index"] and n["index"] != r["index"]:
                status, note = "INDEX", f'script native({n["index"]}) vs engine {r["index"]} ({r["where"]})'
            else:
                status, note = "OK", r["handler"]
        rows[status].append((n, note))
    return natives, rows


def main():
    natives, rows = compute()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    order = ["MISSING", "HP1_PORT", "OTHER_GAME", "STUB", "INDEX", "OK"]
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("# %s native function audit\n\n" % GAME.upper())
        f.write("Generated by `tools/native_audit.py` - do not edit by hand.\n\n")
        f.write("| Status | Count |\n|---|---|\n")
        for s in order:
            f.write(f"| {s} | {len(rows[s])} |\n")
        for s in order:
            if s == "OK":
                continue
            f.write(f"\n## {s}\n\n| Package | Class.Function | Index | Note |\n|---|---|---|---|\n")
            for n, note in sorted(rows[s], key=lambda x: (x[0]["package"], x[0]["cls"], x[0]["name"])):
                f.write(f'| {n["package"]} | {n["cls"]}.{n["name"]} | {n["index"] or ""} | {note} |\n')
        f.write("\n## OK\n\n")
        f.write(", ".join(sorted(f'{n["cls"]}.{n["name"]}' for n, _ in rows["OK"])) + "\n")

    print(f"{len(natives)} natives declared in {GAME.upper()} scripts")
    for s in order:
        print(f"  {s:10} {len(rows[s])}")
    print(f"-> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
