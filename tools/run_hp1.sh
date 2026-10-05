#!/usr/bin/env bash
# Boot HP1 in SurrealEngine without the launcher, streaming the log to a file.
# Usage: tools/run_hp1.sh [seconds] [extra SurrealEngine args...]
#   HP1_GAME_DIR  game folder (default: ../eagames/hp1-work, a disposable copy of the retail install in ../eagames/hp1,
#                 made on first use; SurrealEngine writes ini and save files into it)
#   HP1_LOG       log file    (default: build/hp1_run.log)
#   HP1_WINDOW    windowed size for development runs, written into the game folder's SE-HP.ini before launch
#                 (default: 1280x720); HP1_FULLSCREEN=1 leaves the ini alone
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${HP1_GAME_DIR:-$ROOT/../eagames/hp1-work}"
if [ -z "${HP1_GAME_DIR:-}" ] && [ ! -d "$GAME" ]; then
	[ -d "$ROOT/../eagames/hp1/System" ] || { echo "no game: expected the HP1 retail files in ../eagames/hp1" >&2; exit 1; }
	echo "creating $GAME from ../eagames/hp1"
	cp -r "$ROOT/../eagames/hp1" "$GAME"
fi
LOG="${HP1_LOG:-$ROOT/build/hp1_run.log}"
SECS="${1:-0}"
shift || true

INI="$GAME/System/SE-HP.ini"
if [ "${HP1_FULLSCREEN:-0}" != 1 ] && [ -f "$INI" ]; then
	WIN="${HP1_WINDOW:-1280x720}"
	awk -v w="${WIN%x*}" -v h="${WIN#*x}" '
		/^\[/ { client = ($0 ~ /^\[(Engine\.SurrealClient|WinDrv\.WindowsClient)\]/) }
		client && /^StartupFullscreen=/ { sub(/=.*/, "=False") }
		client && /^WindowedViewportX=/ { sub(/=.*/, "=" w) }
		client && /^WindowedViewportY=/ { sub(/=.*/, "=" h) }
		{ print }' "$INI" > "$INI.tmp" && mv "$INI.tmp" "$INI"
fi

EXE="$ROOT/build/Release/SurrealEngine.exe"
[ -x "$EXE" ] || { echo "build first: tools/build.sh" >&2; exit 1; }

if [ "$SECS" -gt 0 ]; then
	timeout "$SECS" "$EXE" --autolaunch --logfile="$LOG" "$@" "$GAME" || true
else
	"$EXE" --autolaunch --logfile="$LOG" "$@" "$GAME"
fi
echo "log: $LOG"
grep -o "Unimplemented: [A-Za-z0-9_.]*" "$LOG" | sort | uniq -c | sort -rn || true
