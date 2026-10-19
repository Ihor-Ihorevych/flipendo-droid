#!/usr/bin/env bash
# Zip a built SurrealEngine for a GitHub release (used by .github/workflows/release.yml, also runs by hand).
#   tools/release/package.sh <windows|linux|macos> <build dir> <out dir> <tag>
# Windows: <build dir>/Release from the Visual Studio generator. Linux: a Ninja build configured with
# -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON (SurrealEngine finds libSurrealVideo.so next to itself). macOS: the .app bundle,
# whose dylibs (SurrealVideo, OpenAL) are copied into Contents/Frameworks and the bundle ad-hoc signed.
set -euo pipefail

PLATFORM="$1"
BUILD="$2"
OUT="$3"
TAG="$4"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENGINE="$ROOT/src/engine"
NAME="flipendo-$TAG-$PLATFORM"
STAGE="$OUT/stage/flipendo"

rm -rf "$OUT/stage"
mkdir -p "$STAGE/licenses"

case "$PLATFORM" in
windows)
	for f in SurrealEngine.exe SurrealEngine.pk3 SurrealVideo.dll OpenAL32.dll; do
		cp "$BUILD/Release/$f" "$STAGE/"
	done
	cp "$ENGINE/Thirdparty/openal-soft/COPYING" "$STAGE/licenses/OpenAL-Soft-LGPL.txt"
	;;
linux)
	for f in SurrealEngine SurrealEngine.pk3 libSurrealVideo.so; do
		cp "$BUILD/$f" "$STAGE/"
	done
	strip "$STAGE/SurrealEngine" "$STAGE/libSurrealVideo.so"
	# Must load libSurrealVideo.so from its own folder, not the build folder
	readelf -d "$STAGE/SurrealEngine" | grep -q 'R.*PATH.*\$ORIGIN' || { echo "SurrealEngine has no \$ORIGIN rpath" >&2; exit 1; }
	;;
macos)
	APP="$STAGE/Flipendo.app"
	cp -R "$BUILD/SurrealEngine.app" "$APP"
	# The bundle links SurrealVideo and OpenAL through @rpath, which reaches the build folder only on the build
	# machine: copy them in and check every @rpath library is in Contents/Frameworks.
	find "$BUILD" -maxdepth 3 -name '*.dylib' -not -path '*.app/*' -not -path '*_deps*' -exec cp -P {} "$APP/Contents/Frameworks/" \;
	for lib in $(otool -L "$APP/Contents/MacOS/SurrealEngine" | awk '/@rpath\//{print $1}'); do
		[ -e "$APP/Contents/Frameworks/${lib#@rpath/}" ] || { echo "missing in the bundle: $lib" >&2; exit 1; }
	done
	codesign --force --deep --sign - "$APP"
	cp "$ENGINE/Thirdparty/openal-soft/COPYING" "$STAGE/licenses/OpenAL-Soft-LGPL.txt"
	curl -fsSL https://raw.githubusercontent.com/KhronosGroup/MoltenVK/v1.4.2/LICENSE -o "$STAGE/licenses/MoltenVK-Apache-2.0.txt"
	;;
*)
	echo "unknown platform: $PLATFORM" >&2
	exit 1
	;;
esac

cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ENGINE/LICENSE.md" "$STAGE/licenses/SurrealEngine-and-third-party.md"
cp "$ENGINE/SurrealGPU/LICENSE.md" "$STAGE/licenses/SurrealGPU.md"
cp "$ENGINE/SurrealWidgets/LICENSE.md" "$STAGE/licenses/SurrealWidgets.md"
cp "$ENGINE/SurrealVideo/COPYING.LGPLv2.1" "$STAGE/licenses/SurrealVideo-LGPLv2.1.txt"
cp "$ENGINE/SurrealGPU/src/glslang/LICENSE.txt" "$STAGE/licenses/glslang.txt"
cp "$ENGINE/Thirdparty/schrift/LICENSE.txt" "$STAGE/licenses/schrift.txt"
sed "s|@TAG@|$TAG|g" "$ROOT/tools/release/README-first-$PLATFORM.txt" > "$STAGE/README-first.txt"
if [ "$PLATFORM" = windows ]; then
	sed -i 's/$/\r/' "$STAGE/README-first.txt"
fi

rm -f "$OUT/$NAME.zip"
if [ "$PLATFORM" = windows ]; then
	(cd "$OUT/stage" && 7z a -tzip -bso0 "../$NAME.zip" flipendo)
elif [ "$PLATFORM" = macos ]; then
	# ditto keeps the bundle's symlinks and signature intact
	(cd "$OUT/stage" && ditto -c -k --keepParent flipendo "../$NAME.zip")
else
	(cd "$OUT/stage" && zip -qry "../$NAME.zip" flipendo)
fi
rm -rf "$OUT/stage"
echo "$OUT/$NAME.zip"
