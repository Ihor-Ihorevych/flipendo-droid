#!/usr/bin/env bash
# Builds what the PS2 tab needs and puts it in android/deps/ps2out, where build-apk.sh takes it from (docs/ps2.md):
#   libps2main.so    the recompiling runner (ps2-native-recomp, no game code in it), with SDL2's API
#   libSDL2ps2.so    SDL2's API over this app's SDL3 (sdl2-compat; renamed, the HP3 engine has a real libSDL2.so)
#   libps2clang.so, libps2lld.so, sysroot.zip   the compiler the phone translates a game with (android/ps2/build-toolchain.sh)
#
#   . /c/Users/Ihor/CFR2Recomp/env.sh
#   PS2RECOMP=/c/Users/Ihor/CFR2Recomp/ps2-native-recomp bash android/ps2/build-ps2.sh
# Needs the NDK 28 and CMake 3.22.1 of the Android SDK, git, and a ps2-native-recomp checkout with its Android wgpu-native in
# third_party/wgpu-native-android (scripts/fetch_wgpu.sh, or built from source as in that repository's README).
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
: "${PS2RECOMP:?PS2RECOMP names the ps2-native-recomp checkout}"
SDK="${ANDROID_HOME:-$(cygpath "$LOCALAPPDATA")/Android/Sdk}"
NDK="${ANDROID_NDK:-$(ls -d "$SDK"/ndk/28.* | sort | tail -n1)}"
CM="$SDK/cmake/3.22.1/bin"
DEPS="$ROOT/android/deps"
OUT="$DEPS/ps2out"
mkdir -p "$OUT"
JOBS="${PS2_JOBS:-$(nproc)}"

# 1. SDL2's API over SDL3: sdl2-compat finds SDL3 at run time (the app's libSDL3.so), so only SDL3's headers are needed to build it
if [ ! -d "$DEPS/sdl2-compat" ]; then
	git clone -q --depth 1 --branch release-2.32.66 https://github.com/libsdl-org/sdl2-compat "$DEPS/sdl2-compat"
	# the HP3 engine brings a real libSDL2.so: this one must not share its name
	printf '\nset_target_properties(SDL2 PROPERTIES OUTPUT_NAME "SDL2ps2")\n' >> "$DEPS/sdl2-compat/CMakeLists.txt"
fi
if [ ! -f "$DEPS/sdl2-compat-install/lib/libSDL2ps2.so" ]; then
	rm -rf "$DEPS/sdl2-compat-build"
	"$CM/cmake.exe" -S "$DEPS/sdl2-compat" -B "$DEPS/sdl2-compat-build" -G Ninja -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" \
		-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
		-DCMAKE_BUILD_TYPE=Release -DSDL3_INCLUDE_DIRS="$(cygpath -m "$DEPS/SDL/include")" -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF \
		-DSDL2COMPAT_X11=OFF -DSDL2COMPAT_INSTALL=ON -DCMAKE_INSTALL_PREFIX="$(cygpath -m "$DEPS/sdl2-compat-install")"
	"$CM/ninja.exe" -C "$DEPS/sdl2-compat-build" install
fi

# 2. the runner, with no game linked in (host/empty_game): the JNI names are those of Flipendo's Java classes
RB="$PS2RECOMP/build/android-ps2"
"$CM/cmake.exe" -S "$PS2RECOMP" -B "$RB" -G Ninja -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" \
	-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_SHARED_LINKER_FLAGS=-flto=thin -DPS2_GENERATED="$(cygpath -m "$PS2RECOMP/host/empty_game")" \
	-DPS2_WGPU_DIR="$(cygpath -m "$PS2RECOMP/third_party/wgpu-native-android")" \
	-DSDL2_DIR="$(cygpath -m "$DEPS/sdl2-compat-install/lib/cmake/SDL2")" -DPS2_ANDROID_LIB_NAME=ps2main \
	"-DCMAKE_CXX_FLAGS=-flto=thin -DPS2_JNI_TOUCH=io_github_flipendo_spike_Ps2Pad -DPS2_JNI_ACTIVITY=io_github_flipendo_spike_Ps2Activity"
"$CM/ninja.exe" -C "$RB" -j"$JOBS" ps2run
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
cp "$RB/libps2main.so" "$OUT/libps2main.so"
cp "$DEPS/sdl2-compat-install/lib/libSDL2ps2.so" "$OUT/libSDL2ps2.so"

# 3. the compiler (android/ps2/build-toolchain.sh made it; it takes a long time, so it is not started from here)
for f in libps2clang.so libps2lld.so sysroot.zip; do
	[ -f "$DEPS/ps2cc/out/$f" ] || { echo "missing $DEPS/ps2cc/out/$f: run android/ps2/build-toolchain.sh first"; exit 1; }
	cp "$DEPS/ps2cc/out/$f" "$OUT/$f"
done
# 4. the vector programs recorded for each game (games/<name>/vu1/<key>_<entry>.bin), under the disc serial the app looks them up by:
# derived from the game's disc, so they live only in deps/ (never committed)
rm -rf "$OUT/vu1seed"
for pair in hp2:SLES-51192; do
	name="${pair%%:*}" serial="${pair#*:}"
	if compgen -G "$PS2RECOMP/games/$name/vu1/*.bin" > /dev/null; then
		mkdir -p "$OUT/vu1seed/$serial" && cp "$PS2RECOMP/games/$name/vu1/"*.bin "$OUT/vu1seed/$serial/"
	fi
done
"$STRIP" --strip-unneeded "$OUT/libps2main.so" "$OUT/libSDL2ps2.so"
ls -la "$OUT"
