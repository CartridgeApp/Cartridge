package com.cartridgeapp

import com.facebook.react.ReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.uimanager.ViewManager

/**
 * Cartridge M1 -- registers <RetroArchSurface/>. CartridgeModule itself is
 * *not* registered here: it's a hand-written C++ TurboModule (plan.md
 * section 4.3), wired directly into the JS TurboModuleManager via
 * cxxModuleProvider in OnLoad.cpp, not through a Kotlin ReactPackage.
 */
class CartridgePackage : ReactPackage {
    override fun createNativeModules(reactContext: ReactApplicationContext): List<NativeModule> =
        emptyList()

    override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> =
        listOf(RetroArchSurfaceManager())
}
