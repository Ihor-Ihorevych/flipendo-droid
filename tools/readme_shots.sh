#!/usr/bin/env bash
# Retake the README screenshots (images/screenshots/) from fixed recipes, so they can be redone after a visual fix.
# Usage: tools/readme_shots.sh [name...]     (no names = all shots, then social-preview.jpg)
#   HP1_SHOTS_GAME  game folder used for the shots (default: ../eagames/hp1-shots, a disposable copy of ../eagames/hp1
#                   made on first use and set to a 1920x1080 window; the normal hp1-work copy keeps its own settings)
# Needs a Release build (tools/build.sh) and Python with Pillow. Each shot is its own run of the game (HP1_SHOTS,
# docs/debug-tools.md); a recipe is the map, the second it's taken at (since the first frame) and how it's cropped.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${HP1_SHOTS_GAME:-$ROOT/../eagames/hp1-shots}"
OUT="$ROOT/images/screenshots"
TMP="$ROOT/build/readme_shots"

# name | map | seconds | crop | extra environment
#   crop: letterbox = the picture between the cutscene bars (drops subtitles and "Press Space to skip"), full = the
#   whole 1920x1080 window. HP1_SKIPCUTS=1 skips the level's intro cutscene so gameplay starts at once.
SHOTS=(
	"grand-staircase|Lev_Tut1|4.0|letterbox|"           # intro cutscene: the kids run in under the house banners
	"dumbledore|Lev_Tut1|20.5|letterbox|"               # intro cutscene: Dumbledore comes down the stairs to Harry
	"hagrid|Lev2_HogFront|16.0|letterbox|"              # opening cutscene: Hagrid greets Harry in the grounds
	"castle-grounds|Lev2_HogFront|36.0|full|"           # first seconds of play: Hagrid walks ahead along the path
	"corridor|Lev3_Troll|4.0|full|HP1_SKIPCUTS=1"       # level start: the troll in the doorway at the end of the corridor
	"dungeon|Lev5_Chess|9.0|full|HP1_SKIPCUTS=1"        # level start: the torch-lit walkway
	"quidditch-pitch|Lev2_Quid1|17.5|letterbox|"        # opening flyover: the pitch from the stands
	"quidditch-fireworks|Lev2_Quid1|42.5|letterbox|"    # opening flyover: fireworks over the stands
)

[ -x "$ROOT/build/Release/SurrealEngine.exe" ] || { echo "build first: tools/build.sh" >&2; exit 1; }

# A game copy with a 1920x1080 window. SurrealEngine writes System/SE-HP.ini on a clean exit, so a first run quits at
# once to create it.
if [ ! -d "$GAME" ]; then
	echo "creating $GAME from ../eagames/hp1"
	cp -r "$ROOT/../eagames/hp1" "$GAME"
fi
if [ ! -f "$GAME/System/SE-HP.ini" ]; then
	HP1_GAME_DIR="$GAME" HP1_LOG="$TMP.log" HP1_BACKGROUND=1 HP1_EXEC="3:quit" "$ROOT/tools/run_hp1.sh" 20 --skip-splash >/dev/null
fi
sed -i 's/^WindowedViewportX=.*/WindowedViewportX=1920/; s/^WindowedViewportY=.*/WindowedViewportY=1080/; s/^StartupFullscreen=.*/StartupFullscreen=False/' "$GAME/System/SE-HP.ini"

want() { [ $# -eq 0 ] && return 0; local n; for n in "${WANTED[@]}"; do [ "$n" = "$1" ] && return 0; done; return 1; }
WANTED=("$@")
mkdir -p "$TMP" "$OUT"

for shot in "${SHOTS[@]}"; do
	IFS='|' read -r name map secs crop extra <<<"${shot%%#*}"
	name="$(echo "$name" | xargs)"; extra="$(echo "$extra" | xargs)"
	[ ${#WANTED[@]} -eq 0 ] || want "$name" || continue
	echo "$name: $map at ${secs}s ($crop)"
	rm -rf "$TMP/$name"; mkdir -p "$TMP/$name"
	secs_int=${secs%.*}
	env $extra HP1_GAME_DIR="$GAME" HP1_LOG="$TMP/$name.log" HP1_BACKGROUND=1 HP1_SHOTS="$secs" HP1_SHOT_DIR="$TMP/$name" \
		"$ROOT/tools/run_hp1.sh" $((secs_int + 4)) --skip-splash --url="$map" >/dev/null
	python - "$TMP/$name" "$crop" "$OUT/$name.jpg" <<'EOF'
import glob, sys
import numpy as np
from PIL import Image
folder, crop, out = sys.argv[1:4]
shots = glob.glob(folder + "/*.bmp")
if not shots:
    sys.exit("no screenshot taken (the game didn't reach that second?)")
img = Image.open(shots[0]).convert("RGB")
if crop == "letterbox":
    # The picture is the block of rows that are mostly not black around the middle of the window.
    rows = (np.asarray(img.convert("L")) > 8).mean(1) > 0.5
    mid = img.height // 2
    top, bottom = mid, mid
    while top > 0 and rows[top - 1]: top -= 1
    while bottom < img.height and rows[bottom]: bottom += 1
    img = img.crop((0, top, img.width, bottom))
img.save(out, quality=90)
print(f"  {out} {img.size[0]}x{img.size[1]}")
EOF
done

# The social preview (GitHub's 1280x640 card): the staircase shot with the logo.
if want social-preview || [ ${#WANTED[@]} -eq 0 ]; then
	python - "$OUT/grand-staircase.jpg" "$ROOT/images/branding/flipendo-logo.png" "$OUT/social-preview.jpg" <<'EOF'
import sys
from PIL import Image
shot, logo, out = sys.argv[1:4]
img = Image.open(shot).convert("RGB")
scale = max(1280 / img.width, 640 / img.height)
img = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
left, top = (img.width - 1280) // 2, (img.height - 640) // 2
img = img.crop((left, top, left + 1280, top + 640))
mark = Image.open(logo).convert("RGBA")
mark = mark.resize((620, round(mark.height * 620 / mark.width)), Image.LANCZOS)
img.paste(mark, ((1280 - mark.width) // 2, 640 - mark.height - 40), mark)
img.save(out, quality=90)
print(f"  {out} 1280x640")
EOF
fi
