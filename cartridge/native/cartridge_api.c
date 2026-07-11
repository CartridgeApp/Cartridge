/*  Cartridge M1 -- imperative FFI surface (implementation).
 *
 *  See cartridge_api.h for the threading contract. This file is compiled
 *  straight into libretroarch-activity.so (cartridge/android/jni/Android.mk),
 *  so it sees RetroArch's real headers/ABI -- if upstream renames
 *  command_event() or changes content_ctx_info_t, this fails to compile
 *  (plan.md section 4.1, rule 3: drift detection is compile-time).
 */
#ifdef HAVE_CARTRIDGE

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "cartridge_api.h"

#include "../../command.h"
#include "../../tasks/task_content.h"
#include "../../configuration.h"

enum cartridge_op
{
   CARTRIDGE_OP_LOAD_CONTENT = 0,
   CARTRIDGE_OP_SET_PAUSED,
   CARTRIDGE_OP_TOGGLE_PAUSED,
   CARTRIDGE_OP_TAKE_SCREENSHOT
};

/* Deliberately small and fixed-size: these are user-initiated, low-frequency
 * commands (button presses), not a hot path. A full queue means the caller
 * is issuing commands faster than once per frame, which is a caller bug --
 * cartridge_api_poll() drains everything queued each time it runs, so the
 * queue only ever holds "since the last frame". */
#define CARTRIDGE_QUEUE_CAPACITY 16

struct cartridge_work_item
{
   enum cartridge_op   op;
   char               *core_path;
   char               *content_path;
   bool                paused;
   cartridge_result_cb cb;
   void               *user_data;
};

static struct cartridge_work_item s_queue[CARTRIDGE_QUEUE_CAPACITY];
static size_t                     s_queue_head  = 0;
static size_t                     s_queue_count = 0;
static pthread_mutex_t            s_queue_lock  = PTHREAD_MUTEX_INITIALIZER;

static bool cartridge_enqueue(const struct cartridge_work_item *item)
{
   bool ok = false;

   pthread_mutex_lock(&s_queue_lock);
   if (s_queue_count < CARTRIDGE_QUEUE_CAPACITY)
   {
      size_t tail       = (s_queue_head + s_queue_count) % CARTRIDGE_QUEUE_CAPACITY;
      s_queue[tail]     = *item;
      s_queue_count++;
      ok                = true;
   }
   pthread_mutex_unlock(&s_queue_lock);

   return ok;
}

static bool cartridge_dequeue(struct cartridge_work_item *out)
{
   bool ok = false;

   pthread_mutex_lock(&s_queue_lock);
   if (s_queue_count > 0)
   {
      *out          = s_queue[s_queue_head];
      s_queue_head  = (s_queue_head + 1) % CARTRIDGE_QUEUE_CAPACITY;
      s_queue_count--;
      ok            = true;
   }
   pthread_mutex_unlock(&s_queue_lock);

   return ok;
}

void cartridge_api_load_content(const char *core_path,
      const char *content_path, cartridge_result_cb cb, void *user_data)
{
   struct cartridge_work_item item;

   memset(&item, 0, sizeof(item));
   item.op           = CARTRIDGE_OP_LOAD_CONTENT;
   item.core_path    = core_path    ? strdup(core_path)    : strdup("");
   item.content_path = content_path ? strdup(content_path) : strdup("");
   item.cb           = cb;
   item.user_data    = user_data;

   if (!cartridge_enqueue(&item))
   {
      free(item.core_path);
      free(item.content_path);
      if (cb)
         cb(false, "cartridge command queue full", user_data);
   }
}

void cartridge_api_set_paused(bool paused, cartridge_result_cb cb,
      void *user_data)
{
   struct cartridge_work_item item;

   memset(&item, 0, sizeof(item));
   item.op        = CARTRIDGE_OP_SET_PAUSED;
   item.paused    = paused;
   item.cb        = cb;
   item.user_data = user_data;

   if (!cartridge_enqueue(&item) && cb)
      cb(false, "cartridge command queue full", user_data);
}

void cartridge_api_toggle_paused(cartridge_result_cb cb, void *user_data)
{
   struct cartridge_work_item item;

   memset(&item, 0, sizeof(item));
   item.op        = CARTRIDGE_OP_TOGGLE_PAUSED;
   item.cb        = cb;
   item.user_data = user_data;

   if (!cartridge_enqueue(&item) && cb)
      cb(false, "cartridge command queue full", user_data);
}

void cartridge_api_take_screenshot(cartridge_result_cb cb, void *user_data)
{
   struct cartridge_work_item item;

   memset(&item, 0, sizeof(item));
   item.op        = CARTRIDGE_OP_TAKE_SCREENSHOT;
   item.cb        = cb;
   item.user_data = user_data;

   if (!cartridge_enqueue(&item) && cb)
      cb(false, "cartridge command queue full", user_data);
}

static void cartridge_process_load_content(struct cartridge_work_item *item)
{
   content_ctx_info_t content_info;
   bool               ok;

   memset(&content_info, 0, sizeof(content_info));

   ok = task_push_load_content_from_cli(
         item->core_path, item->content_path,
         &content_info, CORE_TYPE_PLAIN, NULL, NULL);

   if (item->cb)
      item->cb(ok, ok ? item->content_path : "failed to load content",
            item->user_data);

   free(item->core_path);
   free(item->content_path);
}

static void cartridge_process_set_paused(struct cartridge_work_item *item)
{
   bool ok = command_event(
         item->paused ? CMD_EVENT_PAUSE : CMD_EVENT_UNPAUSE, NULL);

   if (item->cb)
      item->cb(ok, item->paused ? "1" : "0", item->user_data);
}

static void cartridge_process_toggle_paused(struct cartridge_work_item *item)
{
   bool ok = command_event(CMD_EVENT_PAUSE_TOGGLE, NULL);

   if (item->cb)
      item->cb(ok, "", item->user_data);
}

static void cartridge_process_take_screenshot(struct cartridge_work_item *item)
{
   bool ok = command_event(CMD_EVENT_TAKE_SCREENSHOT, NULL);

   if (item->cb)
   {
      settings_t *settings = config_get_ptr();
      item->cb(ok,
            (ok && settings) ? settings->paths.directory_screenshot : "",
            item->user_data);
   }
}

void cartridge_api_poll(void)
{
   struct cartridge_work_item item;

   while (cartridge_dequeue(&item))
   {
      switch (item.op)
      {
         case CARTRIDGE_OP_LOAD_CONTENT:
            cartridge_process_load_content(&item);
            break;
         case CARTRIDGE_OP_SET_PAUSED:
            cartridge_process_set_paused(&item);
            break;
         case CARTRIDGE_OP_TOGGLE_PAUSED:
            cartridge_process_toggle_paused(&item);
            break;
         case CARTRIDGE_OP_TAKE_SCREENSHOT:
            cartridge_process_take_screenshot(&item);
            break;
      }
   }
}

#endif /* HAVE_CARTRIDGE */
