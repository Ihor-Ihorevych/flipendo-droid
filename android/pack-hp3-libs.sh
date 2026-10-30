#!/usr/bin/env bash
# Packs the HP3 engine libraries that build-apk.sh built (android/dist/hp3-libs) into android/dist/hp3-libs.zip, with the
# engine commit they were built from, for the release "hp3-libs" that the GitHub workflow builds the APK from (a runner has
# no game files, so it cannot generate the headers HP3's engine is built against):
#   android/build-apk.sh                          (builds HP3's engine from source)
#   android/pack-hp3-libs.sh
#   gh release create hp3-libs android/dist/hp3-libs.zip --prerelease   (or: gh release upload hp3-libs ... --clobber)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/android/dist/hp3-libs"
git -C "$ROOT/src/hp3-engine" rev-parse HEAD > COMMIT.txt
rm -f ../hp3-libs.zip
powershell -NoProfile -Command "Compress-Archive -Path libUT2004.so,libSDL2.so,libut2004_android_bridge.so,libc++_shared.so,COMMIT.txt -DestinationPath ../hp3-libs.zip"
echo "android/dist/hp3-libs.zip (engine $(cat COMMIT.txt))"
