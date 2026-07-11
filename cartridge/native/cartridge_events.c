/*  Cartridge M1 -- native-to-JS event sink (implementation). */
#ifdef HAVE_CARTRIDGE

#include <pthread.h>

#include "cartridge_events.h"

static cartridge_event_cb s_sink      = NULL;
static void              *s_user_data = NULL;
static pthread_mutex_t    s_lock      = PTHREAD_MUTEX_INITIALIZER;

void cartridge_events_set_sink(cartridge_event_cb cb, void *user_data)
{
   pthread_mutex_lock(&s_lock);
   s_sink      = cb;
   s_user_data = user_data;
   pthread_mutex_unlock(&s_lock);
}

void cartridge_events_notify_message(const char *msg)
{
   cartridge_event_cb cb;
   void               *user_data;

   pthread_mutex_lock(&s_lock);
   cb        = s_sink;
   user_data = s_user_data;
   pthread_mutex_unlock(&s_lock);

   if (cb && msg)
      cb("message", msg, user_data);
}

#endif /* HAVE_CARTRIDGE */
