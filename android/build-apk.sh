#!/usr/bin/env bash
# Build the Flipendo Android APK (arm64, debug-signed) without Gradle.
#   android/build-apk.sh                          android/dist/flipendo-droid.apk  (no game data: copy the game to the phone)
#   android/build-apk.sh --selfpack "C:/Games/HP" ... and android/dist/flipendo-droid-selfpack.apk, which carries
#                                                 the game from that folder and unpacks it on first launch
#   android/build-apk.sh --install                ... and install the plain APK on the connected phone (adb)
# The self pack contains copyrighted game data: for your own phone only, never publish or share it.
# Needs: JDK 21, Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1), git.
# Override the locations with JAVA_HOME and ANDROID_HOME. See docs/android.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INSTALL=0
SELFPACK=""
while [ $# -gt 0 ]; do
	case "$1" in
		--install) INSTALL=1 ;;
		--selfpack) shift; SELFPACK="$(cygpath -u "${1:?--selfpack needs the game folder}")" ;;
		*) echo "unknown argument: $1" >&2; exit 1 ;;
	esac
	shift
done
if [ -n "$SELFPACK" ] && [ ! -f "$SELFPACK/System/HP.exe" ] && [ ! -f "$SELFPACK/system/HP.exe" ]; then
	echo "$SELFPACK is not an HP1 install (no System/HP.exe)" >&2
	exit 1
fi

export ANDROID_HOME="$(cygpath -u "${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}")"
export JAVA_HOME="$(cygpath -u "${JAVA_HOME:-C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot}")"
export PATH="$JAVA_HOME/bin:$PATH"
BT="$ANDROID_HOME/build-tools/35.0.0"
AJ="$ANDROID_HOME/platforms/android-35/android.jar"
NDK="$ANDROID_HOME/ndk/27.2.12479018"
CMAKE_DIR="$ANDROID_HOME/cmake/3.22.1/bin"
HOST_TAG=windows-x86_64
STRIP="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/llvm-strip.exe"
ADB="$ANDROID_HOME/platform-tools/adb.exe"

A=android/apk
BUILD=build-android

# 1. SDL3 (window, input, Android glue): fetched once, pinned to a release
if [ ! -d android/deps/SDL ]; then
	mkdir -p android/deps
	git clone --depth 1 --branch release-3.2.20 https://github.com/libsdl-org/SDL android/deps/SDL
fi

# 2. the engine with Flipendo's patches (src/surreal-patches/, including 0200-android-port.patch)
# apply_patches.sh is only idempotent for non-overlapping patches; 0200 edits files other patches touch, so a tree
# that already carries it is left alone, and anything else is rebuilt from a clean checkout.
if ! grep -q SE_CLIP_DISTANCE_DEFINE src/engine/SurrealEngine/RenderDevice/Vulkan/ShaderManager.cpp 2>/dev/null; then
	tools/apply_patches.sh --reset
fi

# 3. native code
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
	"$CMAKE_DIR/cmake.exe" -S android -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$CMAKE_DIR/ninja.exe" \
		-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
		-DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
fi
"$CMAKE_DIR/cmake.exe" --build "$BUILD" --target main

# 4. what both APKs share: libraries, dex (SDL's Java glue + our activities + touch overlay), engine UI resources
rm -rf "$A/lib" "$A/classes" "$A/assets" "$A/data_stage"
mkdir -p "$A/lib/arm64-v8a" "$A/classes" "$A/assets" android/dist
for f in "$BUILD/libmain.so" "$BUILD/SDL3/libSDL3.so" "$BUILD/engine/Thirdparty/openal-soft/libopenal.so" "$BUILD/engine/libSurrealVideo.so"; do
	cp "$f" "$A/lib/arm64-v8a/"
done
# keep the unstripped library: frames in a user's crash.txt are module+offset, symbolicate them against it
mkdir -p android/dist/symbols
cp "$BUILD/libmain.so" android/dist/symbols/libmain.unstripped.so
git describe --always --dirty > android/dist/symbols/build-info.txt
date +%F >> android/dist/symbols/build-info.txt
paste -sd' ' android/dist/symbols/build-info.txt > "$A/assets/build-info.txt"
"$STRIP" --strip-unneeded "$A"/lib/arm64-v8a/*.so
jar --create --no-manifest --file "$A/assets/SurrealEngine.pk3" -C src/engine/Resources .
javac -Xlint:-options -source 8 -target 8 -cp "$AJ" -d "$A/classes" \
	android/deps/SDL/android-project/app/src/main/java/org/libsdl/app/*.java android/java/io/github/flipendo/spike/*.java
"$BT/d8.bat" --lib "$AJ" --output "$A" $(find "$A/classes" -name "*.class")
[ -f android/debug.keystore ] || keytool -genkeypair -keystore android/debug.keystore -storepass android -keypass android \
	-alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Flipendo Debug"

# package <output apk> [extra aapt2 -A asset directory]
package() {
	local out="$1"; shift
	local assets=(-A "$A/assets")
	[ $# -gt 0 ] && assets+=(-A "$1")
	rm -f "$A/base.apk" "$A/unsigned.apk" "$A/aligned.apk"
	"$BT/aapt2.exe" link -o "$A/base.apk" --manifest android/AndroidManifest.xml -I "$AJ" "${assets[@]}" \
		--min-sdk-version 29 --target-sdk-version 35
	(cd "$A" && cp base.apk unsigned.apk && jar uf unsigned.apk classes.dex lib && "$BT/zipalign.exe" -f -p 4 unsigned.apk aligned.apk)
	"$BT/apksigner.bat" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android --out "$out" "$A/aligned.apk"
	rm -f "$out.idsig"
	echo "built $out ($(du -h "$out" | cut -f1))"
}

# 5. the plain APK: the game is copied to /sdcard/FlipendoHP/ by hand
package android/dist/flipendo-droid.apk

# 6. the self pack: the same APK plus the game, unpacked on first launch (SetupActivity)
if [ -n "$SELFPACK" ]; then
	STAGE="$A/data_stage/gamedata"
	rm -rf "$A/data_stage"
	mkdir -p "$STAGE"
	# Everything the engine reads, except SafeDisc files, the uninstaller, shortcuts, logs and saves (the save
	# thumbnails stay: the slot page needs them).
	(cd "$SELFPACK" && find . -type f \
		! -iname 'drvmgt.dll' ! -iname 'secdrv.sys' ! -iname 'uninst.*' ! -iname '*.lnk' ! -iname '*.log' \
		! -iname 'NEW.txt' ! -iname '*.usa' ! -iname 'GameSaveInfo*' ! -iname 'SE-*.ini' ! -iname 'User.ini' ! -iname 'flipendo*' \
		-print0 | tar --null -cf - --files-from=-) | (cd "$STAGE" && tar xf -)
	# a stamp of what is inside: the app only unpacks again when the data changes
	(cd "$STAGE" && find . -type f ! -name .stamp -printf '%P %s\n' | sort | sha1sum | cut -d' ' -f1) > "$STAGE/.stamp"
	echo "game data: $(find "$STAGE" -type f | wc -l) files, $(du -sm "$STAGE" | cut -f1) MB"
	package android/dist/flipendo-droid-selfpack.apk "$A/data_stage"
	rm -rf "$A/data_stage"
fi

if [ "$INSTALL" = 1 ]; then
	"$ADB" install -r android/dist/flipendo-droid.apk
	"$ADB" shell appops set io.github.flipendo.spike MANAGE_EXTERNAL_STORAGE allow || true
fi
