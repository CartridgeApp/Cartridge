# Cartridge fork tooling

Scripts specific to this fork (as opposed to upstream RetroArch). Currently
just local Android build tooling.

## android-local-macos-build.sh

Builds the Android frontend (`pkg/android/phoenix`) locally on macOS for
testing, without needing Docker or Android Studio.

CI (`.github/workflows/Android.yml`) just runs `./gradlew assembleDebug` on a
GitHub-hosted runner that already has the right SDK/NDK/JDK preinstalled. On a
dev Mac that's usually not true, so this script:

- **Picks a working JDK.** The project pins Gradle 6.7.1 and AGP 4.2.0
  (`pkg/android/phoenix/build.gradle`), which can't run on Java 17 — you'll
  see `Unsupported class file major version 61`. The script finds a Java 11
  JDK via `/usr/libexec/java_home -v 11` and points `JAVA_HOME` at it for the
  build only (it doesn't touch your shell's default).
- **Installs missing SDK/NDK pieces.** Checks `$ANDROID_HOME` for the exact
  `build-tools`, `ndk`, and `platform` versions `build.gradle` pins, and runs
  `sdkmanager` to fetch anything missing.
- **Builds one ABI by default**, instead of every flavor × every ABI CI
  produces. Full CI output is 5 flavors (`normal`, `aarch64`, `ra32`,
  `playStoreNormal`, `playStorePlus`) and takes ~7 minutes clean. This script
  defaults to just `aarch64` debug restricted to `arm64-v8a` (i.e. a real
  ARM64 device — Anbernic, Retroid, Ayn Odin, and basically every other
  Android retro handheld are `arm64-v8a`), which is ~30s clean and a few
  seconds incremental.

### Requirements

- Android SDK installed (defaults to `~/Library/Android/sdk`, override with
  `ANDROID_HOME`), including the `cmdline-tools` package so `sdkmanager` is
  available.
- A Java 11 JDK installed somewhere `/usr/libexec/java_home -v 11` can find
  it. If you don't have one: `brew install --cask temurin@11`.
- `adb` on your `PATH` if you use `--install`.

### Usage

```
cartridge/android-local-macos-build.sh                # fast: aarch64 debug, arm64-v8a only
cartridge/android-local-macos-build.sh --install       # ...then adb install -r it to a connected device
cartridge/android-local-macos-build.sh --full          # everything CI builds (all flavors/ABIs)
cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
cartridge/android-local-macos-build.sh --clean         # ./gradlew clean first
```

Flags can be combined, e.g. `--clean --install` for a from-scratch build
that's pushed straight to a connected device.

Built APKs land under
`pkg/android/phoenix/build/outputs/apk/<flavor>/debug/`; the script prints
the path(s) of whatever it just built. For the default fast path, that's
`pkg/android/phoenix/build/outputs/apk/aarch64/debug/phoenix-aarch64-debug.apk`.

### Testing on a physical handheld

1. Enable Developer Options + USB debugging on the device, connect over
   USB (or `adb connect <ip>:5555` over Wi-Fi), and confirm it shows up in
   `adb devices`.
2. Run `cartridge/android-local-macos-build.sh --install`.

### Notes / caveats

- `--abi` and the default single-ABI path work by passing
  `-Pandroid.injected.build.abi=<abi>` to Gradle. That's an AGP-internal
  property (normally used by Android Studio's "Apply Changes"), not a
  documented public API. It's been reliable in testing, but if a future AGP
  bump changes its behavior, fall back to `--full`.
- The script only ever touches `JAVA_HOME`/`ANDROID_HOME` for its own
  subprocess — it doesn't modify your shell profile or global environment.
- SDK components are installed side-by-side by version, so this won't
  remove or downgrade any other SDK/NDK versions you have installed for
  other projects.
