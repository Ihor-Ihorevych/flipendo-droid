#!/usr/bin/env bash
# Move the engine/ submodule (mirror of upstream SurrealEngine) to a newer commit and re-apply patches/.
#   tools/update_engine.sh --check   list new upstream commits and which patched files they touch
#   tools/update_engine.sh [ref]     update to ref (default: origin/master), re-apply patches
# On CONFLICT: fix the hook in engine/ by hand, run tools/refresh_patches.sh, rebuild.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
cd "$ROOT"

git submodule update --init engine
git -C "$ENGINE" fetch -q origin

CHECK=0; REF="origin/master"
case "${1:-}" in --check) CHECK=1 ;; "") ;; *) REF="$1" ;; esac

CUR="$(git -C "$ENGINE" rev-parse HEAD)"
NEW="$(git -C "$ENGINE" rev-parse "$REF")"
COUNT="$(git -C "$ENGINE" rev-list --count "$CUR..$NEW")"
echo "engine/ at ${CUR:0:8}, $REF at ${NEW:0:8}: $COUNT new commit(s)"
[ "$COUNT" = 0 ] && exit 0
git -C "$ENGINE" log --oneline --no-decorate "$CUR..$NEW" | head -40

PATCHED="$(grep -h '^diff --git a/' patches/*.patch | awk '{print substr($3,3)}' | sort -u)"
TOUCHED="$(git -C "$ENGINE" diff --name-only "$CUR" "$NEW" | sort -u)"
BOTH="$(comm -12 <(echo "$PATCHED") <(echo "$TOUCHED"))"
[ -n "$BOTH" ] && { echo; echo "upstream also changed these patched files:"; echo "$BOTH" | sed 's/^/  /'; }

[ "$CHECK" = 1 ] && exit 0

git -C "$ENGINE" checkout -q -- . && git -C "$ENGINE" clean -qfd
git -C "$ENGINE" checkout -q "$NEW"
tools/apply_patches.sh
git add engine
echo
echo "engine/ -> ${NEW:0:8}, patches applied. next: tools/build.sh && tools/run_hp1.sh 60, then commit."
