#!/usr/bin/env bash
# Builds the C++ compiler the phone carries (clang + lld, AArch64 only, for aarch64-linux-android) and the small sysroot it needs, so
# the PS2 games can be translated and compiled ON the phone from the player's own disc (docs/ps2.md). Run once; the results go to
# android/deps/ps2cc/out and are packed into the APK by build-apk.sh (lib/arm64-v8a/libps2clang.so, libps2lld.so, assets/ps2/sysroot.zip).
#
# Needs: Git Bash on Windows, the Android NDK (r28), CMake, Ninja, llvm-mingw (for the two host tools LLVM builds for itself), 7z or zip.
#   . /c/Users/Ihor/CFR2Recomp/env.sh     (puts llvm-mingw, CMake and Ninja on the PATH)
#   bash android/ps2/build-toolchain.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
LLVM_VER=20.1.8
DEPS="$ROOT/android/deps/ps2cc"
SRC="$DEPS/llvm-project-$LLVM_VER.src"
OUT="$DEPS/out"
SDK="${ANDROID_SDK_ROOT:-${LOCALAPPDATA:+$(cygpath "$LOCALAPPDATA")/Android/Sdk}}"
NDK="${ANDROID_NDK:-$(ls -d "$SDK"/ndk/28.* | sort | tail -n1)}"
JOBS="${PS2CC_JOBS:-$(nproc)}"
mkdir -p "$DEPS" "$OUT"

# 1. the source, once
if [ ! -d "$SRC/llvm" ]; then
	[ -f "$DEPS/llvm-project-$LLVM_VER.src.tar.xz" ] || curl -L -o "$DEPS/llvm-project-$LLVM_VER.src.tar.xz" \
		"https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/llvm-project-$LLVM_VER.src.tar.xz"
	# Windows cannot make the symlinks of LLVM's test folders: tar says so and ends with an error, the rest is extracted all the same
	tar -C "$DEPS" -xf "$DEPS/llvm-project-$LLVM_VER.src.tar.xz" 2> "$DEPS/tar.log" || true
	[ -d "$SRC/llvm" ] && [ -f "$SRC/clang/CMakeLists.txt" ] && [ -f "$SRC/lld/CMakeLists.txt" ] || { echo "the LLVM source did not unpack"; exit 1; }
fi

# 2. the host tools LLVM runs while it builds (table generators), with the PC's compiler
HOST="$DEPS/build-host"
if [ ! -x "$HOST/bin/clang-tblgen.exe" ] && [ ! -x "$HOST/bin/clang-tblgen" ]; then
	cmake -S "$SRC/llvm" -B "$HOST" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang \
		-DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
		-DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
	ninja -C "$HOST" -j"$JOBS" llvm-tblgen llvm-min-tblgen clang-tblgen
fi

# 3. clang and lld for the phone: small (size-optimised, one target, nothing but the compiler and the linker)
BUILD="$DEPS/build-android"
cmake -S "$SRC/llvm" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
	-DLLVM_NATIVE_TOOL_DIR="$HOST/bin" -DLLVM_HOST_TRIPLE=aarch64-linux-android29 -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android29 \
	-DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
	-DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF \
	-DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_PIC=ON \
	-DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_OBJC_REWRITER=OFF \
	-DCLANG_DEFAULT_CXX_STDLIB=libc++ -DCLANG_DEFAULT_LINKER=lld -DCLANG_DEFAULT_RTLIB=compiler-rt -DCLANG_DEFAULT_UNWINDLIB=libgcc \
	-DLLVM_PARALLEL_LINK_JOBS=2 -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++"
ninja -C "$BUILD" -j"$JOBS" clang lld

# 4. the files that go into the APK
STRIP="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
cp "$BUILD/bin/clang" "$OUT/libps2clang.so"
cp "$BUILD/bin/lld" "$OUT/libps2lld.so"
"$STRIP" --strip-all "$OUT/libps2clang.so" "$OUT/libps2lld.so"
ls -la "$OUT"

# 5. the sysroot: the NDK's headers and the stub libraries for API 29, libc++'s headers, and clang's own headers (stddef.h, arm_neon.h ...)
SYS="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
STAGE="$DEPS/sysroot-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/lib/aarch64-linux-android/29" "$STAGE/usr/include"
cp -r "$SYS/usr/include/." "$STAGE/usr/include/"
rm -rf "$STAGE/usr/include/arm-linux-androideabi" "$STAGE/usr/include/i686-linux-android" "$STAGE/usr/include/x86_64-linux-android" \
	"$STAGE/usr/include/riscv64-linux-android"
cp "$SYS/usr/lib/aarch64-linux-android/29/"*.so "$SYS/usr/lib/aarch64-linux-android/29/"*.o "$STAGE/usr/lib/aarch64-linux-android/29/"
cp "$SYS/usr/lib/aarch64-linux-android/"*.a "$SYS/usr/lib/aarch64-linux-android/"*.so "$STAGE/usr/lib/aarch64-linux-android/" 2>/dev/null || true
# clang's own headers (stddef.h, arm_neon.h ...) must be those of the clang that compiles: the NDK's are another version and arm_neon.h
# does not compile with ours. They are built from the LLVM tree (ninja clang-resource-headers). The compiler runtime library is the NDK's.
ninja -C "$BUILD" clang-resource-headers
RES20="$(ls -d "$BUILD"/lib/clang/* | sort | tail -n1)"
mkdir -p "$STAGE/clang-resource/include"
cp -r "$RES20/include/." "$STAGE/clang-resource/include/"
RES="$(ls -d "$NDK"/toolchains/llvm/prebuilt/windows-x86_64/lib/clang/* | sort | tail -n1)"
mkdir -p "$STAGE/clang-resource/lib/linux"
cp "$RES/lib/linux/libclang_rt.builtins-aarch64-android.a" "$STAGE/clang-resource/lib/linux/" 2>/dev/null || true
rm -f "$OUT/sysroot.zip"
( cd "$STAGE" && 7z a -tzip -mx=9 "$OUT/sysroot.zip" . > /dev/null )
ls -la "$OUT"
