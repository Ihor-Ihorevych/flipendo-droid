#!/usr/bin/env bash
# Desktop tests of the launcher's plain-Java helpers (no Android, no phone): compiled with the desktop JDK at the
# Android source level and run with java. Exit code: 0 when every test passes.
#   android/tests/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export JAVA_HOME="$(cygpath -u "${JAVA_HOME:-C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot}" 2>/dev/null || echo "$JAVA_HOME")"
export PATH="$JAVA_HOME/bin:$PATH"
OUT=build-android-tests
rm -rf "$OUT"
mkdir -p "$OUT"
javac -Xlint:-options -source 8 -target 8 -d "$OUT" android/java/io/github/flipendo/spike/Hp3Setup.java android/tests/Hp3SetupTest.java
java -cp "$OUT" io.github.flipendo.spike.Hp3SetupTest
