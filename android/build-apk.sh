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
#                                                 the self pack also carries a mod (replacement .u packages) that Settings can switch
#   android/build-apk.sh --install                ... and install the plain APK on the connected phone (adb)
# The self pack contains copyrighted game data: for your own phone only, never publish or share it.
# Needs: JDK 21, Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1), git.
# Override the locations with JAVA_HOME and ANDROID_HOME. See docs/android.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INSTALL=0
SELFPACKS=() # "game folder|apk name"
LANGS=()     # "code=game folder": more languages for --selfpack (the in-game language switch)
MODS=()      # "id=zip": script mods the app can switch on and off (Settings > Movement mod)
while [ $# -gt 0 ]; do
	case "$1" in
		--install) INSTALL=1 ;;
		--selfpack) shift; SELFPACKS+=("$(cygpath -u "${1:?--selfpack needs the game folder}")|flipendo-droid-selfpack.apk") ;;
		--selfpack-ru) shift; SELFPACKS+=("$(cygpath -u "${1:?--selfpack-ru needs the game folder}")|flipendo-droid-selfpack-ru.apk") ;;
		--lang) shift; LANGS+=("$(echo "${1:?--lang needs code=game folder (e.g. ru=D:/HP-Russian)}" | sed 's|=.*||')=$(cygpath -u "$(echo "$1" | sed 's|^[^=]*=||')")") ;;
		--mod) shift; MODS+=("${1:?--mod needs id=mod.zip (e.g. movement=movement.zip)}") ;;
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

for l in "${LANGS[@]}"; do
	dir="${l#*=}"
	if [ ! -f "$dir/System/HP.exe" ] && [ ! -f "$dir/system/HP.exe" ]; then
		echo "$dir is not an HP1 install (no System/HP.exe)" >&2
		exit 1
	fi
done

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
# the touch buttons' icons (images/android/*.png) are loaded from assets/icons/ by the overlay
mkdir -p "$A/assets/icons" && cp images/android/*.png "$A/assets/icons/"
javac -Xlint:-options -source 8 -target 8 -cp "$AJ" -d "$A/classes" \
	android/deps/SDL/android-project/app/src/main/java/org/libsdl/app/*.java android/java/io/github/flipendo/spike/*.java
"$BT/d8.bat" --lib "$AJ" --output "$A" $(find "$A/classes" -name "*.class")
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
		--min-sdk-version 29 --target-sdk-version 35
	local extra=()
	[ $# -gt 0 ] && extra=(-C "$1" assets)
	(cd "$A" && cp base.apk unsigned.apk && jar uf unsigned.apk classes.dex lib assets "${extra[@]/#$A\//}" && "$BT/zipalign.exe" -f -p 4 unsigned.apk aligned.apk)
	"$BT/apksigner.bat" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android --out "$out" "$A/aligned.apk"
	rm -f "$out.idsig"
	echo "built $out ($(du -h "$out" | cut -f1))"
}

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

# label of a mod in Settings
mod_label() {
	case "$1" in
		movement) echo "Movement mod (AdamJD)" ;;
		*) echo "$1" ;;
	esac
}

# language label for the in-game switch (assets/langs/langs.txt: "code|label", the first one is the base install)
lang_label() {
	case "$1" in
		en) echo "English" ;;
		ru) echo "Русский (Фаргус)" ;;
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
	if [ "$NAME" = flipendo-droid-selfpack.apk ] && [ ${#LANGS[@]} -gt 0 ]; then
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
		declare -A is_diff=()
		for key in "${differ[@]}"; do is_diff["$key"]=1; done
		common=(); for key in "${!base_files[@]}"; do [ -z "${is_diff[$key]:-}" ] && common+=("${base_files[$key]}"); done
		printf '%s\0' "${common[@]}" | (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$STAGE" && tar xf -)
		mkdir -p "$LANGDIR"
		{ echo "en|$(lang_label en)"; for l in "${LANGS[@]}"; do echo "${l%%=*}|$(lang_label "${l%%=*}")"; done; } > "$LANGDIR/langs.txt"
		# the base language's version of the differing files (the ones the base install has)
		mkdir -p "$LANGDIR/en"
		for key in "${!is_diff[@]}"; do [ -n "${base_files[$key]:-}" ] && printf '%s\0' "${base_files[$key]}"; done \
			| (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$LANGDIR/en" && tar xf -)
		for l in "${LANGS[@]}"; do
			code="${l%%=*}"; dir="${l#*=}"
			mkdir -p "$LANGDIR/$code"
			game_files "$dir" | while IFS= read -r -d '' f; do [ -n "${is_diff[${f,,}]:-}" ] && printf '%s\0' "$f"; done \
				| (cd "$dir" && tar --null -cf - --files-from=-) | (cd "$LANGDIR/$code" && tar xf -)
		done
		echo "languages: $(paste -sd, "$LANGDIR/langs.txt"); ${#is_diff[@]} files differ, $(du -sm "$LANGDIR" | cut -f1) MB in langs/"
		unset base_files lang_dir is_diff
	else
		game_files "$SELFPACK" | (cd "$SELFPACK" && tar --null -cf - --files-from=-) | (cd "$STAGE" && tar xf -)
	fi
	if [ ${#MODS[@]} -gt 0 ] && [ "$NAME" = flipendo-droid-selfpack.apk ]; then
		mkdir -p "$A/data_stage/assets/mods"
		: > "$A/data_stage/assets/mods/mods.txt"
		for m in "${MODS[@]}"; do
			id="${m%%=*}"; zip="$(cygpath -u "${m#*=}")"
			# only plain .u packages (no paths), whatever else the zip holds is ignored
			mkdir -p "$A/data_stage/assets/mods/$id"
			unzip -qo -j "$zip" '*.u' -d "$A/data_stage/assets/mods/$id"
			echo "$id|$(mod_label "$id")" >> "$A/data_stage/assets/mods/mods.txt"
			echo "mod $id: $(ls "$A/data_stage/assets/mods/$id" | tr '
' ' ')"
		done
	fi
	# a stamp of what is inside (a hash of the contents: two installs can differ with the same sizes): the app only unpacks again when the data changes
	(cd "$A/data_stage/assets" && find . -type f ! -name .stamp -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -d' ' -f1) > "$STAGE/.stamp"
	echo "game data: $(find "$A/data_stage" -type f | wc -l) files, $(du -sm "$A/data_stage" | cut -f1) MB"
	package "android/dist/$NAME" "$A/data_stage"
	rm -rf "$A/data_stage"
done

if [ "$INSTALL" = 1 ]; then
	"$ADB" install -r android/dist/flipendo-droid.apk
	"$ADB" shell appops set io.github.flipendo.spike MANAGE_EXTERNAL_STORAGE allow || true
fi
