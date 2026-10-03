#!/usr/bin/env bash
# Regenerate patches/*.patch from the engine/ submodule's working-tree changes.
# Workflow for changing an engine hook: tools/apply_patches.sh, edit the file under engine/, run this,
# commit patches/. Never commit inside engine/ (it's a mirror of upstream SurrealEngine).
# A changed file goes to the patch that already touches it; other files go to the *-hp1-hooks patch.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
cd "$ROOT"

python - "$ENGINE" "$ROOT/patches" <<'PY'
import os, re, subprocess, sys
engine, pdir = sys.argv[1], sys.argv[2]
patches = sorted(f for f in os.listdir(pdir) if f.endswith('.patch'))
owner, headers = {}, {}
for p in patches:
    text = open(os.path.join(pdir, p), encoding='utf-8').read()
    headers[p] = text.split('diff --git', 1)[0]
    for m in re.finditer(r'^diff --git a/(\S+) ', text, re.M):
        owner[m.group(1)] = p
changed = subprocess.run(['git', '-C', engine, 'diff', '--name-only'], capture_output=True, text=True, check=True).stdout.split()
untracked = subprocess.run(['git', '-C', engine, 'ls-files', '--others', '--exclude-standard'], capture_output=True, text=True, check=True).stdout.split()
if untracked:
    sys.exit('untracked files in engine/ (put new code in hp1/ instead): ' + ' '.join(untracked))
files = {p: [] for p in patches}
default = next((p for p in patches if p.endswith('-hp1-hooks.patch')), patches[-1])
for f in changed:
    files[owner.get(f, default)].append(f)
for p in patches:
    body = subprocess.run(['git', '-C', engine, 'diff', '--no-color', '--'] + files[p], capture_output=True, text=True, check=True).stdout if files[p] else ''
    if not body:
        print(f'  {p}: now empty, removing'); os.remove(os.path.join(pdir, p)); continue
    open(os.path.join(pdir, p), 'w', encoding='utf-8', newline='\n').write(headers[p] + body)
    print(f'  {p}: {len(files[p])} file(s)')
PY
