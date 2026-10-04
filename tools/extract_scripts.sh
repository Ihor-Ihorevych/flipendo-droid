#!/usr/bin/env bash
# Extract the UnrealScript of every package of our own install into reference/<game>/ScriptSource/<Pkg>/Classes/
# (gitignored; derived from the game). Uses the source text embedded in the .u files (with the original comments)
# plus generated defaultproperties, via tools/uelib_dump (built on the tools/Unreal-Library submodule, needs the
# .NET 10 SDK). This is what our disc runs; other script exports are different builds.
# Usage: tools/extract_scripts.sh [hp1|hp2]   (default hp1; reads ../eagames/<game>/System)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${1:-hp1}"
SYSTEM="$ROOT/../eagames/$GAME/System"
[ -d "$SYSTEM" ] || { echo "no System folder at $SYSTEM" >&2; exit 1; }
OUT="$ROOT/reference/$GAME/ScriptSource"

[ -f "$ROOT/tools/Unreal-Library/src/Eliot.UELib.csproj" ] || git -C "$ROOT" submodule update --init tools/Unreal-Library
dotnet build "$ROOT/tools/uelib_dump/uelib_dump.csproj" -c Release -v q -nologo | grep -E " error |rror\(s\)" || true
rm -rf "$OUT"
"$ROOT/tools/uelib_dump/bin/Release/net10.0/uelib_dump" scripts "$SYSTEM" "$OUT" 2>&1 | grep -v "PropertyTag value size error" || true
