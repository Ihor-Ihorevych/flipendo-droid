#!/usr/bin/env bash
# Regenerate patches/*.patch from the engine/ submodule's working-tree changes.
# Workflow for changing an engine hook: tools/apply_patches.sh, edit the file under engine/, run this,
# commit patches/. Never commit inside engine/ (it's a mirror of SurrealEngine).
# Which patch a changed file goes to is decided by patches/routes.txt (first matching glob wins); a changed file
# that no route matches stops the refresh. Patch files that routes.txt doesn't list, or that end up empty, are removed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
cd "$ROOT"

python - "$ENGINE" "$ROOT/patches" <<'PY'
import os, re, subprocess, sys
engine, pdir = sys.argv[1], sys.argv[2]

def glob_to_regex(glob):
    out = ''
    i = 0
    while i < len(glob):
        if glob.startswith('**', i):
            out += '.*'; i += 2
        elif glob[i] == '*':
            out += '[^/]*'; i += 1
        else:
            out += re.escape(glob[i]); i += 1
    return re.compile(out + '$')

routes, order, desc = [], [], {}
current = None
for line in open(os.path.join(pdir, 'routes.txt'), encoding='utf-8'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    m = re.match(r'\[(\S+\.patch)\]\s*(.*)', line)
    if m:
        current = m.group(1)
        order.append(current)
        desc[current] = m.group(2)
    elif current:
        routes.append((glob_to_regex(line), current))
    else:
        sys.exit('routes.txt: glob before any [patch] header: ' + line)

changed = subprocess.run(['git', '-C', engine, 'diff', '--name-only'], capture_output=True, text=True, check=True).stdout.split()
untracked = subprocess.run(['git', '-C', engine, 'ls-files', '--others', '--exclude-standard'], capture_output=True, text=True, check=True).stdout.split()
if untracked:
    sys.exit('untracked files in engine/ (put new code in hp1/ instead): ' + ' '.join(untracked))

files = {p: [] for p in order}
unrouted = []
for f in changed:
    owner = next((p for rx, p in routes if rx.match(f)), None)
    if owner:
        files[owner].append(f)
    else:
        unrouted.append(f)
if unrouted:
    sys.exit('no route in patches/routes.txt for: ' + ' '.join(unrouted))

for p in sorted(f for f in os.listdir(pdir) if f.endswith('.patch')):
    if p not in files:
        print(f'  {p}: not in routes.txt, removing'); os.remove(os.path.join(pdir, p))
for p in order:
    path = os.path.join(pdir, p)
    body = subprocess.run(['git', '-C', engine, 'diff', '--no-color', '--'] + files[p], capture_output=True, text=True, check=True).stdout if files[p] else ''
    if not body:
        if os.path.exists(path):
            print(f'  {p}: now empty, removing'); os.remove(path)
        continue
    open(path, 'w', encoding='utf-8', newline='\n').write(desc[p] + '\n\n' + body)
    print(f'  {p}: {len(files[p])} file(s)')
PY
