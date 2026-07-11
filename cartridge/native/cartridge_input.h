/*  Cartridge M1 -- stock-driver input forwarding.
 *
 *  RN's <RetroArchSurface/> is an ordinary View inside a plain ReactActivity,
 *  not an android.app.NativeActivity window -- the OS only ever creates a
 *  real AInputQueue for the latter (that plumbing lives inside
 *  NativeActivity's own Java implementation, not anything
 *  cartridge_android_app_create() fabricates). android_input.c
 *  (input/drivers/android_input.c) is written entirely against
 *  android_app->inputQueue, so without one it never sees game input.
 *
 *  The fix is a guarded addition in android_input.c itself
 *  (cartridge_input_inject_event(), HAVE_CARTRIDGE): it runs an
 *  externally-sourced AInputEvent through the exact same per-event dispatch
 *  as a queue-sourced one. This header is what feeds it -- see
 *  cartridge_input.c for the JNI entry points that convert a Java
 *  KeyEvent/MotionEvent (from the Kotlin host's dispatchKeyEvent /
 *  dispatchGenericMotionEvent, after the small reserved hotkey table has
 *  had first refusal) into an AInputEvent via AKeyEvent_fromJava() /
 *  AMotionEvent_fromJava() (NDK API 31+).
 */
#ifndef CARTRIDGE_INPUT_H
#define CARTRIDGE_INPUT_H

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

struct AInputEvent;

/* Defined in input/drivers/android_input.c. `event` must not be queue-owned
 * (no AInputQueue_finishEvent is called) -- callers own its lifetime. */
void cartridge_input_inject_event(struct AInputEvent *event);

RETRO_END_DECLS

#endif /* CARTRIDGE_INPUT_H */
