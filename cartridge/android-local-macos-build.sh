#!/usr/bin/env bash
#
# android-local-macos-build.sh
#
# Local macOS build helper for the Android frontend, without needing Docker
# or Android Studio.
#
# Default target is the Cartridge Expo/RN app (cartridge/app) -- the M1+
# React Native host. Pass --legacy to instead build the old M0
# NativeActivity frontend (pkg/android/phoenix), e.g. for A/B-testing against
# CartridgeSpikeActivity.
#
# Usage:
#   cartridge/android-local-macos-build.sh                # fast: Cartridge app, arm64-v8a only
#   cartridge/android-local-macos-build.sh --install       # ...then adb install -r + launch on a connected device
#   cartridge/android-local-macos-build.sh --clean         # force a from-scratch native rebuild (expo prebuild --clean / gradlew clean)
#   cartridge/android-local-macos-build.sh --full          # all ABIs instead of just arm64-v8a
#   cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
#   cartridge/android-local-macos-build.sh --legacy         # build pkg/android/phoenix (M0) instead
#   cartridge/android-local-macos-build.sh --legacy --install
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

PHOENIX_DIR="pkg/android/phoenix"
APP_DIR="cartridge/app"
REQUIRED_BUILD_TOOLS="30.0.3"
REQUIRED_NDK="22.0.7026061"
REQUIRED_PLATFORM="android-31"

legacy=0
full_build=0
do_clean=0
do_install=0
abi="arm64-v8a"

while [ $# -gt 0 ]; do
  case "$1" in
    --legacy|--phoenix) legacy=1 ;;
    --full) full_build=1 ;;
    --clean) do_clean=1 ;;
    --install) do_install=1 ;;
    --abi) abi="$2"; shift ;;
    -h|--help)
      cat <<'EOF'
Usage:
  cartridge/android-local-macos-build.sh                # fast: Cartridge (Expo/RN) app, arm64-v8a only
  cartridge/android-local-macos-build.sh --install       # ...then adb install -r + launch on a connected device
  cartridge/android-local-macos-build.sh --clean         # force a from-scratch native rebuild
  cartridge/android-local-macos-build.sh --full          # all ABIs instead of just arm64-v8a
  cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
  cartridge/android-local-macos-build.sh --legacy         # build pkg/android/phoenix (M0 NativeActivity frontend) instead
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

# --- Android SDK ----------------------------------------------------------
ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export ANDROID_HOME
if [ ! -d "$ANDROID_HOME" ]; then
  echo "error: Android SDK not found at $ANDROID_HOME (set ANDROID_HOME)." >&2
  exit 1
fi

install_apk() {
  local apk="$1"
  local component="$2" # e.g. com.cartridgeapp/.MainActivity, or empty to skip launch

  echo "Waiting for device..."
  adb wait-for-device

  echo "Installing $apk to connected device..."
  local install_ok=0
  for attempt in 1 2 3; do
    if adb install -r -t "$apk"; then
      install_ok=1
      break
    fi
    echo "adb install failed (attempt $attempt/3), retrying in 2s..." >&2
    sleep 2
  done
  [ "$install_ok" -eq 1 ] || { echo "error: adb install failed after 3 attempts" >&2; exit 1; }

  if [ -n "$component" ]; then
    echo "Forwarding Metro (tcp:8081) over adb reverse..."
    adb reverse tcp:8081 tcp:8081 || true

    echo "Launching $component..."
    adb shell am start -n "$component"
  fi
}

if [ "$legacy" -eq 1 ]; then
  # --- Legacy M0 frontend (pkg/android/phoenix) ----------------------------

  # JDK 11 -- the project pins Gradle 6.7.1 / AGP 4.2.0, which can't run on
  # Java 17 ("Unsupported class file major version 61").
  if ! java11_home="$(/usr/libexec/java_home -v 11 2>/dev/null)"; then
    echo "error: no Java 11 JDK found. Gradle 6.7.1 / AGP 4.2.0 (pinned for this" >&2
    echo "project) cannot run on newer JDKs. Install one, e.g.:" >&2
    echo "  brew install --cask temurin@11" >&2
    exit 1
  fi
  export JAVA_HOME="$java11_home"
  echo "Using JAVA_HOME=$JAVA_HOME"

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
    install_apk "$apk" ""
  fi

  exit 0
fi

# --- Cartridge Expo/RN app (cartridge/app) -------------------------------

# JDK 17 -- the Expo/RN project's own Gradle wrapper is Gradle 9.x, which
# requires 17+ (unlike the legacy frontend above, which is pinned to Gradle
# 6.7.1 and needs Java 11).
if ! java17_home="$(/usr/libexec/java_home -v 17 2>/dev/null)"; then
  echo "error: no Java 17 JDK found. The Cartridge Expo app's Gradle wrapper" >&2
  echo "needs 17+. Install one, e.g.:" >&2
  echo "  brew install --cask zulu@17" >&2
  exit 1
fi
export JAVA_HOME="$java17_home"
echo "Using JAVA_HOME=$JAVA_HOME"

if [ ! -d "$APP_DIR/node_modules" ]; then
  echo "Installing JS dependencies ($APP_DIR)..."
  (cd "$APP_DIR" && npm install)
fi

# expo prebuild (re)generates android/ from app.json + the cartridge-plugin
# config plugin -- it's gitignored, not checked in. Regenerate it whenever
# it's missing, or when --clean asks for a from-scratch native rebuild (e.g.
# after editing app.json, withCartridgeAndroid.js, or adding new native
# sources). Otherwise skip it so incremental JS/native rebuilds stay fast.
if [ ! -d "$APP_DIR/android" ] || [ "$do_clean" -eq 1 ]; then
  echo "Running expo prebuild (regenerating android/)..."
  (cd "$APP_DIR" && npx expo prebuild --platform android --no-install)
fi

cd "$APP_DIR/android"

gradle_args=("assembleDebug")
if [ "$full_build" -eq 0 ]; then
  echo "Building debug, ABI=$abi only (fast path for a handheld device)..."
  gradle_args+=("-PreactNativeArchitectures=$abi")
else
  echo "Building all ABIs (matches gradle.properties' reactNativeArchitectures)..."
fi

build_marker="$(mktemp)"
./gradlew "${gradle_args[@]}"

echo
echo "APKs built:"
find . -iname "*.apk" -newer "$build_marker" -exec ls -lh "{}" \;
rm -f "$build_marker"

if [ "$do_install" -eq 1 ]; then
  apk="$(find . -iname "app-debug.apk" | head -n1)"
  if [ -z "$apk" ]; then
    echo "error: could not find app-debug.apk to install" >&2
    exit 1
  fi
  install_apk "$apk" "com.cartridgeapp/.MainActivity"

  cat <<'EOF'

Metro isn't started automatically -- run it in another terminal if it's not
already running:
  (cd cartridge/app && npm start)
EOF
fi
