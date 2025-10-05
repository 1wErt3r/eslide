#include "clock.h"

// Clock state variables
Evas_Object *clock_label = NULL;
Ecore_Timer *clock_timer = NULL;
Eina_Bool clock_visible = EINA_FALSE;  // Clock hidden by default

// Timer callback for digital clock updates
Eina_Bool
clock_timer_cb(void *data EINA_UNUSED)
{
   time_t current_time;
   struct tm *time_info;
   char time_string[6];  // HH:MM format + null terminator
   char formatted_time[100];  // Buffer for HTML formatted time
   
   // Get current time
   time(&current_time);
   time_info = localtime(&current_time);
   
   // Format time as HH:MM
   strftime(time_string, sizeof(time_string), "%H:%M", time_info);
   
   // Create HTML formatted string with larger font and white color
   snprintf(formatted_time, sizeof(formatted_time), 
            "<font_size=48><color=#FFFFFF><b>%s</b></color></font_size>", time_string);
   
   // Update clock label
   if (clock_label)
   {
      elm_object_text_set(clock_label, formatted_time);
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
   
   // Position clock in lower right corner with padding, moved further left for larger container
   evas_object_move(clock_label, x + w - 180, y + h - 60);
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
   elm_object_text_set(clock_label, "<font_size=72><color=#FFFFFF><b>00:00</b></color></font_size>");
   evas_object_size_hint_weight_set(clock_label, 0.0, 0.0);
   evas_object_size_hint_align_set(clock_label, 1.0, 1.0);  // Align to bottom right
   
   // Set size for the clock (increased further for larger 72pt font)
   evas_object_resize(clock_label, 180, 90);
   evas_object_layer_set(clock_label, 1000);  // Ensure it's on top
   
   // Hide clock by default (will be shown when toggled)
   if (clock_visible)
      evas_object_show(clock_label);
   else
      evas_object_hide(clock_label);
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