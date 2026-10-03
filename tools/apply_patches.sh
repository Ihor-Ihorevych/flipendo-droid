#!/usr/bin/env bash
# Apply patches/*.patch to the engine/ submodule's working tree (idempotent).
# The submodule itself is never committed to: our engine changes live only in patches/.
#   tools/apply_patches.sh           apply what isn't applied yet
#   tools/apply_patches.sh --reset   discard all working-tree changes in engine/ first, then apply
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
[ -f "$ENGINE/CMakeLists.txt" ] || git -C "$ROOT" submodule update --init engine
# Patches are made from the repo blobs (LF). A checkout made with the global core.autocrlf=true has CRLF
# files, so the first time we switch it off, re-checkout the submodule (nothing of ours is in it yet).
if [ "$(git -C "$ENGINE" config core.autocrlf || true)" != "false" ]; then
	git -C "$ENGINE" config core.autocrlf false
	git -C "$ENGINE" rm -q -r --cached .
	git -C "$ENGINE" reset -q --hard
fi

if [ "${1:-}" = "--reset" ]; then
	git -C "$ENGINE" checkout -q -- .
	git -C "$ENGINE" clean -qfd
fi

status=0
for p in "$ROOT"/patches/*.patch; do
	name="$(basename "$p")"
	if git -C "$ENGINE" apply --reverse --check "$p" 2>/dev/null; then
		echo "  applied   $name"
	elif git -C "$ENGINE" apply --check "$p" 2>/dev/null; then
		git -C "$ENGINE" apply "$p"
		echo "  applying  $name"
	else
		echo "  CONFLICT  $name - does not apply to engine/ at $(git -C "$ENGINE" rev-parse --short HEAD)" >&2
		git -C "$ENGINE" apply --check "$p" || true
		status=1
	fi
done
exit $status
