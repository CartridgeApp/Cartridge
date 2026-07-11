/**
 * Cartridge M1 -- Expo config plugin wiring RetroArch's native build and the
 * hand-written CartridgeModule TurboModule into the prebuild-generated
 * Android project.
 *
 * See ai-specs/2026-07-09-react-native-interface/plan.md section 4.1/5:
 * RetroArch's own griffin build stays ndk-build (Android.mk) based --
 * reusing pkg/android/phoenix-common/jni/Android.mk verbatim, the same file
 * M0 already extended with a guarded HAVE_CARTRIDGE block -- while RN's own
 * native glue (JSI/Fabric/autolinking) is CMake-based as of RN 0.86 and
 * isn't something AGP lets a single Gradle module build with two different
 * native build systems at once. So:
 *
 *   - griffin + cartridge_api/cartridge_events/cartridge_input/cartridge_host
 *     build into libretroarch-activity.so via a plain `ndk-build` Gradle
 *     Exec task (mirroring what AGP's own `externalNativeBuild { ndkBuild }`
 *     does for the legacy pkg/android/phoenix project), whose output is
 *     dropped into src/main/jniLibs so Gradle packages it like any other
 *     prebuilt .so.
 *   - CartridgeModule.cpp (the JSI TurboModule) builds via RN's own CMake
 *     build, customized the way RN's default-app-setup/CMakeLists.txt and
 *     OnLoad.cpp explicitly document ("copy this file into
 *     android/app/src/main/jni and extend it").
 *
 * This is a refinement of the plan's original "our own
 * cartridge/android/jni/Android.mk" framing, discovered while actually
 * wiring up RN 0.86's New Architecture build (which turned out to be
 * CMake-first) -- the same spirit (additive, small guarded diffs) applied
 * to what's actually true today, the way M0 discovered NativeActivity isn't
 * special to RetroArch's C code.
 */
const {
  withAppBuildGradle,
  withMainApplication,
  withDangerousMod,
} = require('@expo/config-plugins');
const fs = require('fs');
const path = require('path');

// cartridge/app/cartridge-plugin/ -> cartridge/
const CARTRIDGE_DIR = path.resolve(__dirname, '..', '..');
const KOTLIN_PACKAGE_PATH = 'com/retroarch/cartridge';

function copyFileSync(src, dest) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.copyFileSync(src, dest);
}

/** Copies our hand-written Kotlin sources over the prebuild-generated ones,
 * overwriting the stub MainActivity.kt Expo generates (we replace it
 * entirely -- see MainActivity.kt's own header comment) and adding the
 * files that don't collide with anything generated. */
function withCartridgeKotlinSources(config) {
  return withDangerousMod(config, [
    'android',
    async (config) => {
      const srcDir = path.join(CARTRIDGE_DIR, 'android', 'src', KOTLIN_PACKAGE_PATH);
      const destDir = path.join(
        config.modRequest.platformProjectRoot,
        'app', 'src', 'main', 'java', KOTLIN_PACKAGE_PATH
      );

      for (const file of fs.readdirSync(srcDir)) {
        copyFileSync(path.join(srcDir, file), path.join(destDir, file));
      }

      return config;
    },
  ]);
}

/** Copies the customized CMakeLists.txt/OnLoad.cpp into src/main/jni, per
 * the customization steps documented at the top of RN's own
 * default-app-setup/CMakeLists.txt. */
function withCartridgeJniGlue(config) {
  return withDangerousMod(config, [
    'android',
    async (config) => {
      const srcDir = path.join(CARTRIDGE_DIR, 'android', 'jni');
      const destDir = path.join(
        config.modRequest.platformProjectRoot,
        'app', 'src', 'main', 'jni'
      );

      for (const file of ['CMakeLists.txt', 'OnLoad.cpp']) {
        copyFileSync(path.join(srcDir, file), path.join(destDir, file));
      }

      return config;
    },
  ]);
}

/** Registers CartridgePackage (RetroArchSurface's ViewManager) in
 * MainApplication.kt's autolinked package list. CartridgeModule itself is
 * NOT registered here -- it's wired directly into OnLoad.cpp's
 * cxxModuleProvider instead (plan.md section 4.3: pure C++ TurboModule, no
 * Kotlin/Java class). */
function withCartridgePackageRegistration(config) {
  return withMainApplication(config, (config) => {
    let { contents } = config.modResults;

    if (!contents.includes('com.retroarch.cartridge.CartridgePackage')) {
      contents = contents.replace(
        'import com.facebook.react.PackageList',
        'import com.facebook.react.PackageList\nimport com.retroarch.cartridge.CartridgePackage'
      );
      contents = contents.replace(
        'PackageList(this).packages.apply {',
        'PackageList(this).packages.apply {\n          add(CartridgePackage())'
      );
    }

    config.modResults.contents = contents;
    return config;
  });
}

/** Points app/build.gradle's CMake build at our customized
 * src/main/jni/CMakeLists.txt (housing CartridgeModule.cpp alongside RN's
 * own JSI/Fabric glue), and registers the Gradle task that runs `ndk-build`
 * against RetroArch's Android.mk to produce libretroarch-activity.so. */
function withCartridgeGradle(config) {
  config = withAppBuildGradle(config, (config) => {
    let { contents } = config.modResults;

    if (!contents.includes('src/main/jni/CMakeLists.txt')) {
      contents = contents.replace(
        /android\s*\{/,
        `android {\n    externalNativeBuild {\n        cmake {\n            path "src/main/jni/CMakeLists.txt"\n        }\n    }`
      );
    }

    // Tells our customized CMakeLists.txt (see withCartridgeJniGlue) where
    // buildRetroArchNative (below) drops libretroarch-activity.so, so it can
    // be linked as an IMPORTED target for CartridgeModule.cpp's extern "C"
    // calls into cartridge_api.h/cartridge_events.h.
    if (!contents.includes('CARTRIDGE_NDKBUILD_LIBS_DIR')) {
      contents = contents.replace(
        /defaultConfig\s*\{/,
        `defaultConfig {\n        externalNativeBuild {\n            cmake {\n                arguments "-DCARTRIDGE_NDKBUILD_LIBS_DIR=${'$'}{project.buildDir}/cartridge-ndkbuild/libs"\n            }\n        }`
      );
    }

    if (!contents.includes('buildRetroArchNative')) {
      contents += `
// Cartridge M1: builds RetroArch (griffin) + cartridge_api/cartridge_events/
// cartridge_input/cartridge_host into libretroarch-activity.so via ndk-build,
// reusing pkg/android/phoenix-common/jni/Android.mk verbatim -- the same
// ndk-build invocation AGP's own externalNativeBuild.ndkBuild block performs
// for the legacy pkg/android/phoenix project, just run by hand here because
// this module's externalNativeBuild slot is taken by RN's CMake build (see
// withCartridgeAndroid.js for why both are needed).
def cartridgeAndroidMk = file("${'$'}{project.projectDir}/../../../../pkg/android/phoenix-common/jni/Android.mk")
def cartridgeNdkLibsOut = file("${'$'}{project.buildDir}/cartridge-ndkbuild/libs")
def cartridgeNdkObjOut = file("${'$'}{project.buildDir}/cartridge-ndkbuild/obj")

tasks.register("buildRetroArchNative", Exec) {
    inputs.file(cartridgeAndroidMk)
    outputs.dir(cartridgeNdkLibsOut)

    def ndkBuildAbis = (findProperty("reactNativeArchitectures") ?: "arm64-v8a,x86_64").toString()

    def ndkDir = android.ndkDirectory
    // NDK_PROJECT_PATH must point at phoenix-common/ (the dir *containing*
    // jni/), not "null" -- "null" tells ndk-build to skip jni/Application.mk
    // entirely, which is what sets APP_STL := c++_static; without it griffin's
    // C++ files (griffin_cpp.cpp, griffin_glslang.cpp) link with undefined
    // std::__ndk1::* symbols. APP_PLATFORM is still overridden below (NDK 31+,
    // for cartridge_input.c's AKeyEvent_fromJava/AMotionEvent_fromJava) since
    // command-line values take precedence over Application.mk's.
    commandLine "${'$'}{ndkDir}/ndk-build".toString(),
        "NDK_PROJECT_PATH=${'$'}{cartridgeAndroidMk.parentFile.parentFile}".toString(),
        "APP_BUILD_SCRIPT=${'$'}{cartridgeAndroidMk}".toString(),
        "APP_PLATFORM=android-31",
        "APP_ABI=${'$'}{ndkBuildAbis.replace(',', ' ')}".toString(),
        "NDK_LIBS_OUT=${'$'}{cartridgeNdkLibsOut}".toString(),
        "NDK_OUT=${'$'}{cartridgeNdkObjOut}".toString(),
        "-j${'$'}{Runtime.runtime.availableProcessors()}".toString()
}

android {
    sourceSets {
        main {
            jniLibs.srcDirs += cartridgeNdkLibsOut
        }
    }
}

afterEvaluate {
    // AGP's actual CMake task names are configureCMake<Variant>[<abi>] /
    // buildCMake<Variant>[<abi>] (there is no "externalNativeBuild*"-prefixed
    // task -- an earlier version of this wiring guessed wrong and only
    // "worked" because buildRetroArchNative's output was already cached on
    // disk from a previous manual run). CMake needs libretroarch-activity.so
    // to exist by configure time (IMPORTED_LOCATION) as well as link time,
    // so both configure and build tasks depend on it.
    tasks.matching { it.name.startsWith("merge") && it.name.endsWith("JniLibFolders") }.configureEach {
        dependsOn("buildRetroArchNative")
    }
    tasks.matching { it.name.startsWith("configureCMake") || it.name.startsWith("buildCMake") }.configureEach {
        dependsOn("buildRetroArchNative")
    }
}
`;
    }

    config.modResults.contents = contents;
    return config;
  });

  return config;
}

module.exports = function withCartridgeAndroid(config) {
  config = withCartridgeKotlinSources(config);
  config = withCartridgeJniGlue(config);
  config = withCartridgePackageRegistration(config);
  config = withCartridgeGradle(config);
  return config;
};
