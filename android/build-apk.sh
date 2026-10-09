#!/usr/bin/env bash
# Build the Flipendo Android APK (arm64, debug-signed) without Gradle.
#   android/build-apk.sh              build android/dist/flipendo-droid.apk
#   android/build-apk.sh --install    ... and install it on the connected phone (adb)
# Needs: JDK 21, Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1), git.
# Override the locations with JAVA_HOME and ANDROID_HOME. See docs/android.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

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
OUT=android/dist/flipendo-droid.apk
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

# 4. the APK: libraries, dex (SDL's Java glue + our activity + touch overlay), manifest, engine UI resources
rm -rf "$A/lib" "$A/classes" "$A/assets"
mkdir -p "$A/lib/arm64-v8a" "$A/classes" "$A/assets" android/dist
for f in "$BUILD/libmain.so" "$BUILD/SDL3/libSDL3.so" "$BUILD/engine/Thirdparty/openal-soft/libopenal.so" "$BUILD/engine/libSurrealVideo.so"; do
	cp "$f" "$A/lib/arm64-v8a/"
done
"$STRIP" --strip-unneeded "$A"/lib/arm64-v8a/*.so
jar --create --no-manifest --file "$A/assets/SurrealEngine.pk3" -C src/engine/Resources .
javac -Xlint:-options -source 8 -target 8 -cp "$AJ" -d "$A/classes" \
	android/deps/SDL/android-project/app/src/main/java/org/libsdl/app/*.java android/java/io/github/flipendo/spike/*.java
"$BT/d8.bat" --lib "$AJ" --output "$A" $(find "$A/classes" -name "*.class")
"$BT/aapt2.exe" link -o "$A/base.apk" --manifest android/AndroidManifest.xml -I "$AJ" -A "$A/assets" --min-sdk-version 29 --target-sdk-version 35
[ -f android/debug.keystore ] || keytool -genkeypair -keystore android/debug.keystore -storepass android -keypass android \
	-alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Flipendo Debug"
(cd "$A" && cp base.apk unsigned.apk && jar uf unsigned.apk classes.dex lib && "$BT/zipalign.exe" -f -p 4 unsigned.apk aligned.apk)
"$BT/apksigner.bat" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android --out "$OUT" "$A/aligned.apk"
echo "built $OUT ($(du -h "$OUT" | cut -f1))"

if [ "${1:-}" = "--install" ]; then
	"$ADB" install -r "$OUT"
	"$ADB" shell appops set io.github.flipendo.spike MANAGE_EXTERNAL_STORAGE allow || true
fi
