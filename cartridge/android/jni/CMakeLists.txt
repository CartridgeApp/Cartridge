# Cartridge M1 -- customized copy of react-native's
# ReactAndroid/cmake-utils/default-app-setup/CMakeLists.txt (see that file's
# own header comment: this is the officially documented way to extend it).
#
# Adds CartridgeModule.cpp (the hand-written JSI TurboModule,
# cartridge/native/turbomodule/) to RN's own CMake build, and links it
# against libretroarch-activity.so -- built separately by the
# `buildRetroArchNative` ndk-build Gradle task (app/build.gradle) because
# griffin's build stays Android.mk/ndk-build based and AGP only allows one
# externalNativeBuild tool per module. See withCartridgeAndroid.js for the
# full explanation.

cmake_minimum_required(VERSION 3.13)

project(appmodules)

include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)

# This file is copied to <expo-app>/android/app/src/main/jni/CMakeLists.txt
# by withCartridgeAndroid.js; six levels up from there is the repo-root
# cartridge/ directory (jni -> main -> src -> app -> android -> <expo-app> -> cartridge).
get_filename_component(CARTRIDGE_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../native" ABSOLUTE)
get_filename_component(CARTRIDGE_REPO_ROOT "${CARTRIDGE_NATIVE_DIR}/../.." ABSOLUTE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CARTRIDGE_NATIVE_DIR}/turbomodule/CartridgeModule.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CARTRIDGE_NATIVE_DIR}
    # cartridge_api.h/cartridge_events.h pull in libretro-common's boolean.h
    # and retro_common_api.h, the same as any other RetroArch source file.
    ${CARTRIDGE_REPO_ROOT}/libretro-common/include
)

# CARTRIDGE_NDKBUILD_LIBS_DIR is passed in from app/build.gradle
# (defaultConfig.externalNativeBuild.cmake.arguments) -- see
# withCartridgeAndroid.js's withCartridgeGradle().
if(NOT DEFINED CARTRIDGE_NDKBUILD_LIBS_DIR)
    message(FATAL_ERROR "CARTRIDGE_NDKBUILD_LIBS_DIR not set -- see withCartridgeAndroid.js")
endif()

add_library(retroarch-activity SHARED IMPORTED)
set_target_properties(retroarch-activity PROPERTIES
    IMPORTED_LOCATION "${CARTRIDGE_NDKBUILD_LIBS_DIR}/${ANDROID_ABI}/libretroarch-activity.so"
)

target_link_libraries(${CMAKE_PROJECT_NAME} retroarch-activity)
