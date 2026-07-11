/*  Cartridge M1 -- stock-driver input forwarding (JNI shim).
 *
 *  See cartridge_input.h for why this exists. Bound to CartridgeNative, a
 *  Kotlin singleton object callable from both MainActivity
 *  (dispatchKeyEvent/dispatchGenericMotionEvent, after the reserved hotkey
 *  table has had first refusal) and anywhere else that needs it.
 *
 *  AKeyEvent_fromJava()/AMotionEvent_fromJava()/AInputEvent_release() are
 *  NDK API 31+ only -- this whole file compiles to nothing below that
 *  (Cartridge's own ndk-build invocation targets APP_PLATFORM=android-31;
 *  see cartridge/android). The Kotlin side additionally guards the call
 *  sites on Build.VERSION.SDK_INT as a defensive backstop.
 */
#ifdef HAVE_CARTRIDGE

#include <android/api-level.h>

#if __ANDROID_API__ >= 31

#include <jni.h>
#include <android/input.h>

#include "cartridge_input.h"

JNIEXPORT jboolean JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeInjectKeyEvent(
      JNIEnv *env, jobject thiz, jobject key_event)
{
   /* AKeyEvent_fromJava() returns const AInputEvent* in the NDK headers;
    * android_input.c's existing dispatch functions (unrelated to Cartridge,
    * predating it) all take non-const AInputEvent*, so this cast matches
    * that file's own convention. We exclusively own this event until
    * AInputEvent_release() below, so mutation isn't a real concern here. */
   AInputEvent *event = (AInputEvent*)AKeyEvent_fromJava(env, key_event);

   if (!event)
      return JNI_FALSE;

   cartridge_input_inject_event(event);
   AInputEvent_release(event);
   return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_retroarch_cartridge_CartridgeNative_nativeInjectMotionEvent(
      JNIEnv *env, jobject thiz, jobject motion_event)
{
   AInputEvent *event = (AInputEvent*)AMotionEvent_fromJava(env, motion_event);

   if (!event)
      return JNI_FALSE;

   cartridge_input_inject_event(event);
   AInputEvent_release(event);
   return JNI_TRUE;
}

#endif /* __ANDROID_API__ >= 31 */

#endif /* HAVE_CARTRIDGE */
