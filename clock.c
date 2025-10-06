#include "clock.h"

// Clock state variables
Evas_Object *clock_label = NULL;
Ecore_Timer *clock_timer = NULL;
Eina_Bool clock_visible = EINA_FALSE;  // Clock hidden by default
Eina_Bool clock_is_24h = EINA_FALSE;   // Default to 12-hour time

// Timer callback for digital clock updates
Eina_Bool
clock_timer_cb(void *data EINA_UNUSED)
{
   time_t current_time;
   struct tm *time_info;
   char time_string[32];   // "HH:MM" or "H:MM AM"
   char date_string[64];   // e.g., "Saturday, October 5"
   char formatted_text[256];  // Buffer for HTML formatted date + time

   // Get current time
   time(&current_time);
   time_info = localtime(&current_time);

   // Format time based on selected mode
   if (clock_is_24h)
      strftime(time_string, sizeof(time_string), "%H:%M", time_info);
   else
      // Use %-I to drop leading zero in 12-hour format (e.g., 01 -> 1)
      strftime(time_string, sizeof(time_string), "%-I:%M %p", time_info);

   // Format date line (long weekday, month name, day of month)
   // Use %-d to drop leading zero in day of month
   strftime(date_string, sizeof(date_string), "%A, %B %-d", time_info);

   // Create HTML formatted string with Open Sans Light and white color
   // Date on a line above time; time larger than date
   // If Open Sans is unavailable, fontconfig will fallback to a default sans
   snprintf(formatted_text, sizeof(formatted_text),
            "<font=Open Sans:style=Light><color=#FFFFFF>"
            "<font_size=32>%s</font_size><br>"
            "<font_size=96>%s</font_size>"
            "</color></font>",
            date_string, time_string);

   // Update clock label
   if (clock_label)
   {
      elm_object_text_set(clock_label, formatted_text);
   }

   return ECORE_CALLBACK_RENEW;  // Keep the timer running
}

// Callback to reposition clock when letterbox is resized
void
on_letterbox_resize(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (!clock_label) return;
   
   int x, y, w, h;
   evas_object_geometry_get(obj, &x, &y, &w, &h);

   // Position clock in lower-right based on its current size, with 20px padding
   int cw = 0, ch = 0;
   evas_object_geometry_get(clock_label, NULL, NULL, &cw, &ch);
   if (cw <= 0) cw = 520; // fallback to default size if not yet realized
   if (ch <= 0) ch = 180;
   evas_object_move(clock_label, x + w - cw - 20, y + h - ch - 20);
}

// Toggle clock visibility
void
toggle_clock(void)
{
   if (!clock_label) return;
   
   clock_visible = !clock_visible;
   
   if (clock_visible)
   {
      evas_object_show(clock_label);
      INF("Clock shown");
   }
   else
   {
      evas_object_hide(clock_label);
      INF("Clock hidden");
   }
}

// Clock initialization
void
clock_init(Evas_Object *parent_window)
{
   // Create digital clock label as an overlay on the letterbox
   clock_label = elm_label_add(parent_window);  // Add to window instead of letterbox for proper layering
   // Placeholder two-line layout: date above time (larger)
   elm_object_text_set(clock_label,
                       "<font=Open Sans:style=Light><color=#FFFFFF>"
                       "<font_size=32>Monday, January 1</font_size><br>"
                       "<font_size=96>1:00 AM</font_size>"
                       "</color></font>");
   evas_object_size_hint_weight_set(clock_label, 0.0, 0.0);
   evas_object_size_hint_align_set(clock_label, 1.0, 1.0);  // Align to bottom right
   
   // Set size for the clock: room for two lines and larger time
   evas_object_resize(clock_label, 520, 180);
   evas_object_layer_set(clock_label, 1000);  // Ensure it's on top
   
   // Hide clock by default (will be shown when toggled)
   if (clock_visible)
      evas_object_show(clock_label);
   else
      evas_object_hide(clock_label);
}

// Set clock format (12-hour or 24-hour)
void
clock_set_24h(Eina_Bool use_24h)
{
   clock_is_24h = use_24h;
   // Update immediately if timer is running
   if (clock_label)
   {
      clock_timer_cb(NULL);
   }
}

// Start clock timer
void
clock_start(void)
{
   // Initialize and start the digital clock timer
   clock_timer_cb(NULL);  // Update clock immediately
   clock_timer = ecore_timer_add(1.0, clock_timer_cb, NULL);  // Update every second
   INF("Digital clock timer started");
}

// Clock cleanup
void
clock_cleanup(void)
{
   // Cleanup clock timer
   if (clock_timer)
   {
      ecore_timer_del(clock_timer);
      clock_timer = NULL;
   }
   
   // Reset global pointer
   clock_label = NULL;
}