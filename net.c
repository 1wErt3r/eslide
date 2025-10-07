#include <Ecore_Con.h>
#include "net.h"
#include "slideshow.h"  // for extern letterbox_bg

// Simple async HTTP fetch using Ecore_Con_Url
Evas_Object *net_label = NULL;
static Ecore_Con_Url *net_url = NULL;
static Eina_Strbuf *net_buf = NULL;
static Ecore_Event_Handler *hdl_data = NULL;
static Ecore_Event_Handler *hdl_complete = NULL;
static Ecore_Timer *net_timer = NULL;
static char *net_station = NULL; // optional station/city for wttr.in

static void
_net_position_label(void)
{
   if (!net_label || !letterbox_bg) return;
   int x, y, w, h;
   evas_object_geometry_get(letterbox_bg, &x, &y, &w, &h);
   // Top-left with padding
   evas_object_move(net_label, x + 20, y + 20);
}

static void
_net_set_text(const char *txt)
{
   if (!net_label || !txt) return;
   char buf[1024];
   // Keep formatting minimal and readable
   snprintf(buf, sizeof(buf), "<font=Open Sans:style=Light><font_size=24><color=#FFFFFF>%s</color></font_size></font>", txt);
   elm_object_text_set(net_label, buf);
   _net_position_label();
   evas_object_show(net_label);
}

static Eina_Bool
_url_data_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Con_Event_Url_Data *e = event;
   if (!e) return ECORE_CALLBACK_PASS_ON;
   
   INF("Received data chunk: %d bytes", e->size);
   
   if (!net_buf) net_buf = eina_strbuf_new();
   eina_strbuf_append_length(net_buf, (const char *)e->data, e->size);
   
   INF("Total response size now: %zu bytes", eina_strbuf_length_get(net_buf));
   
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_url_complete_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Con_Event_Url_Complete *e = event;
   if (!e) return ECORE_CALLBACK_PASS_ON;

   INF("HTTP request completed");

   // Basic status handling
   if (e->status == 200 && net_buf)
   {
      const char *msg = eina_strbuf_string_get(net_buf);
      if (msg && *msg)
      {
         INF("Received response: '%.100s%s'", msg, 
             eina_strbuf_length_get(net_buf) > 100 ? "..." : "");
         _net_set_text(msg);
         INF("Updated net_label with new text");
      }
      else
      {
         WRN("No data received from HTTP request");
         _net_set_text("(empty response)");
      }
   }
   else
   {
      char err[128];
      snprintf(err, sizeof(err), "Network error: HTTP %d", e->status);
      _net_set_text(err);
   }

   // Cleanup URL object for this request
   if (net_url)
   {
      ecore_con_url_free(net_url);
      net_url = NULL;
      INF("Cleaned up URL object - ready for next request");
   }
   if (hdl_data)
   {
      ecore_event_handler_del(hdl_data);
      hdl_data = NULL;
   }
   if (hdl_complete)
   {
      ecore_event_handler_del(hdl_complete);
      hdl_complete = NULL;
   }

   if (net_buf)
   {
      eina_strbuf_free(net_buf);
      net_buf = NULL;
   }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_net_on_letterbox_resize(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _net_position_label();
}

void
net_init(Evas_Object *parent_window)
{
   // Create overlay label (initially hidden)
   net_label = elm_label_add(parent_window);
   elm_object_text_set(net_label, "");
   evas_object_size_hint_weight_set(net_label, 0.0, 0.0);
   evas_object_size_hint_align_set(net_label, 0.0, 0.0);
   evas_object_resize(net_label, 800, 40);
   elm_label_ellipsis_set(net_label, EINA_TRUE);
   evas_object_layer_set(net_label, 1000);
   evas_object_hide(net_label);

   // Keep it positioned relative to letterbox
   if (letterbox_bg)
      evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_RESIZE, _net_on_letterbox_resize, NULL);
}

void
net_fetch_start(void)
{
   INF("net_fetch_start() called");
   
   // Initialize URL subsystem once
   static Eina_Bool inited = EINA_FALSE;
   // Avoid overlapping requests
   if (net_url) {
      INF("Skipping fetch - request already in progress");
      return;
   }
   if (!inited)
   {
      if (!ecore_con_url_init())
      {
         _net_set_text("Failed to init network");
         return;
      }
      inited = EINA_TRUE;
   }

   // Build wttr.in endpoint; use configured station if provided
   char url_buf[256];
   if (net_station && *net_station)
   {
      // Basic encoding: replace spaces with '+' for path segment
      Eina_Strbuf *tmp = eina_strbuf_new();
      for (const char *p = net_station; p && *p; ++p)
      {
         if (*p == ' ')
            eina_strbuf_append_char(tmp, '+');
         else
            eina_strbuf_append_char(tmp, *p);
      }
      snprintf(url_buf, sizeof(url_buf), "https://wttr.in/%s?format=1", eina_strbuf_string_get(tmp));
      eina_strbuf_free(tmp);
   }
   else
   {
      snprintf(url_buf, sizeof(url_buf), "https://wttr.in/?format=1");
   }

   INF("Creating new HTTP request to %s", url_buf);
   // Create URL object and set a User-Agent
   net_url = ecore_con_url_new(url_buf);
   if (!net_url)
   {
      ERR("Failed to create URL object");
      _net_set_text("Failed to create URL object");
      return;
   }
   ecore_con_url_additional_header_add(net_url, "User-Agent", "eslide/1.0");

   // Register event handlers for data and completion
   hdl_data = ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _url_data_cb, NULL);
   hdl_complete = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _url_complete_cb, NULL);

   // Start GET request (async)
   INF("Starting HTTP GET request");
   if (!ecore_con_url_get(net_url))
   {
      ERR("Failed to start HTTP GET request");
      _net_set_text("Failed to start request");
   }
   else
   {
      INF("HTTP GET request started successfully");
   }
}

static Eina_Bool
_net_timer_cb(void *data EINA_UNUSED)
{
    static int timer_count = 0;
    timer_count++;
    INF("Timer callback triggered #%d - attempting to refresh network message", timer_count);
    // Print to stdout so users can verify periodic updates
    printf("Auto refresh: starting weather request #%d\n", timer_count);
    net_fetch_start();
    return ECORE_CALLBACK_RENEW;
}

void
net_refresh_start(double interval_seconds)
{
   INF("Starting network refresh timer with %.1f second interval", interval_seconds);
   
   if (net_timer) {
      INF("Deleting existing timer");
      ecore_timer_del(net_timer);
   }
   if (interval_seconds <= 0.0) interval_seconds = 60.0;
   net_timer = ecore_timer_add(interval_seconds, _net_timer_cb, NULL);
   if (net_timer) {
      INF("Timer created successfully");
   } else {
      ERR("Failed to create refresh timer");
   }
}

void
net_refresh_stop(void)
{
   if (net_timer)
   {
      INF("Stopping network refresh timer");
      ecore_timer_del(net_timer);
      net_timer = NULL;
   }
   if (net_label)
   {
      evas_object_hide(net_label);
   }
}

void
net_cleanup(void)
{
   if (net_url)
   {
      ecore_con_url_free(net_url);
      net_url = NULL;
   }
   if (hdl_data)
   {
      ecore_event_handler_del(hdl_data);
      hdl_data = NULL;
   }
   if (hdl_complete)
   {
      ecore_event_handler_del(hdl_complete);
      hdl_complete = NULL;
   }
   if (net_buf)
   {
      eina_strbuf_free(net_buf);
      net_buf = NULL;
   }
   if (letterbox_bg)
   {
      evas_object_event_callback_del(letterbox_bg, EVAS_CALLBACK_RESIZE, _net_on_letterbox_resize);
   }
   if (net_timer)
   {
      ecore_timer_del(net_timer);
      net_timer = NULL;
   }
   ecore_con_url_shutdown();
   net_label = NULL;
   if (net_station) { free(net_station); net_station = NULL; }
}

void
net_set_station(const char *station_id)
{
   if (net_station)
   {
      free(net_station);
      net_station = NULL;
   }
   if (station_id && *station_id)
   {
      net_station = strdup(station_id);
      INF("Weather station/location set to '%s'", net_station);
   }
}

const char*
net_get_station(void)
{
   return net_station;
}