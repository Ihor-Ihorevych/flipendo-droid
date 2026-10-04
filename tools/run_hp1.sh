#!/usr/bin/env bash
# Boot HP1 in SurrealEngine without the launcher, streaming the log to a file.
# Usage: tools/run_hp1.sh [seconds] [extra SurrealEngine args...]
#   HP1_GAME_DIR  game folder (default: ../eagames/hp1-work, a disposable copy of the retail install in ../eagames/hp1,
#                 made on first use; SurrealEngine writes ini and save files into it)
#   HP1_LOG       log file    (default: build/hp1_run.log)
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

EXE="$ROOT/build/Release/SurrealEngine.exe"
[ -x "$EXE" ] || { echo "build first: tools/build.sh" >&2; exit 1; }

if [ "$SECS" -gt 0 ]; then
	timeout "$SECS" "$EXE" --autolaunch --logfile="$LOG" "$@" "$GAME" || true
else
	"$EXE" --autolaunch --logfile="$LOG" "$@" "$GAME"
fi
echo "log: $LOG"
grep -o "Unimplemented: [A-Za-z0-9_.]*" "$LOG" | sort | uniq -c | sort -rn || true
