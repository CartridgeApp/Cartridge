/*  Cartridge M0 -- library-mode spike.
 *
 *  This is the throwaway JNI shim described in
 *  ai-specs/2026-07-09-react-native-interface/plan.md, M0: it proves that
 *  RetroArch's runloop can be driven from a plain Kotlin Activity + SurfaceView
 *  instead of android.app.NativeActivity.
 *
 *  The trick: android.app.NativeActivity is not special to RetroArch's C code
 *  at all -- ANativeActivity_onCreate() (frontend/drivers/platform_unix.c)
 *  just expects *someone* to hand it a populated `ANativeActivity` struct and
 *  then forward lifecycle/window/input events to the callback table it fills
 *  in. Normally the Android runtime's NativeActivity bridge is that someone.
 *  Here, we are: we fabricate the struct ourselves from a plain Activity's
 *  JNI objects and call cartridge_android_app_create() (the
 *  HAVE_CARTRIDGE-guarded twin of ANativeActivity_onCreate added to
 *  platform_unix.c) directly, then invoke the resulting callbacks by hand
 *  from CartridgeSpikeActivity's lifecycle and SurfaceHolder callbacks.
 *
 *  Compiled directly into libretroarch-activity.so (see
 *  pkg/android/phoenix-common/jni/Android.mk, HAVE_CARTRIDGE), so it links
 *  against RetroArch's real internals and calling convention for free.
 */
#ifdef HAVE_CARTRIDGE

#include <string.h>
#include <stdlib.h>

#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

/* Deliberately not including platform_unix.h: it holds tentative (non-extern)
 * global definitions that are only safe in RetroArch's single-TU griffin
 * unity build. We only need the opaque android_app pointer and the one
 * HAVE_CARTRIDGE-guarded entry point it declares, so redeclare that
 * prototype locally instead of pulling the whole header into a second
 * translation unit. */
struct android_app;
extern struct android_app *cartridge_android_app_create(
      ANativeActivity *activity, void *saved_state, size_t saved_state_size);

#define CARTRIDGE_TAG "CartridgeSpike"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CARTRIDGE_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CARTRIDGE_TAG, __VA_ARGS__)

/* Single spike instance -- CartridgeSpikeActivity is not meant to be
 * multi-instanced, so a static is fine (mirrors g_android's own use of a
 * single global android_app). */
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

JNIEXPORT jboolean JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeCreate(
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
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeSurfaceCreated(
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
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeSurfaceDestroyed(
      JNIEnv *env, jobject thiz)
{
   if (!s_created || !s_callbacks.onNativeWindowDestroyed)
      return;

   LOGI("nativeSurfaceDestroyed");
   /* android_app_set_window(..., NULL) is what actually matters here --
    * platform_unix.c's onNativeWindowDestroyed ignores the window argument
    * and just tears down the pending window, so passing NULL is correct. */
   s_callbacks.onNativeWindowDestroyed(&s_activity, NULL);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeSurfaceChanged(
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
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeStart(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onStart)
      s_callbacks.onStart(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeResume(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onResume)
      s_callbacks.onResume(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativePause(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onPause)
      s_callbacks.onPause(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeStop(JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onStop)
      s_callbacks.onStop(&s_activity);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeDestroy(JNIEnv *env, jobject thiz)
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
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeWindowFocusChanged(
      JNIEnv *env, jobject thiz, jboolean has_focus)
{
   if (s_created && s_callbacks.onWindowFocusChanged)
      s_callbacks.onWindowFocusChanged(&s_activity, has_focus);
}

JNIEXPORT void JNICALL
Java_com_retroarch_cartridge_CartridgeSpikeActivity_nativeConfigurationChanged(
      JNIEnv *env, jobject thiz)
{
   if (s_created && s_callbacks.onConfigurationChanged)
      s_callbacks.onConfigurationChanged(&s_activity);
}

#endif /* HAVE_CARTRIDGE */
