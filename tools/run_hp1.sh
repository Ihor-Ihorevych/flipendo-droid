#!/usr/bin/env bash
# Boot HP1 in SurrealEngine without the launcher, streaming the log to a file.
# Usage: tools/run_hp1.sh [seconds] [extra SurrealEngine args...]
#   HP1_GAME_DIR  game folder (default: ../eagames/hp1-work, a disposable copy of the retail install in ../eagames/hp1,
#                 made on first use; SurrealEngine writes ini and save files into it)
#   HP1_LOG       log file    (default: build/hp1_run.log)
#   HP1_WINDOW    windowed size for development runs, written into the game folder's SE-HP.ini before launch
#                 (default: 1280x720)
#   HP1_FULLSCREEN=1  borderless full screen at the desktop size instead (playtesting by hand); HP1_WINDOW sets the
#                 size if the desktop size can't be read
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
if [ -f "$INI" ]; then
	FULL=False
	WIN="${HP1_WINDOW:-1280x720}"
	if [ "${HP1_FULLSCREEN:-0}" = 1 ]; then
		# SurrealEngine's full screen is a borderless window covering the desktop; the viewport takes the
		# FullscreenViewport size, so give it the desktop's
		FULL=True
		DESK="$(powershell -NoProfile -Command 'Add-Type -AssemblyName System.Windows.Forms; $s = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; "$($s.Width)x$($s.Height)"' 2>/dev/null | tr -d '\r')" || true
		case "$DESK" in *x*) WIN="$DESK" ;; esac
	fi
	awk -v w="${WIN%x*}" -v h="${WIN#*x}" -v full="$FULL" '
		/^\[/ { client = ($0 ~ /^\[(Engine\.SurrealClient|WinDrv\.WindowsClient)\]/) }
		client && /^StartupFullscreen=/ { sub(/=.*/, "=" full) }
		client && full == "False" && /^WindowedViewportX=/ { sub(/=.*/, "=" w) }
		client && full == "False" && /^WindowedViewportY=/ { sub(/=.*/, "=" h) }
		client && full == "True" && /^FullscreenViewportX=/ { sub(/=.*/, "=" w) }
		client && full == "True" && /^FullscreenViewportY=/ { sub(/=.*/, "=" h) }
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
