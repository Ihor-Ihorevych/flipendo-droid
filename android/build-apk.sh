#!/usr/bin/env bash
# Build the Flipendo Android APK (arm64, debug-signed) without Gradle.
#   android/build-apk.sh                          android/dist/flipendo-droid.apk  (no game data: copy the game to the phone)
#   android/build-apk.sh --selfpack "C:/Games/HP" ... and android/dist/flipendo-droid-selfpack.apk, which carries
#                                                 the game from that folder and unpacks it on first launch
#   android/build-apk.sh --selfpack-ru "C:/Program Files/HPFarg"   ... and flipendo-droid-selfpack-ru.apk: the same with
#                                                 another install (the Russian one), under its own file name
#   android/build-apk.sh --selfpack "C:/Games/HP" --lang ru="C:/Program Files/HPFarg"
#                                                 one self pack with both languages; Settings > Language switches them in game
#   android/build-apk.sh --selfpack "C:/Games/HP" --mod movement=movement.zip
#                                                 every APK carries the mod (replacement .u packages, Settings switches it); movement.zip by default
#   android/build-apk.sh --selfpack "C:/Games/HP" --textures texture-packs/storybook-hd --menu-art texture-packs/menu-art
#                                                 .dds texture packs (Textures/<package>/<group>/<name>.dds, the texture-overrides mod); menu art per language
#   android/build-apk.sh --selfpack "C:/Games/HP" --hp2 "<HP2 folder>" [--hp2-texts "<harry2 folder>"]   ... HP2 in the self pack too
#   android/build-apk.sh --selfpack "C:/Games/HP" --hp3 "<HP3 folder>" [--hp3-eng "<English HP3 folder>"]   ... HP3 in the self pack too
#                                                 (--hp3 is the base, the Russian edition; --hp3-eng adds the English voices, texts and art: the launcher's language picks one)
#   android/build-apk.sh --selfpack "C:/Games/HP" --ps1-hp1 HP1.chd --ps1-hp2 HP2.chd   ... the PS1 games' discs in the self pack too
#                                                 (CHD: chdman createcd -i game.cue -o game.chd; unpacked when the PS1 tab's Play is first pressed)
#   android/build-apk.sh --install                ... and install the plain APK on the connected phone (adb)
# Every APK carries Harry Potter 3's engine: by default the Rust engine of the sibling repository (--hp3-rust, default
# ../hp3-engine), built with cargo for aarch64-linux-android and linked against this APK's own libSDL3.so. Needs rustup with
# the aarch64-linux-android target. --hp3-legacy builds the older C++ engine instead, from the sibling engine repository
# (--hp3-engine, default src/hp3-engine) with HP3's generated headers (--hp3-headers, default ../hp3-headers) and the engine's
# dependency sources (--hp3-deps, default <engine>/build-windows-hp3-layout/_deps: sdl2-src, gl4es-src, metoolkitkarmadecomp-src).
# The self pack contains copyrighted game data: for your own phone only, never publish or share it.
# Needs: JDK 21, Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018 and, for HP3, 28.2.13676358,
# CMake 3.22.1 and, for HP3, 4.1.2), git.
# Override the locations with JAVA_HOME and ANDROID_HOME. See docs/android.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INSTALL=0
SELFPACKS=() # "game folder|apk name"
HP2DIR=""      # --hp2: an HP2 install (System/Game.exe) that goes into the self pack too (assets/hp2data)
HP2TEXTS=""    # --hp2-texts: HP2's Russian text files (the harry2 folder of the Russian edition)
HP2EDITIONS=() # "code=folder|name": an HP2 release whose voices and texts are an edition the player can choose (--hp2-edition)
LANGS=()     # "code=game folder": more languages for --selfpack (the in-game language switch)
ADDLANGS=()  # "code=game folder": another release's language files only (texts, voices, menu art; --lang-add), on top of the base build
MODS=()      # "id=zip": script mods the app can switch on and off (Settings > Movement mod)
TEXTURES=()  # --textures (repeatable, later ones win): folders with a Textures/ tree of .dds overrides for the self pack (ESRGAN pack, HD storybook pages)
MENUART=""   # --menu-art: the main menu upscale pack, one folder per language, each with Textures/MenuArt/*.dds
VOICES=()    # "code=Sounds folder": a dubbing for --selfpack, an edition that is the base install with its Sounds replaced
HP3DIR=""    # --hp3: an HP3 install (system/Core.u) that goes into the self pack too (assets/hp3data)
PS1_HP1=""   # --ps1-hp1: the PS1 Harry Potter 1 disc image (a CHD: chdman createcd) that goes into the self pack (assets/ps1data/hp1)
PS1_HP2=""   # --ps1-hp2: the same for PS1 Harry Potter 2
HP3CACHEZIP="" # --hp3-cache-zip: also write HP3's data (as staged for the self pack, Movies included) as a zip with a FlipendoHP3 folder, to unpack into the phone's storage root for a light APK
HP3ENG=""    # --hp3-eng: the English retail HP3 install, added to the base one as a second language (voices, texts, localised art)
HP3_ENGINE="$ROOT/src/hp3-engine" # --hp3-engine: the engine repository (flipendo-droid-2004, branch hp3; the submodule by default, or any other checkout)
HP3_HEADERS="$(cd "$ROOT/.." && pwd)/hp3-headers"        # --hp3-headers: HP3's generated EngineClasses.h (never in git)
HP3_DEPS=""  # --hp3-deps: the engine's dependency sources (default: <engine>/build-windows-hp3-layout/_deps)
HP3_RUST="$(cd "$ROOT/.." && pwd)/hp3-engine" # --hp3-rust: the Rust engine repository (hp3-engine)
HP3_LEGACY=0 # --hp3-legacy: the C++ engine instead of the Rust one
HP3_LIBS=""  # --hp3-libs: a folder of prebuilt HP3 libraries (libUT2004.so, libSDL2.so, libut2004_android_bridge.so, libc++_shared.so): no HP3 build then
while [ $# -gt 0 ]; do
	case "$1" in
		--install) INSTALL=1 ;;
		--hp3) shift; HP3DIR="$(cygpath -u "${1:?--hp3 needs the HP3 folder}")" ;;
		--hp3-eng) shift; HP3ENG="$(cygpath -u "${1:?--hp3-eng needs the English HP3 folder}")" ;;
		--hp3-cache-zip) shift; HP3CACHEZIP="${1:?--hp3-cache-zip needs the zip file to write}" ;;
		--hp3-rust) shift; HP3_RUST="$(cygpath -u "${1:?--hp3-rust needs the Rust engine repository}")" ;;
		--hp3-legacy) HP3_LEGACY=1 ;;
		--hp3-engine) shift; HP3_ENGINE="$(cygpath -u "${1:?--hp3-engine needs the engine repository}")" ;;
		--hp3-headers) shift; HP3_HEADERS="$(cygpath -u "${1:?--hp3-headers needs the header folder}")" ;;
		--hp3-deps) shift; HP3_DEPS="$(cygpath -u "${1:?--hp3-deps needs the _deps folder}")" ;;
		--hp3-libs) shift; HP3_LIBS="$(cygpath -u "${1:?--hp3-libs needs the folder of prebuilt HP3 libraries}")" ;;
		--hp2) shift; HP2DIR="$(cygpath -u "${1:?--hp2 needs the HP2 folder}")" ;;
		--ps1-hp1) shift; PS1_HP1="$(cygpath -u "${1:?--ps1-hp1 needs a disc image (a .chd)}")" ;;
		--ps1-hp2) shift; PS1_HP2="$(cygpath -u "${1:?--ps1-hp2 needs a disc image (a .chd)}")" ;;
		--hp2-edition) shift; HP2EDITIONS+=("$1") ;; # code=folder|name, e.g. ru=C:/Games/HP2Rus|Russian
		--hp2-texts) shift; HP2TEXTS="$(cygpath -u "${1:?--hp2-texts needs the harry2 folder}")" ;;
		--selfpack) shift; SELFPACKS+=("$(cygpath -u "${1:?--selfpack needs the game folder}")|flipendo-droid-selfpack.apk") ;;
		--selfpack-ru) shift; SELFPACKS+=("$(cygpath -u "${1:?--selfpack-ru needs the game folder}")|flipendo-droid-selfpack-ru.apk") ;;
		--lang) shift; LANGS+=("$(echo "${1:?--lang needs code=game folder (e.g. ru=D:/HP-Russian)}" | sed 's|=.*||')=$(cygpath -u "$(echo "$1" | sed 's|^[^=]*=||')")") ;;
		--lang-add) shift; ADDLANGS+=("$(echo "${1:?--lang-add needs code=game folder (e.g. de=\"C:/Games/Game Files HP1 German\")}" | sed 's|=.*||')=$(cygpath -u "$(echo "$1" | sed 's|^[^=]*=||')")") ;;
		--mod) shift; MODS+=("${1:?--mod needs id=mod.zip (e.g. movement=movement.zip)}") ;;
		--textures) shift; TEXTURES+=("$(cygpath -u "${1:?--textures needs a folder with Textures/}")") ;;
		--menu-art) shift; MENUART="$(cygpath -u "${1:?--menu-art needs the folder with one subfolder per language}")" ;;
		--voice) shift; VOICES+=("$(echo "${1:?--voice needs code=Sounds folder (e.g. ru-softclub=Sounds-Rus-Softclub)}" | sed 's|=.*||')=$(cygpath -u "$(echo "$1" | sed 's|^[^=]*=||')")") ;;
		*) echo "unknown argument: $1" >&2; exit 1 ;;
	esac
	shift
done
for entry in "${SELFPACKS[@]}"; do
	dir="${entry%|*}"
	if [ ! -f "$dir/System/HP.exe" ] && [ ! -f "$dir/system/HP.exe" ]; then
		echo "$dir is not an HP1 install (no System/HP.exe)" >&2
		exit 1
	fi
done

if [ -n "$HP2DIR" ] && [ ! -f "$HP2DIR/System/Game.exe" ] && [ ! -f "$HP2DIR/system/Game.exe" ]; then
	echo "$HP2DIR is not an HP2 install (no System/Game.exe)" >&2
	exit 1
fi
if [ -n "$HP3DIR" ] && [ ! -f "$HP3DIR/System/Core.u" ] && [ ! -f "$HP3DIR/system/Core.u" ]; then
	echo "$HP3DIR is not an HP3 install (no system/Core.u)" >&2
	exit 1
fi
HP3_DEPS="${HP3_DEPS:-$HP3_ENGINE/build-windows-hp3-layout/_deps}"
# HP3's engine comes from prebuilt libraries (--hp3-libs), from a build of its source (needs the generated headers, which
# come from a game install and are never in git), or is left out: the APK then has no HP3 and the launcher hides it.
CARGO="$(cygpath -u "${CARGO_HOME:-$USERPROFILE/.cargo}")/bin/cargo.exe"
HP3_MODE=off
if [ "$HP3_LEGACY" = 0 ] && [ -z "$HP3_LIBS" ] && [ -f "$HP3_RUST/Cargo.toml" ] && [ -x "$CARGO" ]; then
	HP3_MODE=rust
elif [ -n "$HP3_LIBS" ]; then
	for l in libUT2004.so libSDL2.so libut2004_android_bridge.so libc++_shared.so; do
		if [ ! -f "$HP3_LIBS/$l" ]; then
			echo "--hp3-libs: $HP3_LIBS has no $l" >&2
			exit 1
		fi
	done
	HP3_MODE=libs
elif [ -d "$HP3_ENGINE/Source" ] && [ -d "$HP3_HEADERS" ] && [ -d "$HP3_DEPS/sdl2-src" ] && [ -d "$HP3_DEPS/gl4es-src" ] && [ -d "$HP3_DEPS/metoolkitkarmadecomp-src" ]; then
	HP3_MODE=build
else
	echo "HP3 left out: no --hp3-libs, and no engine source with generated headers and dependencies (see --hp3-engine, --hp3-headers, --hp3-deps)" >&2
fi
if [ -n "$HP3DIR" ] && [ "$HP3_MODE" = off ]; then
	echo "--hp3 needs HP3's engine: give --hp3-libs or the engine source" >&2
	exit 1
fi
HP3_JAVA_DIR="$HP3_ENGINE/Android/app/src/main/java"
[ -d "$HP3_JAVA_DIR" ] || { [ "$HP3_MODE" = off ] || [ "$HP3_MODE" = rust ] || { echo "HP3's engine sources not found at $HP3_ENGINE (git submodule update --init src/hp3-engine)" >&2; exit 1; }; }

for l in "${LANGS[@]}" "${ADDLANGS[@]}"; do
	dir="${l#*=}"
	if [ ! -f "$dir/System/HP.exe" ] && [ ! -f "$dir/system/HP.exe" ]; then
		echo "$dir is not an HP1 install (no System/HP.exe)" >&2
		exit 1
	fi
done
# the language extension of a --lang-add install: its Default.ini says Language=<ext> (ger), and it must name a language
addlang_ext() {
	local ini
	ini="$(ls "$1"/System/Default.ini "$1"/system/Default.ini "$1"/System/default.ini 2>/dev/null | head -1)"
	tr -d '\r' < "$ini" | sed -n 's/^Language=//p' | head -1 | tr 'A-Z' 'a-z'
}
for l in "${ADDLANGS[@]}"; do
	ext="$(addlang_ext "${l#*=}")"
	if [ -z "$ext" ] || [ "$ext" = int ]; then
		echo "${l#*=}: Default.ini has no Language other than int, so there is nothing to add" >&2
		exit 1
	fi
done
# the files of a language: the texts (System/*.<ext>), the voices (Sounds/*.<ext>_uax), the menu art (Textures/*.<ext>_utx)
addlang_files() {
	(cd "$1" && find . -type f \( -ipath './system/*' -o -ipath './sounds/*' -o -ipath './textures/*' -o -ipath './music/*' \) \
		\( -iname "*.$2" -o -iname "*.$2_*" \) -print0)
}

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

# 1b. SwanStation (GPLv3), the PS1 emulator core: fetched once, pinned to a commit, built below as a libretro core
PS1_CORE_SHA=631f0fb31b809e0f889d7c551fe695788e39c1d3
if [ ! -d android/deps/swanstation/.git ]; then
	mkdir -p android/deps/swanstation
	git -C android/deps/swanstation init -q
	git -C android/deps/swanstation remote add origin https://github.com/libretro/swanstation.git
fi
if [ "$(git -C android/deps/swanstation rev-parse -q --verify HEAD 2>/dev/null || true)" != "$PS1_CORE_SHA" ]; then
	git -C android/deps/swanstation fetch --depth 1 origin "$PS1_CORE_SHA"
	git -C android/deps/swanstation checkout -q --force "$PS1_CORE_SHA"
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
"$CMAKE_DIR/cmake.exe" --build "$BUILD" --target main ps1host

# 3a. the PS1 core as a libretro shared library (its own CMake project; the AArch64 recompiler is on for Android arm64)
if [ ! -f "$BUILD/ps1core/CMakeCache.txt" ]; then
	"$CMAKE_DIR/cmake.exe" -S android/deps/swanstation -B "$BUILD/ps1core" -G Ninja -DCMAKE_MAKE_PROGRAM="$CMAKE_DIR/ninja.exe" \
		-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
		-DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
fi
"$CMAKE_DIR/cmake.exe" --build "$BUILD/ps1core" --target swanstation_libretro

# 3b. Harry Potter 3's engine (flipendo-droid-2004): its own SDL2, under the Java package org.libsdl2.app so that it sits
# beside SDL3, built with the engine's NDK and CMake and the arguments its Gradle build passes
HP3_NDK="$ANDROID_HOME/ndk/28.2.13676358"
HP3_CMAKE="$ANDROID_HOME/cmake/4.1.2/bin"
HP3_BUILD=build-android-hp3
if [ "$HP3_MODE" = build ] && [ ! -f "$HP3_BUILD/CMakeCache.txt" ]; then
	"$HP3_CMAKE/cmake.exe" -S "$HP3_ENGINE" -B "$HP3_BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$HP3_CMAKE/ninja.exe" \
		-DCMAKE_TOOLCHAIN_FILE="$HP3_NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
		-DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DUE_GAME_HP3=ON -DUE_HP3_HEADERS_DIR="$(cygpath -m "$HP3_HEADERS")" -DUE_SDL2_JAVA_PACKAGE=org.libsdl2.app \
		-DFETCHCONTENT_SOURCE_DIR_SDL2="$(cygpath -m "$HP3_DEPS/sdl2-src")" \
		-DFETCHCONTENT_SOURCE_DIR_GL4ES="$(cygpath -m "$HP3_DEPS/gl4es-src")" \
		-DFETCHCONTENT_SOURCE_DIR_METOOLKITKARMADECOMP="$(cygpath -m "$HP3_DEPS/metoolkitkarmadecomp-src")"
fi
if [ "$HP3_MODE" = build ]; then
	"$HP3_CMAKE/cmake.exe" --build "$HP3_BUILD" --target SDLLaunch ut2004_android_bridge
	# the four libraries, ready to publish as the prebuilt set for a build without the game files (--hp3-libs)
	rm -rf android/dist/hp3-libs
	mkdir -p android/dist/hp3-libs
	cp "$HP3_BUILD/Source/SDLLaunch/libUT2004.so" "$HP3_BUILD/_deps/sdl2-build/libSDL2.so" "$HP3_BUILD/libut2004_android_bridge.so" \
		"$HP3_NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" android/dist/hp3-libs/
	HP3_LIBS="$ROOT/android/dist/hp3-libs"
fi

# 3c. Harry Potter 3's Rust engine: one shared library, linked against this APK's libSDL3.so (the same SDL, so its Java
# glue and the native side match); built with the NDK's clang as the linker
if [ "$HP3_MODE" = rust ]; then
	RUST_NDK_BIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
	env CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$RUST_NDK_BIN/aarch64-linux-android29-clang.cmd" \
		CC_aarch64_linux_android="$RUST_NDK_BIN/aarch64-linux-android29-clang.cmd" AR_aarch64_linux_android="$RUST_NDK_BIN/llvm-ar.exe" \
		RUSTFLAGS="-L $(cygpath -m "$ROOT/$BUILD/SDL3")" \
		"$CARGO" build --release --target aarch64-linux-android -p hp3 --lib --manifest-path "$HP3_RUST/Cargo.toml"
	HP3_RUST_SO="$HP3_RUST/target/aarch64-linux-android/release/libhp3app.so"
fi

# 4. what both APKs share: libraries, dex (SDL's Java glue + our activities + touch overlay), engine UI resources
rm -rf "$A/lib" "$A/classes" "$A/assets" "$A/data_stage"
mkdir -p "$A/lib/arm64-v8a" "$A/classes" "$A/assets" android/dist
for f in "$BUILD/libmain.so" "$BUILD/SDL3/libSDL3.so" "$BUILD/engine/Thirdparty/openal-soft/libopenal.so" "$BUILD/engine/libSurrealVideo.so"; do
	cp "$f" "$A/lib/arm64-v8a/"
done
# the PS1 emulator: the host (Ps1Activity's entry) and the SwanStation core it loads by name (lib*.so, so it is unpacked next to the others)
cp "$BUILD/libps1host.so" "$A/lib/arm64-v8a/"
cp "$BUILD/ps1core/swanstation_libretro_android.so" "$A/lib/arm64-v8a/libswanstation.so"
# HP3's engine: the game, its SDL2, its process bridge and the C++ runtime it is built against (its own NDK's); Flipendo's
# libopenal.so serves it too (the engine binds libopenal.so by name)
if [ "$HP3_MODE" = rust ]; then
	cp "$HP3_RUST_SO" "$A/lib/arm64-v8a/libhp3app.so"
elif [ "$HP3_MODE" != off ]; then
	for l in libUT2004.so libSDL2.so libut2004_android_bridge.so libc++_shared.so; do
		cp "$HP3_LIBS/$l" "$A/lib/arm64-v8a/"
	done
	"$HP3_ENGINE/tools/android/check_sdl_java_package.sh" "$A/lib/arm64-v8a/libSDL2.so" org.libsdl2.app
fi
# keep the unstripped library: frames in a user's crash.txt are module+offset, symbolicate them against it
mkdir -p android/dist/symbols
cp "$BUILD/libmain.so" android/dist/symbols/libmain.unstripped.so
[ "$HP3_MODE" = build ] && cp "$HP3_BUILD/Source/SDLLaunch/libUT2004.so" android/dist/symbols/libUT2004.unstripped.so
[ "$HP3_MODE" = rust ] && cp "$HP3_RUST_SO" android/dist/symbols/libhp3app.unstripped.so
# The version (docs/android.md, "Versions"): android/VERSION is the release number; a build of its tag android-v<VERSION>
# carries it as is, any other build adds -dev+<commit>. versionCode is the commit count, so every newer build counts higher.
VERSION="$(tr -d ' \r\n' < android/VERSION)"
VERSION_CODE="$(git rev-list --count HEAD)"
if [ "$(git tag --points-at HEAD | grep -cx "android-v$VERSION")" = 1 ] && [ -z "$(git status --porcelain --untracked-files=no --ignore-submodules)" ]; then
	VERSION_NAME="$VERSION"
else
	VERSION_NAME="$VERSION-dev+$(git rev-parse --short HEAD)"
fi
echo "version $VERSION_NAME (code $VERSION_CODE)"
echo "$VERSION_NAME" > android/dist/symbols/build-info.txt
git describe --always --dirty >> android/dist/symbols/build-info.txt
date +%F >> android/dist/symbols/build-info.txt
paste -sd' ' android/dist/symbols/build-info.txt > "$A/assets/build-info.txt"
"$STRIP" --strip-unneeded "$A"/lib/arm64-v8a/*.so
jar --create --no-manifest --file "$A/assets/SurrealEngine.pk3" -C src/engine/Resources .
# the touch buttons' icons (images/android/*.png) are loaded from assets/icons/ by the overlay
mkdir -p "$A/assets/icons" && cp images/android/*.png "$A/assets/icons/"
# the Russian text translation (android/lang/ru-text, UTF-16 .rus files): in every APK, as the game's .int files in
# Windows-1251, whose Cyrillic the game draws with glyphs from Noto Sans (src/hp1/HP1Cyrillic.cpp)
# HP3's interface texts in the launcher's other languages (android/lang/hp3: HpMenu and SaveTips per language, UTF-8)
# PS1: PCSX-Redux's OpenBIOS (MIT, android/ps1/README.md), the BIOS the emulator uses unless the player picks another
mkdir -p "$A/assets/ps1" && cp android/ps1/openbios.bin "$A/assets/ps1/"
mkdir -p "$A/assets/hp3lang"
cp android/lang/hp3/* "$A/assets/hp3lang/"
mkdir -p "$A/assets/rutext"
for f in android/lang/ru-text/*.rus; do
	iconv -f UTF-16 -t CP1251 "$f" > "$A/assets/rutext/$(basename "$f" .rus).int"
done
RUST_JAVA=()
[ "$HP3_MODE" = rust ] && RUST_JAVA=(android/java/com/ut2004/android/*.java)
javac -Xlint:-options -source 8 -target 8 -cp "$AJ" -d "$A/classes" \
	android/deps/SDL/android-project/app/src/main/java/org/libsdl/app/*.java android/java/io/github/flipendo/spike/*.java "${RUST_JAVA[@]}"
if [ "$HP3_MODE" = libs ] || [ "$HP3_MODE" = build ]; then
# HP3's engine activity and touch controls, with SDL2's Java moved to org.libsdl2.app (the package its libSDL2.so is built
# for); the engine's Java is Java 17. None of the engine app's assets/system files: the launcher prepares HP3's own.
HP3JAVA="$A/hp3java"
EJ="$HP3_ENGINE/Android/app/src/main/java"
rm -rf "$HP3JAVA"
mkdir -p "$HP3JAVA/com/ut2004/android" "$HP3JAVA/org/libsdl2/app"
for f in GameActivity Hp3TouchOverlayView Storage; do
	sed 's/org\.libsdl\.app/org.libsdl2.app/g' "$EJ/com/ut2004/android/$f.java" > "$HP3JAVA/com/ut2004/android/$f.java"
done
for f in "$EJ"/org/libsdl/app/*.java; do
	sed 's/org\.libsdl\.app/org.libsdl2.app/g' "$f" > "$HP3JAVA/org/libsdl2/app/$(basename "$f")"
done
javac -Xlint:-options -source 17 -target 17 -cp "$AJ" -d "$A/classes" "$HP3JAVA"/com/ut2004/android/*.java "$HP3JAVA"/org/libsdl2/app/*.java
mkdir -p "$A/assets/touch_overlay/hp3"
cp "$HP3_ENGINE/Android/app/src/main/assets/touch_overlay/hp3/"*.png "$A/assets/touch_overlay/hp3/"
fi
# the class list goes in a file: with the touch overlay's inner classes the command line got longer than Windows allows
(cd "$A" && find classes -name "*.class" > classes.list && "$BT/d8.bat" --lib "$AJ" --output . @classes.list)
[ -f android/debug.keystore ] || keytool -genkeypair -keystore android/debug.keystore -storepass android -keypass android \
	-alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Flipendo Debug"

# package <output apk> [directory that holds gamedata/ for the self pack]
package() {
	local out="$1"; shift
	rm -f "$A/base.apk" "$A/unsigned.apk" "$A/aligned.apk"
	# aapt2 links the manifest and resources only: on Windows it writes assets with backslashes in their names
	# (assets/icons\wand.png), which Android can't find. jar writes proper paths, so the assets are added with it.
	# android/res: the launcher icon (mipmap-*/ic_launcher.png, scaled from images/branding/flipendo-icon.png)
	"$BT/aapt2.exe" compile --dir android/res -o "$A/res.zip"
	"$BT/aapt2.exe" link -o "$A/base.apk" --manifest android/AndroidManifest.xml -I "$AJ" -R "$A/res.zip" \
		--min-sdk-version 29 --target-sdk-version 35 --version-code "$VERSION_CODE" --version-name "$VERSION_NAME"
	local extra=()
	[ $# -gt 0 ] && extra=(-C "$1" assets)
	(cd "$A" && cp base.apk unsigned.apk && jar uf unsigned.apk classes.dex lib assets "${extra[@]/#$A\//}" && "$BT/zipalign.exe" -f -p 4 unsigned.apk aligned.apk)
	"$BT/apksigner.bat" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android --out "$out" "$A/aligned.apk"
	rm -f "$out.idsig"
	echo "built $out ($(du -h "$out" | cut -f1))"
}

# label of a mod in Settings
mod_label() {
	case "$1" in
		movement) echo "Movement mod (AdamJD)" ;;
		*) echo "$1" ;;
	esac
}

# script mods go into every APK (light and self pack): assets/mods/, installed into the HP1 folder in use (SetupActivity.applyMod);
# the movement mod is taken from movement.zip next to the repository's README when no --mod names one
if [ ${#MODS[@]} -eq 0 ] && [ -f movement.zip ]; then
	MODS=("movement=movement.zip")
fi
if [ ${#MODS[@]} -gt 0 ]; then
	mkdir -p "$A/assets/mods"
	: > "$A/assets/mods/mods.txt"
	for m in "${MODS[@]}"; do
		id="${m%%=*}"; zip="$(cygpath -u "${m#*=}")"
		# only plain .u packages (no paths), whatever else the zip holds is ignored
		mkdir -p "$A/assets/mods/$id"
		unzip -qo -j "$zip" '*.u' -d "$A/assets/mods/$id"
		echo "$id|$(mod_label "$id")" >> "$A/assets/mods/mods.txt"
		echo "mod $id: $(ls "$A/assets/mods/$id" | tr '\n' ' ')"
	done
fi

# 5. the plain APK: the game is copied to /sdcard/FlipendoHP/ by hand
package android/dist/flipendo-droid.apk

# 6. the self pack: the same APK plus the game, unpacked on first launch (SetupActivity)
# What the engine reads from an install: everything except SafeDisc files, the uninstaller, shortcuts, logs and saves
# (the save thumbnails stay: the slot page needs them). NUL-separated paths relative to the folder, "./" first.
game_files() {
	(cd "$1" && find . -type f \
		! -iname 'drvmgt.dll' ! -iname 'secdrv.sys' ! -iname 'uninst.*' ! -iname '*.lnk' ! -iname '*.log' \
		! -iname 'NEW.txt' ! -iname '*.usa' ! -iname 'GameSaveInfo*' ! -iname 'SE-*.ini' ! -iname 'User.ini' ! -iname 'flipendo*' \
		-print0)
}

# language label for the in-game switch (assets/langs/langs.txt: "code|label", the first one is the base install)
lang_label() {
	case "$1" in
		en) echo "English" ;;
		ru) echo "Русский (Фаргус)" ;;
		ru-softclub) echo "Русский (озвучка СофтКлаб)|rutext" ;; # "|rutext": the Russian text translation goes with it
		de) echo "Deutsch" ;;
		*) echo "$1" ;;
	esac
}

for entry in "${SELFPACKS[@]}"; do
	SELFPACK="${entry%|*}"
	NAME="${entry#*|}"
	STAGE="$A/data_stage/assets/gamedata"
	LANGDIR="$A/data_stage/assets/langs"
	rm -rf "$A/data_stage"
	mkdir -p "$STAGE"
	if [ "$NAME" = flipendo-droid-selfpack.apk ] && { [ ${#LANGS[@]} -gt 0 ] || [ ${#VOICES[@]} -gt 0 ] || [ ${#ADDLANGS[@]} -gt 0 ]; }; then
		# Several languages (the base install is English): the files all installs share are shipped once, the files that differ
		# (voices, fonts, texts) once per language; the app copies the chosen language's set over (SetupActivity, in-game switch).
		declare -A base_files=() lang_dir=()
		while IFS= read -r -d '' f; do base_files["${f,,}"]="$f"; done < <(game_files "$SELFPACK")
		differ=() # lowercase keys of the files that differ in any language
		for l in "${LANGS[@]}"; do
			code="${l%%=*}"; dir="${l#*=}"
			lang_dir["$code"]="$dir"
			declare -A seen=()
			while IFS= read -r -d '' f; do
				key="${f,,}"; seen["$key"]=1
				if [ -z "${base_files[$key]:-}" ] || ! cmp -s "$SELFPACK/${base_files[$key]}" "$dir/$f"; then differ+=("$key"); fi
			done < <(game_files "$dir")
			for key in "${!base_files[@]}"; do [ -z "${seen[$key]:-}" ] && differ+=("$key"); done
			unset seen
		done
		# a dubbing: only its Sounds files count, every other file is the base install's
		for v in "${VOICES[@]}"; do
			dir="${v#*=}"
			while IFS= read -r -d '' f; do
				key="./sounds/${f#./}"; key="${key,,}"
				if [ -z "${base_files[$key]:-}" ] || ! cmp -s "$SELFPACK/${base_files[$key]}" "$dir/$f"; then differ+=("$key"); fi
			done < <(cd "$dir" && find . -type f -print0)
		done
		# a language add-on: only its language files, and the ini that names the language (the base's version goes into en)
		for l in "${ADDLANGS[@]}"; do
			dir="${l#*=}"
			while IFS= read -r -d '' f; do differ+=("${f,,}"); done < <(addlang_files "$dir" "$(addlang_ext "$dir")")
			differ+=("./system/default.ini")
		done
		declare -A is_diff=()
		for key in "${differ[@]}"; do is_diff["$key"]=1; done
		common=(); for key in "${!base_files[@]}"; do [ -z "${is_diff[$key]:-}" ] && common+=("${base_files[$key]}"); done
		printf '%s\0' "${common[@]}" | (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$STAGE" && tar xf -)
		mkdir -p "$LANGDIR"
		{ echo "en|$(lang_label en)"; for l in "${LANGS[@]}" "${ADDLANGS[@]}" "${VOICES[@]}"; do echo "${l%%=*}|$(lang_label "${l%%=*}")"; done; } > "$LANGDIR/langs.txt"
		# the base language's version of the differing files (the ones the base install has)
		mkdir -p "$LANGDIR/en"
		for key in "${!is_diff[@]}"; do if [ -n "${base_files[$key]:-}" ]; then printf '%s\0' "${base_files[$key]}"; fi; done \
			| (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$LANGDIR/en" && tar xf -)
		for l in "${LANGS[@]}"; do
			code="${l%%=*}"; dir="${l#*=}"
			mkdir -p "$LANGDIR/$code"
			game_files "$dir" | while IFS= read -r -d '' f; do if [ -n "${is_diff[${f,,}]:-}" ]; then printf '%s\0' "$f"; fi; done \
				| (cd "$dir" && tar --null -cf - --files-from=-) | (cd "$LANGDIR/$code" && tar xf -)
		done
		# a language add-on: its language files, and the base's Default.ini with that language (the files of the other builds
		# stay the base's: the scripts, maps and exe of another release are not ours to swap in)
		for l in "${ADDLANGS[@]}"; do
			code="${l%%=*}"; dir="${l#*=}"; ext="$(addlang_ext "$dir")"
			mkdir -p "$LANGDIR/$code"
			addlang_files "$dir" "$ext" | (cd "$dir" && tar --null -cf - --files-from=-) | (cd "$LANGDIR/$code" && tar xf -)
			base_ini="${base_files[./system/default.ini]:?the base install has no System/Default.ini}"
			mkdir -p "$LANGDIR/$code/$(dirname "$base_ini")"
			# only the value changes: the file stays byte for byte the base's (its CRLF line ends included)
			LANG_EXT="$ext" perl -pe 's/^Language=\K[^\r\n]*/$ENV{LANG_EXT}/' "$SELFPACK/$base_ini" > "$LANGDIR/$code/$base_ini"
		done
		# a dubbing's edition: its own Sounds files, and the base install's version of every other differing file (so a switch
		# from another edition puts the base's texts and fonts back)
		for v in "${VOICES[@]}"; do
			code="${v%%=*}"; dir="${v#*=}"
			mkdir -p "$LANGDIR/$code"
			declare -A voice_files=()
			while IFS= read -r -d '' f; do key="./sounds/${f#./}"; voice_files["${key,,}"]="$f"; done < <(cd "$dir" && find . -type f -print0)
			for key in "${!is_diff[@]}"; do
				if [ -n "${voice_files[$key]:-}" ]; then
					target="${base_files[$key]:-./Sounds/${voice_files[$key]#./}}"
					mkdir -p "$LANGDIR/$code/$(dirname "$target")"
					cp "$dir/${voice_files[$key]}" "$LANGDIR/$code/$target"
				elif [ -n "${base_files[$key]:-}" ]; then
					mkdir -p "$LANGDIR/$code/$(dirname "${base_files[$key]}")"
					cp "$SELFPACK/${base_files[$key]}" "$LANGDIR/$code/${base_files[$key]}"
				fi
			done
			unset voice_files
		done
		echo "languages: $(paste -sd, "$LANGDIR/langs.txt"); ${#is_diff[@]} files differ, $(du -sm "$LANGDIR" | cut -f1) MB in langs/"
		unset base_files lang_dir is_diff
	else
		game_files "$SELFPACK" | (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$STAGE" && tar xf -)
	fi
	# texture packs (the texture-overrides mod draws Textures/<package>/<group>/<name>.dds): assets/texpacks/, which the launcher
	# copies into whichever game folder is in use (SetupActivity.applyTexturePacks): common/ always, the menu art of the game's language
	if [ "$NAME" = flipendo-droid-selfpack.apk ] && { [ ${#TEXTURES[@]} -gt 0 ] || [ -n "$MENUART" ]; }; then
		TEXPACKS="$A/data_stage/assets/texpacks"
		mkdir -p "$TEXPACKS"
		if [ ${#TEXTURES[@]} -gt 0 ]; then
			mkdir -p "$TEXPACKS/common/Textures"
			for t in "${TEXTURES[@]}"; do
				cp -r "$t/Textures/." "$TEXPACKS/common/Textures/"
			done
			echo "texture packs: $(find "$TEXPACKS/common" -type f | wc -l) files, $(du -sm "$TEXPACKS/common" | cut -f1) MB"
		fi
		if [ -n "$MENUART" ]; then
			# the pack names the base language after the game; Russian is the one with the Cyrillic logos
			mkdir -p "$TEXPACKS/menuart/en" "$TEXPACKS/menuart/ru"
			cp "$MENUART/Harry Potter and the Philosopher's Stone/Textures/MenuArt"/*.dds "$TEXPACKS/menuart/en/"
			cp "$MENUART/Russian/Textures/MenuArt"/*.dds "$TEXPACKS/menuart/ru/"
			echo "menu art: en, ru"
		fi
	fi
	# Harry Potter 2 in the same self pack: assets/hp2data, unpacked into FlipendoHP2 (SetupActivity.installHp2Data), once
	if [ -n "$HP2DIR" ] && [ "$NAME" = flipendo-droid-selfpack.apk ]; then
		HP2STAGE="$A/data_stage/assets/hp2data"
		mkdir -p "$HP2STAGE"
		# what the engine reads from an install: no RTX/ReShade/DLSS add-ons, SafeDisc files, backups, logs, dumps or saves
		(cd "$HP2DIR" && find . -type f \
			! -iname 'd3d11drv*' ! -iname 'dxgi.dll' ! -iname 'reshade*' ! -iname 'dlss5-*' ! -ipath './host64/*' ! -iname '*.dmp' \
			! -iname '*.bak' ! -iname 'vssver.scc' ! -iname 'mssccprj.scc' ! -iname 'drvmgt.dll' ! -iname 'secdrv.sys' ! -iname '*.lnk' \
			! -iname '*.log' ! -ipath './save/*' ! -ipath './harry2/*' ! -iname 'SE-*.ini' ! -iname 'User.ini' ! \( -iname 'flipendo*' ! -iname '*.utx' \) -print0) \
			| (cd "$HP2DIR" && tar --null -cf - --files-from=-) | (cd "$HP2STAGE" && tar xf -)
		if [ -n "$HP2TEXTS" ]; then
			mkdir -p "$HP2STAGE/harry2"
			cp "$HP2TEXTS"/*.rus "$HP2STAGE/harry2/"
		fi
		# HP2 editions (voices and texts of a release): the files that make it one, in editions/<code>/ (SetupActivity.applyHp2Edition)
		for edition in "${HP2EDITIONS[@]}"; do
			code="${edition%%=*}"
			rest="${edition#*=}"
			dir="$(cygpath -u "${rest%%|*}")"
			name="${rest#*|}"
			[ "$name" = "$rest" ] && name=""
			target="$HP2STAGE/editions/$code"
			mkdir -p "$target/Sounds" "$target/System" "$target/Textures"
			cp "$dir"/Sounds/*uax "$target/Sounds/" 2>/dev/null || true
			for f in "$dir"/System/*.rus; do
				[ -f "$f" ] && [ "$(basename "$f" | tr A-Z a-z)" != "sapfont.rus" ] && cp "$f" "$target/System/"
			done
			cp "$dir"/Textures/MenuArt.Rus_utx "$target/Textures/" 2>/dev/null || true
			printf '%s\n' "${name:-$code}" > "$target/edition.txt"
			echo "HP2 edition $code: $(find "$target" -type f | wc -l) files, $(du -sm "$target" | cut -f1) MB"
		done
		(cd "$HP2STAGE" && find . -type f ! -name .stamp -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -d' ' -f1) > "$HP2STAGE/.stamp"
		echo "HP2 data: $(find "$HP2STAGE" -type f | wc -l) files, $(du -sm "$HP2STAGE" | cut -f1) MB"
	fi
	# Harry Potter 3 in the same self pack: assets/hp3data, unpacked into FlipendoHP3 (SetupActivity.installBundle), once.
	# The game's folders only: no installer files, help, Support, Windows binaries, logs or saves. Movies is in: the Rust engine
	# plays its Bink videos as textures (the animals' fur in the Transfiguration class and the Shack, the newspaper's photograph).
	if [ -n "$HP3DIR" ] && [ "$NAME" = flipendo-droid-selfpack.apk ]; then
		HP3STAGE="$A/data_stage/assets/hp3data"
		mkdir -p "$HP3STAGE"
		(cd "$HP3DIR" && find . -type f \( -ipath './animations/*' -o -ipath './maps/*' -o -ipath './music/*' -o -ipath './sounds/*' \
			-o -ipath './staticmeshes/*' -o -ipath './textures/*' -o -ipath './system/*' -o -ipath './movies/*' \) \
			! -iname '*.exe' ! -iname '*.dll' ! -iname '*.log' ! -iname '*.usa' -print0) \
			| (cd "$HP3DIR" && tar --null -cf - --files-from=-) | (cd "$HP3STAGE" && tar xf -)
		# A second edition (--hp3-eng): both languages live in the one folder and HP3's engine picks by the language the launcher wrote
		# (.flipendo-language; none = the base, Default.ini). Files only the English edition has (alldialog.uax, the .int texts) go in
		# as they are; a texture package both have under one name but with different art (HP3_Menu, HP3_Gameplay: the title logo)
		# keeps the base's as <name>.rus_<ext> (the engine prefers it when Russian is chosen) and takes the English as plain.
		if [ -n "$HP3ENG" ]; then
			declare -A stage_files=()
			while IFS= read -r -d '' f; do stage_files["${f,,}"]="$f"; done < <(cd "$HP3STAGE" && find . -type f -print0)
			en_new=0; en_art=0
			while IFS= read -r -d '' f; do
				have="${stage_files[${f,,}]:-}"
				if [ -z "$have" ]; then
					mkdir -p "$HP3STAGE/$(dirname "$f")"
					cp "$HP3ENG/$f" "$HP3STAGE/$f"
					en_new=$((en_new + 1))
				else
					case "${f,,}" in
						./textures/*)
							if [ "$(stat -c %s "$HP3ENG/$f")" != "$(stat -c %s "$HP3STAGE/$have")" ] || ! cmp -s "$HP3ENG/$f" "$HP3STAGE/$have"; then
								mv "$HP3STAGE/$have" "$HP3STAGE/${have%.*}.rus_${have##*.}"
								cp "$HP3ENG/$f" "$HP3STAGE/$have"
								en_art=$((en_art + 1))
							fi ;;
					esac
				fi
			done < <(cd "$HP3ENG" && find . -type f \( -ipath './animations/*' -o -ipath './maps/*' -o -ipath './music/*' -o -ipath './sounds/*' \
				-o -ipath './staticmeshes/*' -o -ipath './textures/*' -o -ipath './system/*' -o -ipath './movies/*' \) \
				! -iname '*.exe' ! -iname '*.dll' ! -iname '*.log' ! -iname '*.usa' -print0)
			echo "HP3 English edition: $en_new files added, $en_art localised texture packages split"
			[ "$en_new" -gt 0 ] || { echo "--hp3-eng $HP3ENG added nothing: is it an English install?" >&2; exit 1; }
		fi
		(cd "$HP3STAGE" && find . -type f ! -name .stamp -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -d' ' -f1) > "$HP3STAGE/.stamp"
		echo "HP3 data: $(find "$HP3STAGE" -type f | wc -l) files, $(du -sm "$HP3STAGE" | cut -f1) MB"
		# The same data as a cache zip for the light APK (like the HP1/HP2 ones): a FlipendoHP3 folder, with .nomedia, no stamp
		if [ -n "$HP3CACHEZIP" ]; then
			CZ="$A/hp3_zip_stage"
			rm -rf "$CZ"; mkdir -p "$CZ/FlipendoHP3"
			cp -a "$HP3STAGE/." "$CZ/FlipendoHP3/"
			rm -f "$CZ/FlipendoHP3/.stamp"
			: > "$CZ/FlipendoHP3/.nomedia"
			rm -f "$HP3CACHEZIP"
			(cd "$CZ" && 7z a -tzip -mx=1 "$(cygpath -w "$(cd "$ROOT" && realpath -m "$HP3CACHEZIP")")" FlipendoHP3 > /dev/null)
			rm -rf "$CZ"
			echo "HP3 cache zip: $HP3CACHEZIP ($(du -sm "$HP3CACHEZIP" | cut -f1) MB)"
		fi
	fi
	# a stamp of what is inside (a hash of the contents: two installs can differ with the same sizes): the app only unpacks again when the data changes
	# PS1 discs in the same self pack: assets/ps1data/hp1 and hp2 (one image each, a CHD by choice), unpacked into
	# FlipendoPS1/HP1 and HP2 the first time the PS1 tab's Play is pressed (SetupActivity.startPs1, installBundle), once per stamp
	if [ "$NAME" = flipendo-droid-selfpack.apk ]; then
		for ps1 in "hp1|$PS1_HP1" "hp2|$PS1_HP2"; do
			g="${ps1%%|*}"; img="${ps1#*|}"
			[ -n "$img" ] || continue
			PS1STAGE="$A/data_stage/assets/ps1data/$g"
			mkdir -p "$PS1STAGE"
			cp "$img" "$PS1STAGE/$(echo "$g" | tr a-z A-Z).${img##*.}"
			(cd "$PS1STAGE" && find . -type f ! -name .stamp -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -d' ' -f1) > "$PS1STAGE/.stamp"
			echo "PS1 $g: $(du -sm "$PS1STAGE" | cut -f1) MB"
		done
	fi
	# (the PS1 discs have their own stamps and stay out of the PC game's)
	(cd "$A/data_stage/assets" && find . -type f ! -name .stamp ! -path './ps1data/*' -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -d' ' -f1) > "$STAGE/.stamp"
	echo "game data: $(find "$A/data_stage" -type f | wc -l) files, $(du -sm "$A/data_stage" | cut -f1) MB"
	package "android/dist/$NAME" "$A/data_stage"
	rm -rf "$A/data_stage"
done

if [ "$INSTALL" = 1 ]; then
	"$ADB" install -r android/dist/flipendo-droid.apk
	"$ADB" shell appops set io.github.flipendo.spike MANAGE_EXTERNAL_STORAGE allow || true
fi
