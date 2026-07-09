package com.retroarch.cartridge

import android.app.Activity
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.content.res.Configuration
import android.graphics.PixelFormat
import android.os.BatteryManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.FrameLayout
import com.retroarch.browser.preferences.util.UserPreferences

/**
 * Cartridge M0 -- library-mode spike (throwaway, no RN).
 *
 * Boots libretroarch.so in library mode from a plain Activity + SurfaceView
 * instead of android.app.NativeActivity, to retire the single biggest
 * unknown in ai-specs/2026-07-09-react-native-interface/plan.md before any
 * RN code is written. See cartridge/native/cartridge_spike.c for how the
 * native side fabricates an ANativeActivity and drives RetroArch's existing
 * platform_unix.c callback table directly.
 *
 * Deliberately out of scope for this spike (all M1+ concerns):
 *  - game input forwarding (touch overlay / controllers) -- M1's job.
 *  - the RN view tree / TurboModule FFI -- M1/M2.
 *  - Core/content selection UI -- launcher's job, not exercised here.
 *
 * Core and content paths are hardcoded below for a specific device/game;
 * override without rebuilding via:
 *   adb shell am start -n com.retroarch.aarch64/com.retroarch.cartridge.CartridgeSpikeActivity \
 *       --es LIBRETRO /sdcard/.../some_libretro_android.so \
 *       --es ROM /sdcard/.../game.ext
 */
class CartridgeSpikeActivity : Activity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "CartridgeSpike"
        private const val REQUEST_STORAGE_PERMISSION = 1001

        // Edit for your device/core/ROM, or override via adb intent extras (see above).
        private const val DEFAULT_CORE_PATH = "/sdcard/RetroArch/cores/mgba_libretro_android.so"
        private const val DEFAULT_ROM_PATH = "/sdcard/RetroArch/roms/game.gba"

        init {
            System.loadLibrary("retroarch-activity")
        }
    }

    private external fun nativeCreate(assetManager: AssetManager, internalDataPath: String, sdkVersion: Int): Boolean
    private external fun nativeSurfaceCreated(surface: Surface)
    private external fun nativeSurfaceChanged(left: Int, top: Int, right: Int, bottom: Int)
    private external fun nativeSurfaceDestroyed()
    private external fun nativeStart()
    private external fun nativeResume()
    private external fun nativePause()
    private external fun nativeStop()
    private external fun nativeDestroy()
    private external fun nativeWindowFocusChanged(hasFocus: Boolean)
    private external fun nativeConfigurationChanged()

    private lateinit var surfaceView: SurfaceView
    private var nativeReady = false

    // --- JNI-callable method surface ------------------------------------
    //
    // platform_unix.c resolves these by reflection against whatever runtime
    // class backs `android_app->activity->clazz` (see the GET_METHOD_ID
    // calls in frontend_android_init() / frontend_unix_get_env()) -- it
    // does not care that this class isn't RetroActivityCommon. getIntent()
    // and getContentResolver() are inherited from Activity/Context for
    // free; the rest mirror RetroActivityCommon's signatures with minimal
    // (often no-op) bodies, since input/rumble/core-management are out of
    // scope for this boot-parity spike.

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
    fun setScreenOrientation(orientation: Int) {
        /* no-op: manifest/launcher controls orientation for this spike */
    }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrate(id: Int, effect: Int, strength: Int, oneShot: Int) { /* no-op: input is M1 scope */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrateJoypad(id: Int, strong: Int, weak: Int, unused: Int) { /* no-op: input is M1 scope */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doVibrateUSB(deviceId: Int, strong: Int, weak: Int): Boolean = false

    @Suppress("unused", "UNUSED_PARAMETER")
    fun doHapticFeedback(effect: Int) { /* no-op: input is M1 scope */ }

    @Suppress("unused")
    fun getUserLanguageString(): String = resources.configuration.locales[0].language

    @Suppress("unused")
    fun isPlayStoreBuild(): Boolean = false

    @Suppress("unused")
    fun getAvailableCores(): Array<String> = emptyArray()

    @Suppress("unused")
    fun getInstalledCores(): Array<String> = emptyArray()

    @Suppress("unused", "UNUSED_PARAMETER")
    fun downloadCore(coreName: String) { /* no-op: no launcher/core-updater in this spike */ }

    @Suppress("unused", "UNUSED_PARAMETER")
    fun deleteCore(coreName: String) { /* no-op */ }

    @Suppress("unused")
    fun getVolumeCount(): Int = 0

    @Suppress("unused", "UNUSED_PARAMETER")
    fun getVolumePath(index: String): String = ""

    @Suppress("unused", "UNUSED_PARAMETER")
    fun inputGrabMouse(state: Boolean) { /* no-op: input is M1 scope */ }

    @Suppress("unused")
    fun isScreenReaderEnabled(): Boolean = false

    @Suppress("unused", "UNUSED_PARAMETER")
    fun accessibilitySpeak(message: String) { /* no-op */ }

    @Suppress("unused")
    fun requestOpenDocumentTree() { /* no-op: SAF isn't exercised by this spike */ }

    @Suppress("unused")
    fun getPersistedSafTrees(): Array<String> = emptyArray()

    // ---------------------------------------------------------------------

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON or
                WindowManager.LayoutParams.FLAG_FULLSCREEN
        )

        populateBootExtras()

        surfaceView = SurfaceView(this)
        surfaceView.holder.setFormat(PixelFormat.RGBA_8888)
        surfaceView.holder.addCallback(this)
        setContentView(
            surfaceView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        if (hasStoragePermission()) {
            bootstrapNative()
        } else {
            requestStoragePermission()
        }
    }

    private fun hasStoragePermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return true
        return checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun requestStoragePermission() {
        requestPermissions(
            arrayOf(
                android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE
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

    /** Fills in the same Intent extras MainMenuActivity sets before launching
     *  RetroActivityFuture (see frontend_unix_get_env() in platform_unix.c) --
     *  only if the launcher didn't already provide them via adb. */
    private fun populateBootExtras() {
        val i = intent
        if (!i.hasExtra("LIBRETRO"))
            i.putExtra("LIBRETRO", DEFAULT_CORE_PATH)
        if (!i.hasExtra("ROM"))
            i.putExtra("ROM", DEFAULT_ROM_PATH)
        if (!i.hasExtra("CONFIGFILE"))
            i.putExtra("CONFIGFILE", UserPreferences.getDefaultConfigPath(this))
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
            i.putExtra("SDCARD", Environment.getExternalStorageDirectory().absolutePath)
        if (!i.hasExtra("EXTERNAL"))
            i.putExtra(
                "EXTERNAL",
                Environment.getExternalStorageDirectory().absolutePath + "/Android/data/" + packageName + "/files"
            )
    }

    private fun bootstrapNative() {
        if (nativeReady) return
        UserPreferences.updateConfigFile(this)
        nativeReady = nativeCreate(assets, filesDir.absolutePath, Build.VERSION.SDK_INT)
        Log.i(TAG, "nativeCreate -> $nativeReady")
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (nativeReady) nativeSurfaceCreated(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (nativeReady) nativeSurfaceChanged(0, 0, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (nativeReady) nativeSurfaceDestroyed()
    }

    override fun onStart() {
        super.onStart()
        if (nativeReady) nativeStart()
    }

    override fun onResume() {
        super.onResume()
        if (nativeReady) nativeResume()
    }

    override fun onPause() {
        if (nativeReady) nativePause()
        super.onPause()
    }

    override fun onStop() {
        if (nativeReady) nativeStop()
        super.onStop()
    }

    override fun onDestroy() {
        if (nativeReady) nativeDestroy()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (nativeReady) nativeWindowFocusChanged(hasFocus)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (nativeReady) nativeConfigurationChanged()
    }
}
