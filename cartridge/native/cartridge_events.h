/*  Cartridge M1 -- native-to-JS event sink.
 *
 *  A single chokepoint (plan.md section 4.3: "a single cartridge_events.c
 *  sink"). For M1 there is exactly one hook, at runloop_msg_queue_push()
 *  (runloop.c) -- RetroArch's own notification-message pipe, already used
 *  for "Paused", "Screenshot saved to ...", core/content load errors, etc.
 *  Later milestones add more hooks (task progress, achievements) that all
 *  funnel through the same cartridge_events_set_sink() registration.
 */
#ifndef CARTRIDGE_EVENTS_H
#define CARTRIDGE_EVENTS_H

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* `name` is a short stable event identifier (e.g. "message"); `detail` is a
 * plain string payload. Invoked on whatever thread produced the event --
 * the TurboModule's event emitter is responsible for hopping to the JS
 * thread itself. */
typedef void (*cartridge_event_cb)(const char *name, const char *detail,
      void *user_data);

/* Registers the (single) JS-side listener. Passing cb == NULL unregisters.
 * Only the TurboModule's C++ glue calls this, once, at module init/teardown. */
void cartridge_events_set_sink(cartridge_event_cb cb, void *user_data);

/* Called from the HAVE_CARTRIDGE hook in runloop_msg_queue_push(). */
void cartridge_events_notify_message(const char *msg);

RETRO_END_DECLS

#endif /* CARTRIDGE_EVENTS_H */
