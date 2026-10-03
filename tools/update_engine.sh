#!/usr/bin/env bash
# Pull the latest SurrealEngine into engine/ (git subtree, full history), keeping our hp1_re changes.
#
# Usage: tools/update_engine.sh [ref]        ref defaults to upstream master
#        tools/update_engine.sh --check      only show what upstream has that we don't
#
# On conflicts: git stops mid-merge. Resolve (keep the hp1_re: blocks, take upstream for the rest),
# `git add` the files, `git commit`, then rebuild and rerun the audit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_URL="https://github.com/dpjudas/SurrealEngine.git"
PREFIX="engine"
cd "$ROOT"

CHECK=0
REF="master"
case "${1:-}" in
	--check) CHECK=1 ;;
	"") ;;
	*) REF="$1" ;;
esac

# A fresh clone of hp1_re has no 'upstream' remote - add it (fetch-only: pushing is disabled).
if ! git remote get-url upstream >/dev/null 2>&1; then
	git remote add upstream "$UPSTREAM_URL"
	git remote set-url --push upstream DISABLED
	echo "added remote 'upstream' -> $UPSTREAM_URL (push disabled)"
fi

git fetch --quiet upstream "$REF"
NEW="$(git rev-parse FETCH_HEAD)"

# Last upstream commit already merged into engine/.
CUR="$(git log --format=%H -n1 --grep="^git-subtree-split:" HEAD | xargs -r git log -n1 --format=%B | sed -n 's/^git-subtree-split: //p')"
if [ -z "$CUR" ]; then
	# Imported with `subtree add` from full history: upstream commits are ancestors of HEAD.
	CUR="$(git merge-base HEAD "$NEW" || true)"
fi

COUNT="$(git rev-list --count "${CUR:+$CUR..}$NEW")"
echo "engine/ at upstream ${CUR:0:8}, upstream $REF at ${NEW:0:8}: $COUNT new commit(s)"
if [ "$COUNT" = 0 ]; then
	exit 0
fi
git log --oneline --no-decorate "${CUR:+$CUR..}$NEW" | head -40

# Files we changed that upstream also touched - likely merge-conflict spots.
OURS="$(git grep -l "hp1_re:" -- "$PREFIX" || true)"
if [ -n "$OURS" ] && [ -n "$CUR" ]; then
	TOUCHED="$(git diff --name-only "$CUR" "$NEW" | sed "s|^|$PREFIX/|")"
	BOTH="$(comm -12 <(echo "$OURS" | sort) <(echo "$TOUCHED" | sort))"
	[ -n "$BOTH" ] && { echo; echo "upstream also changed these hp1_re-modified files:"; echo "$BOTH" | sed 's/^/  /'; }
fi

[ "$CHECK" = 1 ] && exit 0

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "working tree has uncommitted changes - commit or stash first" >&2
	exit 1
fi

git subtree pull --prefix="$PREFIX" upstream "$REF" \
	-m "Merge SurrealEngine upstream ${NEW:0:8} into $PREFIX/"

echo
echo "merged. next: tools/build.sh && python tools/native_audit.py && tools/run_hp1.sh 60"
