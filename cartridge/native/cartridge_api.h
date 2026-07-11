/*  Cartridge M1 -- imperative FFI surface.
 *
 *  The only header the TurboModule's C++ glue (cartridge/native/turbomodule/)
 *  is allowed to include of RetroArch internals -- everything on the other
 *  side of these functions (command_event(), task_push_load_content_from_cli(),
 *  RetroArch's task queue) is called from cartridge_api.c only, which is
 *  compiled straight into libretroarch-activity.so and therefore sees the
 *  real headers/ABI directly (see ai-specs/2026-07-09-react-native-interface/
 *  plan.md section 4.3).
 *
 *  Threading: RetroArch's global state assumes single-threaded ownership by
 *  the emulation thread (see plan.md section 7 risks). Every call below may
 *  be made from any thread (in practice, the JS/TurboModule thread); the
 *  implementation enqueues the work and actually touches RetroArch internals
 *  only from cartridge_api_poll(), which cartridge/native's guarded hook
 *  calls once per iteration of runloop_iterate() on the emulation thread.
 */
#ifndef CARTRIDGE_API_H
#define CARTRIDGE_API_H

#include <stddef.h>

#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* Result callback for the async calls below. Invoked from the emulation
 * thread (inside cartridge_api_poll()), never from the calling thread --
 * callers that need to touch JS/UI state must hop back to their own thread
 * themselves (the TurboModule glue does this via its JS invoker). */
typedef void (*cartridge_result_cb)(bool success, const char *detail,
      void *user_data);

/* Loads `content_path` with the core at `core_path`. Either string may be
 * NULL/empty to mean "keep the currently loaded core/content" is NOT
 * supported here -- both are required, mirroring
 * task_push_load_content_from_cli()'s CLI-equivalent contract. */
void cartridge_api_load_content(const char *core_path,
      const char *content_path, cartridge_result_cb cb, void *user_data);

/* Pauses or unpauses the running content (CMD_EVENT_PAUSE/UNPAUSE). */
void cartridge_api_set_paused(bool paused, cartridge_result_cb cb,
      void *user_data);

/* Toggles pause (CMD_EVENT_PAUSE_TOGGLE). Used by the reserved hotkey table
 * (plan.md section 4.2) -- a hardware menu-toggle combo doesn't need a JS
 * round-trip, so cb/user_data may both be NULL. */
void cartridge_api_toggle_paused(cartridge_result_cb cb, void *user_data);

/* Takes a screenshot into the configured screenshot directory
 * (CMD_EVENT_TAKE_SCREENSHOT). `detail` on success is that directory --
 * resolving the exact generated filename is left to a later milestone
 * (M3 ships save-state thumbnail capture, which needs it properly). */
void cartridge_api_take_screenshot(cartridge_result_cb cb, void *user_data);

/* Must be called once per iteration of runloop_iterate(), on the emulation
 * thread -- drains whatever cartridge_api_* calls have queued up since the
 * last poll and actually performs them. See the HAVE_CARTRIDGE hook at the
 * top of runloop_iterate() in runloop.c. */
void cartridge_api_poll(void);

RETRO_END_DECLS

#endif /* CARTRIDGE_API_H */
