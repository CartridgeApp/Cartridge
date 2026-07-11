# Cartridge fork tooling

Scripts specific to this fork (as opposed to upstream RetroArch). Currently
just local Android build tooling.

## android-local-macos-build.sh

Builds an Android frontend locally on macOS for testing, without needing
Docker or Android Studio. There are two targets:

- **Cartridge (default)** — the Expo/RN app at `cartridge/app`, package
  `com.cartridgeapp`, app name "Cartridge". This is the M1+ React Native
  host: `<RetroArchSurface/>` + the C++ TurboModule + the native shim, all
  wired together by `expo prebuild` and the `withCartridgeAndroid.js` config
  plugin.
- **`--legacy`** — the old M0 NativeActivity frontend (`pkg/android/phoenix`,
  package `com.retroarch`), useful for A/B-testing against
  `CartridgeSpikeActivity` or anything else that predates the RN host.

CI (`.github/workflows/Android.yml`) builds the legacy frontend with
`./gradlew assembleDebug` on a GitHub-hosted runner that already has the
right SDK/NDK/JDK preinstalled. On a dev Mac that's usually not true, so this
script:

- **Picks a working JDK per target.** The legacy frontend pins Gradle 6.7.1 /
  AGP 4.2.0 (`pkg/android/phoenix/build.gradle`), which needs Java 11 — Java
  17 fails with `Unsupported class file major version 61`. The Cartridge
  Expo app's own Gradle wrapper is Gradle 9.x, which needs Java 17+ instead.
  The script picks the right one per target via `/usr/libexec/java_home` and
  points `JAVA_HOME` at it for the build only (it doesn't touch your shell's
  default).
- **Installs missing SDK/NDK pieces** (`--legacy` only — the Cartridge app's
  own Gradle/NDK versions are resolved by AGP/the RN Gradle plugin). Checks
  `$ANDROID_HOME` for the exact `build-tools`/`ndk`/`platform` versions
  `pkg/android/phoenix/build.gradle` pins, and runs `sdkmanager` to fetch
  anything missing.
- **Runs `expo prebuild` when needed** (Cartridge target only). `cartridge/app/android`
  is generated, not checked in (see `cartridge/app/.gitignore`) — the script
  regenerates it the first time it's missing, or whenever you pass
  `--clean` (e.g. after editing `app.json`, `withCartridgeAndroid.js`, or
  adding new native sources). Otherwise it's left alone so incremental
  Gradle/NDK rebuilds stay fast.
- **Builds one ABI by default**, instead of every ABI (Cartridge target) or
  every flavor × every ABI (legacy target, 5 flavors: `normal`, `aarch64`,
  `ra32`, `playStoreNormal`, `playStorePlus`) CI produces. Defaults to
  `arm64-v8a` (i.e. a real ARM64 device — Anbernic, Retroid, Ayn Odin, and
  basically every other Android retro handheld are `arm64-v8a`).

### Requirements

- Android SDK installed (defaults to `~/Library/Android/sdk`, override with
  `ANDROID_HOME`).
- A Java 17 JDK for the Cartridge target (`brew install --cask zulu@17` or
  similar), or a Java 11 JDK for `--legacy` (`brew install --cask temurin@11`),
  findable via `/usr/libexec/java_home -v <version>`.
- `--legacy` also needs the `cmdline-tools` SDK package so `sdkmanager` is
  available.
- `adb` on your `PATH` if you use `--install`.
- For the Cartridge target: JS deps get installed automatically
  (`npm install` in `cartridge/app`) if `node_modules` is missing.

### Usage

```
cartridge/android-local-macos-build.sh                # fast: Cartridge app, arm64-v8a only
cartridge/android-local-macos-build.sh --install       # ...then adb install -r + launch it on a connected device
cartridge/android-local-macos-build.sh --clean         # force a from-scratch native rebuild (expo prebuild --clean / gradlew clean)
cartridge/android-local-macos-build.sh --full          # all ABIs instead of just arm64-v8a
cartridge/android-local-macos-build.sh --abi x86_64    # single-ABI build for a different ABI
cartridge/android-local-macos-build.sh --legacy         # build pkg/android/phoenix (M0) instead
cartridge/android-local-macos-build.sh --legacy --install
```

Flags can be combined, e.g. `--clean --install` for a from-scratch build
that's pushed straight to a connected device.

Built APKs land under `cartridge/app/android/app/build/outputs/apk/debug/`
(Cartridge target) or `pkg/android/phoenix/build/outputs/apk/<flavor>/debug/`
(`--legacy`); the script prints the path(s) of whatever it just built.

### Testing on a physical handheld

1. Enable Developer Options + USB debugging on the device, connect over
   USB (or `adb connect <ip>:5555` over Wi-Fi), and confirm it shows up in
   `adb devices`.
2. Start Metro in another terminal (Cartridge target only — debug builds load
   JS from it, not an embedded bundle): `cd cartridge/app && npm start`.
3. Run `cartridge/android-local-macos-build.sh --install`. This installs the
   APK, `adb reverse tcp:8081 tcp:8081`s Metro over USB, and launches
   `com.cartridgeapp/.MainActivity`.
4. `MainActivity` boots straight into the demo UI even with no launch Intent
   extras — it falls back to hardcoded core/ROM paths
   (`MainActivity.kt`'s `DEFAULT_CORE_PATH`/`DEFAULT_ROM_PATH`; edit those or
   pass `--es LIBRETRO ... --es ROM ...` via `adb shell am start` for a
   different core/ROM).
5. **The core (`LIBRETRO` path) must live under the app's private data dir**
   (`/data/data/com.cartridgeapp/...`), not external/SD storage — Android's
   dynamic linker refuses to `dlopen()` executable code from anywhere outside
   a small set of trusted paths, independent of file permissions. If your
   core is inside another app's private storage (e.g. RetroArch's own
   downloaded core), pull it out via `adb shell run-as <that-app> cat
   cores/foo.so > foo.so`, then push it in via `adb shell run-as
   com.cartridgeapp` (debug builds only) into `cores/`. The ROM can stay on
   external storage since it's just read as data, not executable.

### Notes / caveats

- `--abi` and the default single-ABI path work differently per target: for
  `--legacy` it's `-Pandroid.injected.build.abi=<abi>`, an AGP-internal
  property (normally used by Android Studio's "Apply Changes"), not a
  documented public API — fall back to `--full` if a future AGP bump changes
  its behavior. For the Cartridge target it's the documented
  `-PreactNativeArchitectures=<abi>` Gradle property.
- The script only ever touches `JAVA_HOME`/`ANDROID_HOME` for its own
  subprocess — it doesn't modify your shell profile or global environment.
- SDK components are installed side-by-side by version, so this won't
  remove or downgrade any other SDK/NDK versions you have installed for
  other projects.
