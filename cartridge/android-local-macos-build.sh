#!/usr/bin/env bash
#
# android-local-macos-build.sh
#
# Local macOS build helper for the Android frontend (pkg/android/phoenix),
# mirroring .github/workflows/Android.yml but tuned for fast local iteration:
#
#   - Selects a JDK the pinned Gradle 6.7.1 / AGP 4.2.0 can run on. Java 17
#     fails with "Unsupported class file major version 61" — Gradle needs 11.
#   - Verifies (and installs if missing) the exact SDK build-tools/NDK/platform
#     versions pinned in pkg/android/phoenix/build.gradle.
#   - By default builds only the aarch64 flavor restricted to a single ABI
#     (arm64-v8a, i.e. a real ARM64 handheld) instead of all 5 flavors x every
#     ABI CI produces, so rebuilds are minutes instead of ~7.
#
# Usage:
#   cartridge/android-local-macos-build.sh                # fast: aarch64 debug, arm64-v8a only
#   cartridge/android-local-macos-build.sh --full          # everything CI builds
#   cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
#   cartridge/android-local-macos-build.sh --clean         # ./gradlew clean first
#   cartridge/android-local-macos-build.sh --install       # adb install -r the built APK after
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

PHOENIX_DIR="pkg/android/phoenix"
REQUIRED_BUILD_TOOLS="30.0.3"
REQUIRED_NDK="22.0.7026061"
REQUIRED_PLATFORM="android-31"

full_build=0
do_clean=0
do_install=0
abi="arm64-v8a"

while [ $# -gt 0 ]; do
  case "$1" in
    --full) full_build=1 ;;
    --clean) do_clean=1 ;;
    --install) do_install=1 ;;
    --abi) abi="$2"; shift ;;
    -h|--help)
      cat <<'EOF'
Usage:
  cartridge/android-local-macos-build.sh                # fast: aarch64 debug, arm64-v8a only
  cartridge/android-local-macos-build.sh --full          # everything CI builds
  cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
  cartridge/android-local-macos-build.sh --clean         # ./gradlew clean first
  cartridge/android-local-macos-build.sh --install       # adb install -r the built APK after
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: $1 (use --help)" >&2
      exit 1
      ;;
  esac
  shift
done

# --- JDK 11 -------------------------------------------------------------
if ! java11_home="$(/usr/libexec/java_home -v 11 2>/dev/null)"; then
  echo "error: no Java 11 JDK found. Gradle 6.7.1 / AGP 4.2.0 (pinned for this" >&2
  echo "project) cannot run on newer JDKs. Install one, e.g.:" >&2
  echo "  brew install --cask temurin@11" >&2
  exit 1
fi
export JAVA_HOME="$java11_home"
echo "Using JAVA_HOME=$JAVA_HOME"

# --- Android SDK ----------------------------------------------------------
ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export ANDROID_HOME
if [ ! -d "$ANDROID_HOME" ]; then
  echo "error: Android SDK not found at $ANDROID_HOME (set ANDROID_HOME)." >&2
  exit 1
fi

sdkmanager="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$sdkmanager" ]; then
  echo "error: sdkmanager not found at $sdkmanager." >&2
  echo "Install the 'Android SDK Command-line Tools' package via Android Studio's SDK Manager." >&2
  exit 1
fi

missing=()
[ -d "$ANDROID_HOME/build-tools/$REQUIRED_BUILD_TOOLS" ] || missing+=("build-tools;$REQUIRED_BUILD_TOOLS")
[ -d "$ANDROID_HOME/ndk/$REQUIRED_NDK" ] || missing+=("ndk;$REQUIRED_NDK")
[ -d "$ANDROID_HOME/platforms/$REQUIRED_PLATFORM" ] || missing+=("platforms;$REQUIRED_PLATFORM")

if [ "${#missing[@]}" -gt 0 ]; then
  echo "Installing missing SDK components: ${missing[*]}"
  yes | "$sdkmanager" "${missing[@]}" >/dev/null
fi

# --- Build ------------------------------------------------------------------
cd "$PHOENIX_DIR"

gradle_args=()
[ "$do_clean" -eq 1 ] && gradle_args+=("clean")

if [ "$full_build" -eq 1 ]; then
  echo "Building all flavors/ABIs (matches CI: assembleDebug)..."
  gradle_args+=("assembleDebug")
else
  echo "Building aarch64 debug, ABI=$abi only (fast path for a handheld device)..."
  gradle_args+=("assembleAarch64Debug" "-Pandroid.injected.build.abi=$abi")
fi

build_marker="$(mktemp)"
./gradlew "${gradle_args[@]}"

echo
echo "APKs built:"
find . -iname "*.apk" -newer "$build_marker" -exec ls -lh "{}" \;
rm -f "$build_marker"

if [ "$do_install" -eq 1 ]; then
  apk="$(find . -iname "phoenix-aarch64-debug.apk" | head -n1)"
  if [ -z "$apk" ]; then
    echo "error: could not find phoenix-aarch64-debug.apk to install" >&2
    exit 1
  fi
  echo "Installing $apk to connected device..."
  adb install -r "$apk"
fi
