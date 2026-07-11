package com.retroarch.cartridge

import android.Manifest
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.content.res.Configuration
import android.os.BatteryManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultNewArchitectureEntryPoint.fabricEnabled
import com.facebook.react.defaults.DefaultReactActivityDelegate

import expo.modules.ReactActivityDelegateWrapper

import java.io.File

/**
 * Cartridge M1 -- the real host Activity (plan.md M1: "RN host + FFI
 * skeleton"). Unlike M0's CartridgeSpikeActivity (a throwaway Activity whose
 * *entire* content view was RetroArch's SurfaceView), this is a normal
 * ReactActivity: RetroArch's SurfaceView is just one native component
 * (<RetroArchSurface/>, see RetroArchSurfaceManager) inside the RN view
 * tree, with RN's own views compositing above it for free via the hardware
 * compositor (plan.md section 4.2).
 *
 * Content is still loaded from Intent extras on boot, same as M0 -- a
 * same-Activity `loadContent` handoff from the launcher is M3 scope, and a
 * full MANAGE_EXTERNAL_STORAGE onboarding gate (plan.md section 2) is also
 * M3 ("storage-permission onboarding gate"); this uses the same
 * READ/WRITE_EXTERNAL_STORAGE runtime-permission request M0 used, which is
 * enough to unblock content loading on a test device today.
 */
class MainActivity : ReactActivity() {

    companion object {
        private const val TAG = "CartridgeMainActivity"
        private const val REQUEST_STORAGE_PERMISSION = 1001

        // Edit for your device/core/ROM, or override via adb intent extras:
        //   adb shell am start -n <applicationId>/.MainActivity \
        //       --es LIBRETRO /sdcard/.../some_libretro_android.so \
        //       --es ROM /sdcard/.../game.ext
        private const val DEFAULT_CORE_PATH = "/sdcard/RetroArch/cores/mgba_libretro_android.so"
        private const val DEFAULT_ROM_PATH = "/sdcard/RetroArch/roms/game.gba"

        // Reserved hotkey table (plan.md section 4.2): consumed here, never
        // forwarded into RetroArch's input driver. KEYCODE_BUTTON_MODE is the
        // "home"/guide button most retro handheld pads expose.
        private val RESERVED_HOTKEYS = setOf(KeyEvent.KEYCODE_BUTTON_MODE)

        init {
            System.loadLibrary("retroarch-activity")
        }
    }

    private external fun nativeCreate(
        assetManager: AssetManager,
        internalDataPath: String,
        sdkVersion: Int
    ): Boolean

    private var nativeReady = false

    override fun onCreate(savedInstanceState: Bundle?) {
        setTheme(R.style.AppTheme)
        super.onCreate(null)

        populateBootExtras()

        if (hasStoragePermission()) {
            bootstrapNative()
        } else {
            requestStoragePermission()
        }
    }

    override fun getMainComponentName(): String = "main"

    override fun createReactActivityDelegate(): ReactActivityDelegate {
        return ReactActivityDelegateWrapper(
            this,
            BuildConfig.IS_NEW_ARCHITECTURE_ENABLED,
            object : DefaultReactActivityDelegate(
                this,
                mainComponentName,
                fabricEnabled
            ) {}
        )
    }

    // --- storage permission + boot extras --------------------------------

    private fun hasStoragePermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return true
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun requestStoragePermission() {
        requestPermissions(
            arrayOf(
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            ),
            REQUEST_STORAGE_PERMISSION
        )
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_STORAGE_PERMISSION)
            bootstrapNative()
    }

    /** Mirrors the extras MainMenuActivity sets before launching
     * RetroActivityFuture (see frontend_unix_get_env() in platform_unix.c). */
    private fun populateBootExtras() {
        val i = intent
        if (!i.hasExtra("LIBRETRO"))
            i.putExtra("LIBRETRO", DEFAULT_CORE_PATH)
        if (!i.hasExtra("ROM"))
            i.putExtra("ROM", DEFAULT_ROM_PATH)
        if (!i.hasExtra("CONFIGFILE"))
            i.putExtra("CONFIGFILE", File(filesDir, "retroarch.cfg").absolutePath)
        if (!i.hasExtra("IME"))
            i.putExtra(
                "IME",
                Settings.Secure.getString(contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD) ?: ""
            )
        if (!i.hasExtra("DATADIR"))
            i.putExtra("DATADIR", applicationInfo.dataDir)
        if (!i.hasExtra("APK"))
            i.putExtra("APK", applicationInfo.sourceDir)
        if (!i.hasExtra("SDCARD"))
            i.putExtra("SDCARD", android.os.Environment.getExternalStorageDirectory().absolutePath)
        if (!i.hasExtra("EXTERNAL"))
            i.putExtra(
                "EXTERNAL",
                android.os.Environment.getExternalStorageDirectory().absolutePath +
                    "/Android/data/" + packageName + "/files"
            )
    }

    private fun bootstrapNative() {
        if (nativeReady) return
        nativeReady = nativeCreate(assets, filesDir.absolutePath, Build.VERSION.SDK_INT)
        Log.i(TAG, "nativeCreate -> $nativeReady")
    }

    // --- Activity lifecycle -> CartridgeNative ---------------------------

    override fun onStart() {
        super.onStart()
        if (nativeReady) CartridgeNative.nativeStart()
    }

    override fun onResume() {
        super.onResume()
        if (nativeReady) CartridgeNative.nativeResume()
    }

    override fun onPause() {
        if (nativeReady) CartridgeNative.nativePause()
        super.onPause()
    }

    override fun onStop() {
        if (nativeReady) CartridgeNative.nativeStop()
        super.onStop()
    }

    override fun onDestroy() {
        if (nativeReady) CartridgeNative.nativeDestroy()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (nativeReady) CartridgeNative.nativeWindowFocusChanged(hasFocus)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (nativeReady) CartridgeNative.nativeConfigurationChanged()
    }

    // --- hotkey interception + stock-driver input forwarding -------------
    //
    // Reserved combos are consumed here and never reach RetroArch. Everything
    // else is handed to cartridge_input.c's AKeyEvent_fromJava/
    // AMotionEvent_fromJava bridge (NDK API 31+), which runs it through
    // RetroArch's existing android_input.c dispatch unmodified -- no custom
    // input driver (plan.md section 4.2).

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (nativeReady && event.action == KeyEvent.ACTION_DOWN && event.keyCode in RESERVED_HOTKEYS) {
            CartridgeNative.nativeToggleMenu()
            return true
        }

        if (nativeReady && Build.VERSION.SDK_INT >= 31 && CartridgeNative.nativeInjectKeyEvent(event))
            return true

        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val isJoystickOrGamepad =
            (event.source and InputDevice.SOURCE_CLASS_JOYSTICK) != 0

        if (nativeReady && isJoystickOrGamepad && Build.VERSION.SDK_INT >= 31 &&
            CartridgeNative.nativeInjectMotionEvent(event)
        )
            return true

        return super.dispatchGenericMotionEvent(event)
    }

    // --- JNI-callable method surface -------------------------------------
    //
    // platform_unix.c resolves these by reflection against whatever runtime
    // class backs `android_app->activity->clazz` (nativeCreate's `thiz`,
    // i.e. this Activity) -- see the GET_METHOD_ID calls in
    // frontend_android_init()/frontend_unix_get_env(). Mirrors
    // RetroActivityCommon's/CartridgeSpikeActivity's signatures; input/
    // rumble/core-management stay no-ops for the same reasons M0 left them
    // as no-ops (game input now goes through dispatchKeyEvent/
    // dispatchGenericMotionEvent above instead of this reflection surface).

    @Suppress("unused")
    fun onRetroArchExit() {
        Log.i(TAG, "onRetroArchExit")
        finish()
    }

    @Suppress("unused")
    fun isAndroidTV(): Boolean = false

    @Suppress("unused")
    fun getPowerstate(): Int {
        val bm = getSystemService(BATTERY_SERVICE) as BatteryManager
        return if (bm.isCharging) 2 /* FRONTEND_POWERSTATE_CHARGING */ else 4 /* ON_POWER_SOURCE */
    }

    @Suppress("unused")
    fun getBatteryLevel(): Int {
        val bm = getSystemService(BATTERY_SERVICE) as BatteryManager
        return bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
    }

    @Suppress("unused")
    fun setSustainedPerformanceMode(on: Boolean) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N)
            window.setSustainedPerformanceMode(on)
    }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun setScreenOrientation(orientation: Int) { /* no-op: manifest/launcher controls orientation */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrate(id: Int, effect: Int, strength: Int, oneShot: Int) { /* no-op: M1 scope is hotkeys + input forwarding, not haptics */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrateJoypad(id: Int, strong: Int, weak: Int, unused: Int) { /* no-op */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrateUSB(deviceId: Int, strong: Int, weak: Int): Boolean = false

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doHapticFeedback(effect: Int) { /* no-op */ }

    @Suppress("unused")
    fun getUserLanguageString(): String = resources.configuration.locales[0].language

    @Suppress("unused")
    fun isPlayStoreBuild(): Boolean = false

    @Suppress("unused")
    fun getAvailableCores(): Array<String> = emptyArray()

    @Suppress("unused")
    fun getInstalledCores(): Array<String> = emptyArray()

    @Suppress("unused", "UNUSED_PARAMETER")
    fun downloadCore(coreName: String) { /* no-op: core browser/downloader is M3 */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun deleteCore(coreName: String) { /* no-op */ }

    @Suppress("unused")
    fun getVolumeCount(): Int = 0

    @Suppress("unused", "UNUSED_PARAMETER")
    fun getVolumePath(index: String): String = ""

    @Suppress("unused", "UNUSED_PARAMETER")
    fun inputGrabMouse(state: Boolean) { /* no-op */ }

    @Suppress("unused")
    fun isScreenReaderEnabled(): Boolean = false

    @Suppress("unused", "UNUSED_PARAMETER")
    fun accessibilitySpeak(message: String) { /* no-op */ }

    @Suppress("unused")
    fun requestOpenDocumentTree() { /* no-op: SAF isn't used (plan.md: full filesystem access, not scoped storage) */ }

    @Suppress("unused")
    fun getPersistedSafTrees(): Array<String> = emptyArray()
}
