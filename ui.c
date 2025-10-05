#include "ui.h"
#include "slideshow.h"
#include "clock.h"
#include "media.h"

// UI state variables
Eina_Bool is_fullscreen = EINA_TRUE;
Eina_Bool controls_visible = EINA_FALSE;
Evas_Object *button_box = NULL;

// Slideshow widget variables (defined here since they're created in UI)
Evas_Object *slideshow_image = NULL;
Evas_Object *slideshow_video = NULL;
Evas_Object *letterbox_bg = NULL;

// Event handler functions
void
on_done(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   // Cleanup slideshow resources
   slideshow_cleanup();
   
   // Cleanup clock timer
   clock_cleanup();
   
   // Cleanup media files
   media_cleanup();
   
   // Reset global pointers
   button_box = NULL;
   
   // Called when the window is closed
   INF("Application shutdown requested");
   elm_exit();
}

void
on_button_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   INF("Slideshow toggle button clicked!");
   toggle_slideshow();
}

void
on_fullscreen_click(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Evas_Object *win = (Evas_Object *)data;
   
   is_fullscreen = !is_fullscreen;
   elm_win_fullscreen_set(win, is_fullscreen);
   
   if (is_fullscreen)
   {
      INF("Switched to fullscreen mode");
      printf("Switched to fullscreen mode\n");
   }
   else
   {
      // Set window size to 640x480 when switching to windowed mode
      evas_object_resize(win, 640, 480);
      INF("Switched to windowed mode (640x480)");
      printf("Switched to windowed mode (640x480)\n");
   }
}

void
on_next_image_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   show_next_media();
   INF("Manual next media");
   printf("Next media\n");
}

void
on_shuffle_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   toggle_shuffle_mode();
   
   // Update button text to reflect current mode
   Evas_Object *shuffle_btn = (Evas_Object *)data;
   if (shuffle_btn)
   {
      if (is_shuffle_mode)
         elm_object_text_set(shuffle_btn, "Shuffle: ON");
      else
         elm_object_text_set(shuffle_btn, "Shuffle: OFF");
   }
}

void
on_media_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   toggle_controls();
}

void
on_clock_toggle_click(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   toggle_clock();
   
   // Update button text
   if (clock_visible)
      elm_object_text_set(obj, "Clock: ON");
   else
      elm_object_text_set(obj, "Clock: OFF");
}

// Function to toggle control visibility
void
toggle_controls(void)
{
   controls_visible = !controls_visible;
   
   if (button_box)
   {
      if (controls_visible)
      {
         evas_object_show(button_box);
         INF("Controls shown");
      }
      else
      {
         evas_object_hide(button_box);
         INF("Controls hidden");
      }
   }
}

// UI creation and setup functions
Evas_Object*
ui_create_main_window(Evas_Object **win_bg_out)
{
   Evas_Object *win;
   
   // Set application policy
   elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

   // Create main window
   win = elm_win_util_standard_add("eslide", "eslide");
   evas_object_smart_callback_add(win, "delete,request", on_done, NULL);
   
   // Set window background to black to match letterboxing
   Evas_Object *win_bg = elm_bg_add(win);
   elm_bg_color_set(win_bg, 0, 0, 0);  // Black background
   evas_object_size_hint_weight_set(win_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, win_bg);
   evas_object_show(win_bg);
   
   // Return the background via output parameter
   if (win_bg_out)
      *win_bg_out = win_bg;
   
   INF("Main window created");
   
   return win;
}

void
ui_setup_media_display(Evas_Object *parent_box)
{
   // Create letterbox background container
   letterbox_bg = elm_bg_add(parent_box);
   printf("Created letterbox_bg: %p\n", letterbox_bg);
   elm_bg_color_set(letterbox_bg, 0, 0, 0);  // Black background
   evas_object_size_hint_weight_set(letterbox_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(letterbox_bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_smart_callback_add(letterbox_bg, "clicked", on_media_click, NULL);
   elm_box_pack_end(parent_box, letterbox_bg);
   evas_object_show(letterbox_bg);
   printf("Letterbox_bg shown\n");

   // Create image widget for slideshow with letterbox effect
   slideshow_image = elm_image_add(letterbox_bg);
   printf("Created slideshow_image: %p\n", slideshow_image);
   elm_image_aspect_fixed_set(slideshow_image, EINA_TRUE);  // Maintain aspect ratio
   elm_image_fill_outside_set(slideshow_image, EINA_FALSE);  // Don't fill outside bounds
   elm_image_resizable_set(slideshow_image, EINA_TRUE, EINA_TRUE);
   elm_image_smooth_set(slideshow_image, EINA_TRUE);
   evas_object_size_hint_weight_set(slideshow_image, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(slideshow_image, 0.5, 0.5);  // Center the image
   evas_object_smart_callback_add(slideshow_image, "clicked", on_media_click, NULL);
   
   // Set the image as the background's content
   elm_object_content_set(letterbox_bg, slideshow_image);
   printf("Set slideshow_image as letterbox content\n");
   evas_object_show(slideshow_image);
   printf("Slideshow_image shown\n");

   // Create video widget for slideshow (will be shown in the same letterbox container)
   slideshow_video = elm_video_add(letterbox_bg);
   elm_video_remember_position_set(slideshow_video, EINA_FALSE);
   evas_object_size_hint_weight_set(slideshow_video, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(slideshow_video, 0.5, 0.5);  // Center the video
   evas_object_smart_callback_add(slideshow_video, "clicked", on_media_click, NULL);
   evas_object_hide(slideshow_video);  // Initially hidden

   // Set up resize callback for letterbox to reposition clock
   evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_RESIZE, on_letterbox_resize, NULL);
   
   // Initialize slideshow with the created widgets
   slideshow_init(slideshow_image, slideshow_video, letterbox_bg);
}

void
ui_create_controls(Evas_Object *parent_box, Evas_Object *win)
{
   // Create horizontal box for control buttons
   button_box = elm_box_add(win);
   elm_box_horizontal_set(button_box, EINA_TRUE);
   elm_box_homogeneous_set(button_box, EINA_TRUE);
   evas_object_size_hint_weight_set(button_box, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(button_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(parent_box, button_box);
   evas_object_hide(button_box);  // Initially hidden

   // Create Toggle Slideshow button
   Evas_Object *toggle_btn = elm_button_add(win);
   elm_object_text_set(toggle_btn, "Toggle Slideshow");
   evas_object_smart_callback_add(toggle_btn, "clicked", on_button_click, NULL);
   evas_object_size_hint_weight_set(toggle_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(toggle_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, toggle_btn);
   evas_object_show(toggle_btn);

   // Create Next Image button
   Evas_Object *next_btn = elm_button_add(win);
   elm_object_text_set(next_btn, "Next Image");
   evas_object_smart_callback_add(next_btn, "clicked", on_next_image_click, NULL);
   evas_object_size_hint_weight_set(next_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(next_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, next_btn);
   evas_object_show(next_btn);

   // Create Shuffle Toggle button
   Evas_Object *shuffle_btn = elm_button_add(win);
   if (is_shuffle_mode)
      elm_object_text_set(shuffle_btn, "Shuffle: ON");
   else
      elm_object_text_set(shuffle_btn, "Shuffle: OFF");
   evas_object_smart_callback_add(shuffle_btn, "clicked", on_shuffle_click, shuffle_btn);
   evas_object_size_hint_weight_set(shuffle_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(shuffle_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, shuffle_btn);
   evas_object_show(shuffle_btn);

   // Create Clock Toggle button
   Evas_Object *clock_btn = elm_button_add(win);
   if (clock_visible)
      elm_object_text_set(clock_btn, "Clock: ON");
   else
      elm_object_text_set(clock_btn, "Clock: OFF");
   evas_object_smart_callback_add(clock_btn, "clicked", on_clock_toggle_click, clock_btn);
   evas_object_size_hint_weight_set(clock_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(clock_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, clock_btn);
   evas_object_show(clock_btn);

   // Create Fullscreen button
   Evas_Object *fullscreen_btn = elm_button_add(win);
   elm_object_text_set(fullscreen_btn, "Fullscreen");
   evas_object_smart_callback_add(fullscreen_btn, "clicked", on_fullscreen_click, win);
   evas_object_size_hint_weight_set(fullscreen_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(fullscreen_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, fullscreen_btn);
   evas_object_show(fullscreen_btn);
}

// UI initialization and cleanup
void
ui_init(void)
{
   // UI initialization is handled by the individual creation functions
}

void
ui_cleanup(void)
{
   // Reset global pointers
   button_box = NULL;
}