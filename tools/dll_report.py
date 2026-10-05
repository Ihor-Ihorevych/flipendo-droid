#!/usr/bin/env python3
"""The game's DLLs: what each one is, what it exports, and where Flipendo stands with it.

Writes docs/re/reports/dlls.md from
  - the DLLs themselves (../eagames/hp1/System, ../eagames/hp2/System): size, exports, the C++ classes they export;
  - the "// IDA <Dll>.dll: <decorated name>" tags in src/knowwonder/, src/hp1/ and src/hp2/: the functions Flipendo reimplements;
  - ../ida/fingerprints/ (tools/ida_fingerprint.py): whether each of those is the same code in HP2;
  - tools/native_audit.py: the state of the script natives each DLL implements (Core.u -> Core.dll, ...).
The "what it is" text below is written by hand; keep it to what the exports and our own work show.

Usage: python tools/dll_report.py
"""
import collections
import glob
import importlib.util
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = {"hp1": os.path.join(ROOT, "..", "eagames", "hp1", "System"), "hp2": os.path.join(ROOT, "..", "eagames", "hp2", "System")}
FP_DIR = os.path.join(ROOT, "..", "ida", "fingerprints")
OUT = os.path.join(ROOT, "docs", "re", "reports", "dlls.md")
TAG = re.compile(r"//\s*IDA\s+(\w+)\.dll:\s*(\?\S+)")

# What each DLL is, and what stands in for it in Flipendo. Order = order in the document.
DLLS = [
    ("Core", "The object system everything else is built on: UObject, names, packages (.u/.unr/.utx loading and "
             "saving), the UnrealScript virtual machine and its intrinsic natives (Object.uc), memory, math "
             "(vectors, rotators, quaternions), config/ini files, logging.",
     "SurrealEngine's own Core (`Package/`, `VM/`, `Math/`). Flipendo only adds the few KnowWonder math helpers "
     "the ported engine code calls (`src/knowwonder/`)."),
    ("Engine", "The game engine proper: actors, levels, physics (walking, falling, flying, movers), collision, "
               "navigation, players and input, the HUD/canvas, sound and music interfaces, save games, and the "
               "script natives of Engine.u. KnowWonder modified it heavily: skeletal animation with animation "
               "channels (AnimChannel), particle systems (ParticleFX), wind, spell gestures (Gesture), "
               "InterpolationManager, CT_Box collision, ledge grabbing (APawn::Mount), save-slot info "
               "(GameSaveInfo) and more.",
     "SurrealEngine's own engine for stock UE1 behaviour; **Flipendo's `src/knowwonder/` reimplements KnowWonder's "
     "additions and changes** (the bulk of the port), hooked in through `src/surreal-patches/`."),
    ("Fire", "Procedural (\"algorithmic\") textures computed every frame instead of loaded as pictures: "
             "FireTexture (fire, sparks), WaterTexture with its WaveTexture and WetTexture kinds (rippling water), "
             "IceTexture (one texture refracted through another, used e.g. behind the spell-lesson template), "
             "FractalTexture (their common base).",
     "SurrealEngine implements Fire/Water/Wave/Wet textures; IceTexture was a stub and is now "
     "`src/knowwonder/KWIceTexture.cpp`."),
    ("Render", "The scene renderer above the graphics driver: walks the BSP, clips and sorts what's visible, "
               "lighting and fog, draws the level, meshes, sprites and (KnowWonder) particle systems, and passes "
               "polygons to a render device (D3DDrv, SoftDrv).",
     "SurrealEngine's renderer (`Render/`). Flipendo ports KnowWonder's particle drawing "
     "(`src/knowwonder/KWParticleRender.cpp`) and hooks skeletal mesh drawing."),
    ("D3DDrv", "Direct3D 7 render device: turns Render's polygons into Direct3D calls.",
     "Replaced: SurrealEngine's Vulkan / Direct3D 11-12 / OpenGL render devices."),
    ("SoftDrv", "Software rasterizer render device (no 3D card).", "Replaced by SurrealEngine's render devices."),
    ("WinDrv", "Windows client: the game window (viewport), mouse/keyboard input (DirectInput), resolution switching.",
     "Replaced: SurrealEngine's window and input (SurrealWidgets), patched for focus handling "
     "(`src/surreal-patches/0003-cursor-focus.patch`)."),
    ("Window", "Win32 user-interface framework (dialogs, controls) used by the launcher's setup screens and the "
               "editor.", "Not needed: SurrealEngine has its own launcher; the game's menus are UnrealScript."),
    ("Galaxy", "HP1's audio subsystem (Galaxy): sound effects, 3D positioning and music playback through "
               "DirectSound.", "Replaced: SurrealEngine's audio device, with HP1 fixes (`src/surreal-patches/0140-knowwonder-audio.patch`)."),
    ("IpDrv", "TCP/IP networking (internet links, server queries). Unused by the single-player game.",
     "Not needed."),
    ("UWeb", "Built-in web server (remote server administration). Unused by the single-player game.", "Not needed."),
    ("Editor", "UnrealEd's backend: brushes and CSG, importing/exporting, commandlets.",
     "Not needed to play; SurrealEngine ships its own SurrealEditor."),
    ("ALAudio", "HP2 only: audio subsystem on OpenAL, replacing HP1's Galaxy.", "Not looked at yet (HP2)."),
    ("OpenAL32", "HP2 only: the OpenAL runtime ALAudio uses.", "Third-party; not looked at."),
    ("ogg", "HP2 only: Ogg container library (HP2's music is Ogg Vorbis). Also `ogg_d.dll` (debug build).",
     "Third-party; not looked at."),
    ("vorbis", "HP2 only: Vorbis audio decoder. Also `vorbis_d.dll`.", "Third-party; not looked at."),
    ("vorbisfile", "HP2 only: Ogg Vorbis file reading on top of ogg/vorbis. Also `vorbisfile_d.dll`.",
     "Third-party; not looked at."),
    ("WinKeyHook", "HP2 only: a small keyboard hook, by its name to stop the Windows key from leaving the game. "
                   "Not reversed.", "Not needed."),
    ("DrvMgt", "SafeDisc copy protection (HP1 has `drvmgt.dll` and `secdrv.sys` in the game folder).",
     "Not needed: Flipendo never runs the SafeDisc-wrapped exe."),
]
SHORT = {'Core': 'SurrealEngine', 'Engine': 'SurrealEngine + **src/knowwonder/**', 'Fire': 'SurrealEngine + src/knowwonder/ (IceTexture)', 'Render': 'SurrealEngine + src/knowwonder/ (particles)', 'D3DDrv': 'replaced (SurrealEngine render devices)', 'SoftDrv': 'replaced (SurrealEngine render devices)', 'WinDrv': 'replaced (SurrealEngine window/input)', 'Window': 'not needed', 'Galaxy': 'replaced (SurrealEngine audio)', 'IpDrv': 'not needed', 'UWeb': 'not needed', 'Editor': 'not needed', 'ALAudio': 'HP2: not looked at', 'OpenAL32': 'HP2: third-party', 'ogg': 'HP2: third-party', 'vorbis': 'HP2: third-party', 'vorbisfile': 'HP2: third-party', 'WinKeyHook': 'not needed', 'DrvMgt': 'not needed (SafeDisc)'}
# Script packages whose natives live in a DLL of the same name.
NATIVE_PACKAGES = {"Core": "Core", "Engine": "Engine", "Fire": "Fire", "IpDrv": "IpDrv", "UWeb": "UWeb", "Editor": "Editor"}


def pe_exports(path):
    """Exported names of a PE file (None if it can't be read)."""
    try:
        data = open(path, "rb").read()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        optsize = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        dd = opt + (96 if magic == 0x10B else 112)
        exp_rva, exp_size = struct.unpack_from("<II", data, dd)
        secs = []
        for i in range(nsec):
            s = opt + optsize + i * 40
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
            secs.append((va, max(vsize, rawsize), rawptr))

        def off(rva):
            for va, size, raw in secs:
                if va <= rva < va + size:
                    return rva - va + raw
            raise ValueError
        if not exp_rva:
            return []
        e = off(exp_rva)
        nnames, _, names_rva = struct.unpack_from("<III", data, e + 24)[0], 0, struct.unpack_from("<I", data, e + 32)[0]
        nnames = struct.unpack_from("<I", data, e + 24)[0]
        names = []
        base = off(names_rva)
        for i in range(nnames):
            p = off(struct.unpack_from("<I", data, base + 4 * i)[0])
            names.append(data[p:data.index(b"\0", p)].decode("latin-1"))
        return names
    except Exception:
        return None


def classes_of(names):
    """Count exports per C++ class from decorated names (?Fn@UClass@@...)."""
    c = collections.Counter()
    for n in names or []:
        m = re.match(r"\?\??[^@]*@([A-Z]\w+)@@", n)
        if m and m.group(1)[0] in "UAFT" and len(m.group(1)) > 2:
            c[m.group(1)] += 1
    return c


def ported_tags():
    tags = collections.defaultdict(list)
    for d in ("knowwonder", "hp1", "hp2"):
        for path in glob.glob(os.path.join(ROOT, "src", d, "**", "*.*"), recursive=True):
            if not path.endswith((".cpp", ".h")):
                continue
            for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
                m = TAG.search(line)
                if m:
                    tags[m.group(1)].append((m.group(2).rstrip(",;"), "%s:%d" % (os.path.relpath(path, ROOT).replace("\\", "/"), n)))
    return tags


def fingerprints(game, dll):
    p = os.path.join(FP_DIR, "%s_%s.json" % (game, dll))
    return json.load(open(p)) if os.path.exists(p) else None


def native_rows(game):
    spec = importlib.util.spec_from_file_location("native_audit_" + game, os.path.join(ROOT, "tools", "native_audit.py"))
    mod = importlib.util.module_from_spec(spec)
    saved = sys.argv
    sys.argv = ["native_audit.py", game]
    try:
        spec.loader.exec_module(mod)
        if not os.path.isdir(mod.SCRIPTS):
            return None
        _, rows = mod.compute()
    finally:
        sys.argv = saved
    per = collections.defaultdict(collections.Counter)
    for status, items in rows.items():
        for n, _ in items:
            per[n["package"]][status] += 1
    return per


def readable(name):
    """?execPlayAnim@AActor@@QAEXAAUFFrame@@QAX@Z -> AActor::execPlayAnim"""
    m = re.match(r"\?(\?[0-9A-Z_]|[^@]+)@([^@]+)@@", name)
    if not m:
        return name
    fn, cls = m.group(1), m.group(2)
    fn = {"?0": cls, "?1": "~" + cls, "?4": "operator="}.get(fn, fn)
    return "%s::%s" % (cls, fn)


def main():
    tags = ported_tags()
    natives = {g: native_rows(g) for g in GAMES}
    L = ["# The game's DLLs", "",
         "Generated by `tools/dll_report.py`; the descriptions are hand-written in that script. What each DLL of the",
         "HP1 and HP2 installs does, what it exports, and where Flipendo stands: who provides it now (SurrealEngine,",
         "Flipendo's `src/knowwonder/`, or nothing because it isn't needed), which functions we reimplemented (`// IDA` tags) and",
         "whether HP2 has the same code, and the state of the script natives it implements.", "",
         "Details: [native_audit_hp1.md](native_audit_hp1.md), [native_audit_hp2.md](native_audit_hp2.md) (every native),",
         "[hp2_compare.md](hp2_compare.md) (every export, HP1 vs HP2).", "",
         "| DLL | HP1 | HP2 | exports HP1 / HP2 | ported functions | provided by |", "|---|---|---|---|---|---|"]
    info = {}
    for name, what, provider in DLLS:
        row = {}
        for g, sysdir in GAMES.items():
            p = next((q for q in glob.glob(os.path.join(sysdir, "*.dll")) if os.path.basename(q).lower() == (name + ".dll").lower()), None)
            row[g] = (os.path.getsize(p), pe_exports(p)) if p else None
        info[name] = row
        size = lambda g: "%d KB" % (row[g][0] // 1024) if row[g] else "-"
        exp = lambda g: str(len(row[g][1])) if row[g] and row[g][1] is not None else "-"
        prov = SHORT[name]
        L.append("| [%s](#%s) | %s | %s | %s / %s | %d | %s |" % (name, name.lower(), size("hp1"), size("hp2"), exp("hp1"), exp("hp2"),
                                                               len(tags.get(name, [])), prov))
    for name, what, provider in DLLS:
        row = info[name]
        L += ["", "## %s" % name, "", what, "", "**In Flipendo:** " + provider]
        names = row["hp1"][1] if row["hp1"] and row["hp1"][1] else (row["hp2"][1] if row["hp2"] and row["hp2"][1] else None)
        cls = classes_of(names)
        if cls:
            top = ", ".join("%s (%d)" % (c, n) for c, n in cls.most_common(24))
            L += ["", "**Exported classes** (exports per class, largest first): " + top + ("" if len(cls) <= 24 else ", and %d more" % (len(cls) - 24)) + "."]
        pkg = NATIVE_PACKAGES.get(name)
        for g in GAMES:
            per = natives.get(g)
            if pkg and per and per.get(pkg):
                c = per[pkg]
                L += ["", "**%s script natives** (%s.u): %s." % (g.upper(), pkg, ", ".join("%d %s" % (c[s], s) for s in
                      ("OK", "HP1_PORT", "STUB", "MISSING", "INDEX", "OTHER_GAME") if c[s]))]
        ported = tags.get(name, [])
        if ported:
            a, b = fingerprints("hp1", name), fingerprints("hp2", name)
            L += ["", "**Reimplemented in Flipendo** (%d):" % len(ported), "", "| function | where | HP2 |", "|---|---|---|"]
            for dec, where in sorted(ported, key=lambda t: readable(t[0])):
                if a is None or b is None or dec not in a:
                    hp2 = "?"
                elif dec not in b:
                    hp2 = "missing"
                elif a[dec]["hash"] == b[dec]["hash"]:
                    hp2 = "identical"
                elif a[dec].get("shape") == b[dec].get("shape"):
                    hp2 = "offsets only"
                else:
                    hp2 = "changed"
                L.append("| `%s` | `%s` | %s |" % (readable(dec), where, hp2))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")
    print("->", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
