/*
 * Cartridge M1 -- customized copy of react-native's
 * ReactAndroid/cmake-utils/default-app-setup/OnLoad.cpp (see that file's own
 * header comment: this is the officially documented way to extend it).
 *
 * The only change from the default is cxxModuleProvider(): it now returns a
 * CartridgeModule for the "CartridgeModule" name before falling back to the
 * autolinked cxx module providers. CartridgeModule is a hand-written C++
 * TurboModule (plan.md section 4.3) with no Kotlin/Java class, so it can't
 * go through the Java module provider path codegen normally sets up --
 * this is the extension point RN provides for exactly that case.
 */

#include <DefaultComponentsRegistry.h>
#include <DefaultTurboModuleManagerDelegate.h>
#include <FBReactNativeSpec.h>
#include <autolinking.h>
#include <fbjni/fbjni.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>

// Resolved via the include dir CMakeLists.txt adds for this target
// (cartridge/native/), not a relative path -- robust to this file's copied
// location inside the generated Android project.
#include "turbomodule/CartridgeModule.h"

#ifdef REACT_NATIVE_APP_CODEGEN_HEADER
#include REACT_NATIVE_APP_CODEGEN_HEADER
#endif
#ifdef REACT_NATIVE_APP_COMPONENT_DESCRIPTORS_HEADER
#include REACT_NATIVE_APP_COMPONENT_DESCRIPTORS_HEADER
#endif

namespace facebook::react {

void registerComponents(
    std::shared_ptr<const ComponentDescriptorProviderRegistry> registry) {
  // Custom Fabric Components go here. RetroArchSurface's ComponentDescriptor
  // is registered automatically below via REACT_NATIVE_APP_COMPONENT_REGISTRATION
  // (generated from cartridge/app/src/specs/RetroArchSurfaceNativeComponent.ts) --
  // nothing to add here by hand.

#ifdef REACT_NATIVE_APP_COMPONENT_REGISTRATION
  REACT_NATIVE_APP_COMPONENT_REGISTRATION(registry);
#endif

  autolinking_registerProviders(registry);
}

std::shared_ptr<TurboModule> cxxModuleProvider(
    const std::string& name,
    const std::shared_ptr<CallInvoker>& jsInvoker) {
  if (name == "CartridgeModule") {
    return CartridgeModuleProvider(jsInvoker);
  }

  return autolinking_cxxModuleProvider(name, jsInvoker);
}

std::shared_ptr<TurboModule> javaModuleProvider(
    const std::string& name,
    const JavaTurboModule::InitParams& params) {
#ifdef REACT_NATIVE_APP_MODULE_PROVIDER
  auto module = REACT_NATIVE_APP_MODULE_PROVIDER(name, params);
  if (module != nullptr) {
    return module;
  }
#endif

  if (auto module = FBReactNativeSpec_ModuleProvider(name, params)) {
    return module;
  }

  if (auto module = autolinking_ModuleProvider(name, params)) {
    return module;
  }

  return nullptr;
}

} // namespace facebook::react

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  return facebook::jni::initialize(vm, [] {
    facebook::react::DefaultTurboModuleManagerDelegate::cxxModuleProvider =
        &facebook::react::cxxModuleProvider;
    facebook::react::DefaultTurboModuleManagerDelegate::javaModuleProvider =
        &facebook::react::javaModuleProvider;
    facebook::react::DefaultComponentsRegistry::
        registerComponentDescriptorsFromEntryPoint =
            &facebook::react::registerComponents;
  });
}
