package com.cartridgeapp

import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface

/**
 * Cartridge M1 -- the JNI binding surface for cartridge_host.c and
 * cartridge_input.c (cartridge/native/). A singleton object rather than
 * methods on MainActivity/RetroArchSurfaceManager directly: lifecycle calls
 * come from MainActivity, surface calls come from RetroArchSurfaceManager's
 * SurfaceHolder.Callback, and input injection comes from MainActivity's
 * dispatchKeyEvent/dispatchGenericMotionEvent -- three different Kotlin
 * classes that all need to drive the same underlying native session.
 *
 * See ai-specs/2026-07-09-react-native-interface/plan.md section 4.2 for why
 * this fabricates an ANativeActivity instead of using
 * android.app.NativeActivity, and cartridge_input.h for why game input needs
 * its own injection path instead of RetroArch's existing AInputQueue-based
 * android_input.c driver.
 *
 * `nativeCreate` is notably *not* here -- it's declared directly on
 * MainActivity because its `thiz` becomes activity->clazz, which
 * platform_unix.c resolves methods against by reflection. Every native
 * method actually declared here ignores its `thiz`, so a stateless
 * singleton object is fine for them.
 */
object CartridgeNative {

    init {
        System.loadLibrary("retroarch-activity")
    }

    // --- lifecycle: called from MainActivity ----------------------------

    external fun nativeStart()
    external fun nativeResume()
    external fun nativePause()
    external fun nativeStop()
    external fun nativeDestroy()
    external fun nativeWindowFocusChanged(hasFocus: Boolean)
    external fun nativeConfigurationChanged()

    // --- surface: called from RetroArchSurfaceManager's SurfaceHolder.Callback

    external fun nativeSurfaceCreated(surface: Surface)
    external fun nativeSurfaceChanged(left: Int, top: Int, right: Int, bottom: Int)
    external fun nativeSurfaceDestroyed()

    // --- reserved hotkey table: called from MainActivity.dispatchKeyEvent

    external fun nativeToggleMenu()

    // --- game input forwarding: called from MainActivity, only on API 31+
    // (see cartridge_input.c -- AKeyEvent_fromJava/AMotionEvent_fromJava
    // don't exist below that). Both return false if the event wasn't
    // convertible/forwarded, letting the caller fall back to default
    // handling if it wants to.

    external fun nativeInjectKeyEvent(event: KeyEvent): Boolean
    external fun nativeInjectMotionEvent(event: MotionEvent): Boolean
}
