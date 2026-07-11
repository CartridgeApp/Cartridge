/*  Cartridge M1 -- real library-mode host (not a spike).
 *
 *  Same ANativeActivity-fabrication trick M0's cartridge_spike.c proved out
 *  (see that file's header comment for the full explanation), bound to
 *  CartridgeNative instead of the throwaway CartridgeSpikeActivity so it can
 *  be driven by MainActivity (a real ReactActivity) and
 *  RetroArchSurfaceManager (the Fabric ViewManager owning the SurfaceView)
 *  rather than a single hardcoded debug Activity.
 *
 *  CartridgeSpikeActivity/cartridge_spike.c are left in place deliberately:
 *  a standalone, RN-independent boot path is still useful for isolating
 *  "is this a RetroArch problem or an RN problem" during bring-up (in the
 *  spirit of plan.md section 4.2's Ozone A/B escape hatch).
 *
 *  Compiled into libretroarch-activity.so alongside griffin (see
 *  pkg/android/phoenix-common/jni/Android.mk, HAVE_CARTRIDGE), same as
 *  cartridge_spike.c.
 */
#ifdef HAVE_CARTRIDGE

#include <string.h>
#include <stdlib.h>

#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

/* See cartridge_spike.c for why platform_unix.h itself isn't included here. */
struct android_app;
extern struct android_app *cartridge_android_app_create(
      ANativeActivity *activity, void *saved_state, size_t saved_state_size);

#include "cartridge_api.h"

#define CARTRIDGE_TAG "CartridgeHost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CARTRIDGE_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CARTRIDGE_TAG, __VA_ARGS__)

/* Single host instance -- Cartridge only ever runs one MainActivity/one
 * emulation session per process, same reasoning as cartridge_spike.c. */
static ANativeActivity          s_activity;
static ANativeActivityCallbacks s_callbacks;
static struct android_app       *s_app     = NULL;
static int                       s_created = 0;

static char *cartridge_strdup(JNIEnv *env, jstring str)
{
   const char *chars;
   char *out;

   if (!str)
      return strdup("");

   chars = (*env)->GetStringUTFChars(env, str, NULL);
   out   = strdup(chars ? chars : "");
   if (chars)
      (*env)->ReleaseStringUTFChars(env, str, chars);
   return out;
}

/* Unlike every other function in this file, nativeCreate's `thiz` matters:
 * it becomes activity->clazz, which platform_unix.c resolves methods
 * against by reflection (getPowerstate, doVibrate, isAndroidTV, ...) --
 * see the "JNI-callable method surface" block on MainActivity. That's why
 * this one native method is declared directly on MainActivity rather than
 * on the stateless CartridgeNative object every other native method here
 * lives on. */
JNIEXPORT jboolean JNICALL
Java_com_retroarch_cartridge_MainActivity_nativeCreate(
      JNIEnv *env, jobject thiz, jobject asset_manager,
      jstring internal_data_path, jint sdk_version)
{
   JavaVM *vm = NULL;

   if (s_created)
   {
      LOGE("nativeCreate: already created, ignoring");
      return JNI_FALSE;
   }

   if ((*env)->GetJavaVM(env, &vm) != JNI_OK || !vm)
   {
      LOGE("nativeCreate: GetJavaVM failed");
      return JNI_FALSE;
   }

   memset(&s_activity, 0, sizeof(s_activity));
   memset(&s_callbacks, 0, sizeof(s_callbacks));

   s_activity.callbacks        = &s_callbacks;
   s_activity.vm               = vm;
   s_activity.env              = env;
   s_activity.clazz            = (*env)->NewGlobalRef(env, thiz);
   s_activity.assetManager     = AAssetManager_fromJava(env, asset_manager);
   s_activity.internalDataPath = cartridge_strdup(env, internal_data_path);
   s_activity.externalDataPath = cartridge_strdup(env, internal_data_path);
   s_activity.obbPath          = cartridge_strdup(env, internal_data_path);
   s_activity.sdkVersion       = sdk_version;

   LOGI("nativeCreate: calling cartridge_android_app_create");
   s_app = cartridge_android_app_create(&s_activity, NULL, 0);

   if (!s_app)
   {
      LOGE("nativeCreate: cartridge_android_app_create failed");
      (*env)->DeleteGlobalRef(env, s_activity.clazz);
      s_activity.clazz = NULL;
      return JNI_FALSE;
   }

   s_created = 1;
   return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeSurfaceCreated(
      JNIEnv *env, jobject thiz, jobject surface)
{
   ANativeWindow *window;

   if (!s_created || !s_callbacks.onNativeWindowCreated)
      return;

   window = ANativeWindow_fromSurface(env, surface);
   LOGI("nativeSurfaceCreated: window=%p", (void*)window);
   s_callbacks.onNativeWindowCreated(&s_activity, window);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeSurfaceDestroyed(
      JNIEnv *env, jobject thiz)
{
   if (!s_created || !s_callbacks.onNativeWindowDestroyed)
      return;

   LOGI("nativeSurfaceDestroyed");
   s_callbacks.onNativeWindowDestroyed(&s_activity, NULL);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeSurfaceChanged(
      JNIEnv *env, jobject thiz, jint left, jint top, jint right, jint bottom)
{
   ARect rect;

   if (!s_created || !s_callbacks.onContentRectChanged)
      return;

   rect.left   = left;
   rect.top    = top;
   rect.right  = right;
   rect.bottom = bottom;

   LOGI("nativeSurfaceChanged: %dx%d", right - left, bottom - top);
   s_callbacks.onContentRectChanged(&s_activity, &rect);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeStart(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onStart)
      s_callbacks.onStart(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeResume(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onResume)
      s_callbacks.onResume(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativePause(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onPause)
      s_callbacks.onPause(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeStop(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onStop)
      s_callbacks.onStop(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeDestroy(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onDestroy)
      s_callbacks.onDestroy(&s_activity);

   if (s_activity.clazz)
      (*env)->DeleteGlobalRef(env, s_activity.clazz);

   free((void*)s_activity.internalDataPath);
   free((void*)s_activity.externalDataPath);
   free((void*)s_activity.obbPath);

   memset(&s_activity, 0, sizeof(s_activity));
   memset(&s_callbacks, 0, sizeof(s_callbacks));
   s_app     = NULL;
   s_created = 0;
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeWindowFocusChanged(
      JNIEnv *env, jobject thiz, jboolean has_focus)
{
   if (s_created && s_callbacks.onWindowFocusChanged)
      s_callbacks.onWindowFocusChanged(&s_activity, has_focus);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeConfigurationChanged(
      JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onConfigurationChanged)
      s_callbacks.onConfigurationChanged(&s_activity);
}

/* Reserved hotkey table entry point (plan.md section 4.2: "menu toggle,
 * quick-menu summon"). A hardware combo doesn't need a JS round-trip, so
 * this goes straight through cartridge_api's thread-marshaled queue with no
 * callback -- fire and forget, same as any other cartridge_api_* caller. */
JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeToggleMenu(
      JNIEnv *env, jobject thiz)
{
   cartridge_api_toggle_paused(NULL, NULL);
}

#endif /* HAVE_CARTRIDGE */
