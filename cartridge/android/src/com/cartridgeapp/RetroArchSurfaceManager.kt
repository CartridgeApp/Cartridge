package com.cartridgeapp

import android.view.SurfaceHolder
import android.view.SurfaceView
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.viewmanagers.RetroArchSurfaceManagerDelegate
import com.facebook.react.viewmanagers.RetroArchSurfaceManagerInterface

/**
 * Cartridge M1 -- Fabric ViewManager for `<RetroArchSurface/>`
 * (cartridge/app/src/specs/RetroArchSurfaceNativeComponent.ts). Owns the
 * SurfaceView RetroArch's GL/Vulkan driver renders into; forwards its
 * SurfaceHolder callbacks to the same native window plumbing
 * CartridgeSpikeActivity drove directly in M0 (plan.md section 4.2).
 *
 * No custom props (RetroArchSurfaceManagerInterface is empty), so the
 * generated delegate is a pure passthrough -- still wired up because Fabric
 * expects a ViewManagerDelegate to exist for a codegen'd component.
 */
class RetroArchSurfaceManager :
    SimpleViewManager<SurfaceView>(),
    RetroArchSurfaceManagerInterface<SurfaceView> {

    private val delegate: ViewManagerDelegate<SurfaceView> = RetroArchSurfaceManagerDelegate(this)

    override fun getDelegate(): ViewManagerDelegate<SurfaceView> = delegate

    override fun getName(): String = REACT_CLASS

    override fun createViewInstance(context: ThemedReactContext): SurfaceView {
        val surfaceView = SurfaceView(context)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                CartridgeNative.nativeSurfaceCreated(holder.surface)
            }

            override fun surfaceChanged(
                holder: SurfaceHolder,
                format: Int,
                width: Int,
                height: Int
            ) {
                CartridgeNative.nativeSurfaceChanged(0, 0, width, height)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                CartridgeNative.nativeSurfaceDestroyed()
            }
        })
        return surfaceView
    }

    companion object {
        const val REACT_CLASS = "RetroArchSurface"
    }
}
