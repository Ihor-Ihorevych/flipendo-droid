#!/usr/bin/env bash
# Configure (first time) and build the engine. Usage: tools/build.sh [Release|Debug|RelWithDebInfo] [target]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-Release}"
TARGET="${2:-}"

# engine/ is a submodule of SurrealEngine; our hooks are applied from patches/.
"$ROOT/tools/apply_patches.sh"

CMAKE="${CMAKE:-}"
if [ -z "$CMAKE" ]; then
	if command -v cmake >/dev/null 2>&1; then
		CMAKE=cmake
	else
		# CMake bundled with Visual Studio 18 (2026)
		CMAKE="$(ls -d "/c/Program Files/Microsoft Visual Studio/18/"*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe 2>/dev/null | head -1)"
	fi
fi
[ -n "$CMAKE" ] || { echo "cmake not found; set CMAKE=/path/to/cmake" >&2; exit 1; }

if [ ! -f "$ROOT/build/CMakeCache.txt" ]; then
	"$CMAKE" -S "$ROOT/engine" -B "$ROOT/build" -G "Visual Studio 18 2026" -A x64
fi

"$CMAKE" --build "$ROOT/build" --config "$CONFIG" --parallel ${TARGET:+--target "$TARGET"}
