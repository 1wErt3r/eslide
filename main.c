#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <Elementary.h>
#include <Emotion.h>
#include <Eina.h>
#include <Ecore.h>

// Logging domain for our application
static int _log_domain = -1;

// Logging macros for our application
#define CRITICAL(...) EINA_LOG_DOM_CRIT(_log_domain, __VA_ARGS__)
#define ERR(...)      EINA_LOG_DOM_ERR(_log_domain, __VA_ARGS__)
#define WRN(...)      EINA_LOG_DOM_WARN(_log_domain, __VA_ARGS__)
#define INF(...)      EINA_LOG_DOM_INFO(_log_domain, __VA_ARGS__)
#define DBG(...)      EINA_LOG_DOM_DBG(_log_domain, __VA_ARGS__)

// Global variables
static Eina_Bool is_fullscreen = EINA_FALSE;
static Evas_Object *slideshow_image = NULL;
static Evas_Object *slideshow_video = NULL;
static Evas_Object *letterbox_bg = NULL;
static Evas_Object *button_box = NULL;
static Ecore_Timer *slideshow_timer = NULL;
static Eina_List *media_files = NULL;
static int current_media_index = 0;
static Eina_Bool slideshow_running = EINA_TRUE;
static Eina_Bool controls_visible = EINA_FALSE;

// Clock variables
static Evas_Object *clock_label = NULL;
static Ecore_Timer *clock_timer = NULL;
static Eina_Bool clock_visible = EINA_TRUE;
static Ecore_Animator *clock_fade_animator = NULL;
static Eina_Bool clock_is_fading = EINA_FALSE;
static double clock_fade_start_time = 0.0;
static Eina_Bool clock_target_visible = EINA_TRUE;

// Fade transition variables
static Ecore_Animator *fade_animator = NULL;
static Eina_Bool is_fading = EINA_FALSE;
static char *next_media_path = NULL;
static double fade_start_time = 0.0;

// Slideshow configuration
#define SLIDESHOW_INTERVAL 10.0  // 10 seconds between images
#define IMAGES_DIR "./images/"
#define FADE_DURATION 0.5  // 0.5 seconds fade duration

// Clock configuration
#define CLOCK_UPDATE_INTERVAL 1.0  // Update clock every second
#define CLOCK_FADE_DURATION 0.3    // 0.3 seconds fade duration for clock
#define CLOCK_FONT_SIZE 48         // Large font size for readability

// Function to check if a file has an image extension
static Eina_Bool
is_image_file(const char *filename)
{
   return (eina_str_has_suffix(filename, ".png") || 
           eina_str_has_suffix(filename, ".jpg") || 
           eina_str_has_suffix(filename, ".jpeg") || 
           eina_str_has_suffix(filename, ".gif") || 
           eina_str_has_suffix(filename, ".bmp"));
}

// Function to check if a file has a video extension
static Eina_Bool
is_video_file(const char *filename)
{
   return (eina_str_has_suffix(filename, ".mp4") || 
           eina_str_has_suffix(filename, ".mov") || 
           eina_str_has_suffix(filename, ".avi") || 
           eina_str_has_suffix(filename, ".mkv") || 
           eina_str_has_suffix(filename, ".webm"));
}

// Function to check if a file is a supported media file
static Eina_Bool
is_media_file(const char *filename)
{
   return is_image_file(filename) || is_video_file(filename);
}

// Clock fade animation callback function
static Eina_Bool
clock_fade_animator_cb(void *data EINA_UNUSED)
{
   double current_time = ecore_time_get();
   double elapsed = current_time - clock_fade_start_time;
   double progress = elapsed / CLOCK_FADE_DURATION;
   
   if (progress >= 1.0)
   {
      progress = 1.0;
      clock_fade_animator = NULL;
      clock_is_fading = EINA_FALSE;
   }
   
   int alpha;
   if (clock_target_visible)
   {
      // Fading in
      alpha = (int)(255 * progress);
      clock_visible = (progress >= 1.0) ? EINA_TRUE : clock_visible;
   }
   else
   {
      // Fading out
      alpha = (int)(255 * (1.0 - progress));
      if (progress >= 1.0)
      {
         clock_visible = EINA_FALSE;
         evas_object_hide(clock_label);
      }
   }
   
   if (alpha < 0) alpha = 0;
   if (alpha > 255) alpha = 255;
   
   // Apply alpha to clock (premultiplied colors)
   evas_object_color_set(clock_label, alpha, alpha, alpha, alpha);
   
   return (progress < 1.0) ? ECORE_CALLBACK_RENEW : ECORE_CALLBACK_CANCEL;
}

// Function to update clock display
static void
update_clock_display(void)
{
   time_t rawtime;
   struct tm *timeinfo;
   char time_buffer[64];
   char date_buffer[64];
   char full_buffer[128];
   
   if (!clock_label) return;
   
   time(&rawtime);
   timeinfo = localtime(&rawtime);
   
   // Format time in 12-hour format with AM/PM (iOS/Windows style)
   strftime(time_buffer, sizeof(time_buffer), "%I:%M", timeinfo);
   strftime(date_buffer, sizeof(date_buffer), "%A, %B %d", timeinfo);
   
   // Remove leading zero from hour if present
   if (time_buffer[0] == '0')
   {
      memmove(time_buffer, time_buffer + 1, strlen(time_buffer));
   }
   
   // Create formatted string with time and date with shadow for visibility
   snprintf(full_buffer, sizeof(full_buffer), 
            "<font_size=%d><color=#FFFFFF><shadow><shadow_color=#000000><shadow_offset=2,2>%s</shadow></shadow></color></font_size><br>"
            "<font_size=%d><color=#CCCCCC><shadow><shadow_color=#000000><shadow_offset=1,1>%s</shadow></shadow></color></font_size>", 
            CLOCK_FONT_SIZE, time_buffer, 
            CLOCK_FONT_SIZE / 2, date_buffer);
   
   elm_object_text_set(clock_label, full_buffer);
}

// Timer callback for clock updates
static Eina_Bool
clock_timer_cb(void *data EINA_UNUSED)
{
   if (clock_visible || clock_is_fading)
   {
      update_clock_display();
   }
   return ECORE_CALLBACK_RENEW;  // Keep the timer running
}

// Function to toggle clock visibility with smooth transition
static void
toggle_clock_visibility(void)
{
   if (clock_is_fading) return;  // Already fading
   
   clock_target_visible = !clock_visible;
   clock_is_fading = EINA_TRUE;
   clock_fade_start_time = ecore_time_get();
   
   if (clock_fade_animator)
      ecore_animator_del(clock_fade_animator);
   
   // If fading in, show the clock widget first
   if (clock_target_visible && !clock_visible)
   {
      evas_object_show(clock_label);
      update_clock_display();  // Update immediately when showing
   }
   
   clock_fade_animator = ecore_animator_add(clock_fade_animator_cb, NULL);
   
   if (clock_target_visible)
   {
      INF("Clock fade in started");
      printf("Clock shown\n");
   }
   else
   {
      INF("Clock fade out started");
      printf("Clock hidden\n");
   }
}

// Function to load media files from the images directory
static void
load_media_files(void)
{
   DIR *dir;
   struct dirent *entry;
   Eina_Strbuf *filepath_buf;
   const char *filepath;
   struct stat file_stat;
   
   dir = opendir(IMAGES_DIR);
   if (!dir)
   {
      ERR("Could not open images directory: %s", IMAGES_DIR);
      return;
   }
   
   filepath_buf = eina_strbuf_new();
   
   while ((entry = readdir(dir)) != NULL)
   {
      // Skip hidden files and directories
      if (entry->d_name[0] == '.') continue;
      
      if (is_media_file(entry->d_name))
      {
         eina_strbuf_reset(filepath_buf);
         eina_strbuf_append(filepath_buf, IMAGES_DIR);
         eina_strbuf_append(filepath_buf, entry->d_name);
         filepath = eina_strbuf_string_get(filepath_buf);
         
         // Check if it's a regular file
         if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode))
         {
            // Use stringshare for efficient string storage
            const char *shared_path = eina_stringshare_add(filepath);
            media_files = eina_list_append(media_files, (void*)shared_path);
            if (is_image_file(entry->d_name))
               INF("Added image: %s", shared_path);
            else
               INF("Added video: %s", shared_path);
         }
      }
   }
   
   eina_strbuf_free(filepath_buf);
   closedir(dir);
   
   if (eina_list_count(media_files) == 0)
   {
      WRN("No media files found in %s", IMAGES_DIR);
   }
   else
   {
      INF("Loaded %d media files", eina_list_count(media_files));
   }
}

// Fade animation callback function
static Eina_Bool
fade_animator_cb(void *data EINA_UNUSED)
{
   double current_time = ecore_time_get();
   double elapsed = current_time - fade_start_time;
   double progress = elapsed / FADE_DURATION;
   
   // Calculate fade effect
   int alpha;
   
   if (next_media_path)
   {
      // Phase 1: Fading out current media
      if (progress >= 1.0)
      {
         // Fade out complete - switch to new media and start fade in
         progress = 1.0;
         alpha = 0;  // Fully faded out
         
         // Apply final fade out (premultiplied colors)
          evas_object_color_set(slideshow_image, alpha, alpha, alpha, alpha);
          evas_object_color_set(slideshow_video, alpha, alpha, alpha, alpha);
         
         // Load the new media
         if (is_image_file(next_media_path))
         {
            // Show image in letterbox
            if (slideshow_video) evas_object_hide(slideshow_video);
            if (slideshow_image)
            {
               elm_image_file_set(slideshow_image, next_media_path, NULL);
               elm_object_content_set(letterbox_bg, slideshow_image);
               evas_object_show(slideshow_image);
               INF("Showing image: %s", next_media_path);
            }
         }
         else if (is_video_file(next_media_path))
         {
            // Show video in letterbox
            if (slideshow_image) evas_object_hide(slideshow_image);
            if (slideshow_video)
            {
               elm_video_file_set(slideshow_video, next_media_path);
               elm_object_content_set(letterbox_bg, slideshow_video);
               elm_video_play(slideshow_video);
               evas_object_show(slideshow_video);
               INF("Showing video: %s", next_media_path);
            }
         }
         
         // Clean up and start fade in phase
         free(next_media_path);
         next_media_path = NULL;
         fade_start_time = current_time;  // Reset timer for fade in
      }
      else
      {
         // Fading out - decrease alpha
         alpha = (int)(255 * (1.0 - progress));
         if (alpha < 0) alpha = 0;
         if (alpha > 255) alpha = 255;
         
         // Apply alpha to current visible media (premultiplied colors)
          evas_object_color_set(slideshow_image, alpha, alpha, alpha, alpha);
          evas_object_color_set(slideshow_video, alpha, alpha, alpha, alpha);
      }
   }
   else
   {
      // Phase 2: Fading in new media
      if (progress >= 1.0)
      {
         // Fade in complete (premultiplied colors)
          alpha = 255;
          evas_object_color_set(slideshow_image, alpha, alpha, alpha, alpha);
          evas_object_color_set(slideshow_video, alpha, alpha, alpha, alpha);
         
         // Animation complete
         fade_animator = NULL;
         is_fading = EINA_FALSE;
         return ECORE_CALLBACK_CANCEL;
      }
      else
      {
         // Fading in - increase alpha
         alpha = (int)(255 * progress);
         if (alpha < 0) alpha = 0;
         if (alpha > 255) alpha = 255;
         
         // Apply alpha to current visible media (premultiplied colors)
          evas_object_color_set(slideshow_image, alpha, alpha, alpha, alpha);
          evas_object_color_set(slideshow_video, alpha, alpha, alpha, alpha);
      }
   }
   
   return ECORE_CALLBACK_RENEW;
}

// Function to start fade transition to new media
static void
start_fade_transition(const char *media_path)
{
   if (is_fading) return;  // Already fading
   
   is_fading = EINA_TRUE;
   next_media_path = strdup(media_path);
   fade_start_time = ecore_time_get();
   
   if (fade_animator)
      ecore_animator_del(fade_animator);
   
   fade_animator = ecore_animator_add(fade_animator_cb, NULL);
}

// Function to show the next media in the slideshow
static void
show_next_media(void)
{
   char *media_path;
   int count;
   int new_index;
   
   if (!media_files) return;
   
   count = eina_list_count(media_files);
   if (count == 0) return;
   
   // Skip if already fading
   if (is_fading) return;
   
   // Get next media file (avoid showing the same image twice in a row)
   if (count == 1)
   {
      // Only one file, use it
      new_index = 0;
   }
   else
   {
      // Multiple files - pick a different one than current
      do {
         new_index = rand() % count;
      } while (new_index == current_media_index);
   }
   
   current_media_index = new_index;
   media_path = eina_list_nth(media_files, current_media_index);
   
   if (media_path)
   {
      // Start fade transition to new media
      start_fade_transition(media_path);
   }
}

// Function to show media immediately (without fade, for initial load)
static void
show_media_immediate(const char *media_path)
{
   if (!media_path) return;
   
   if (is_image_file(media_path))
   {
      // Show image in letterbox
      if (slideshow_video) evas_object_hide(slideshow_video);
      if (slideshow_image)
      {
         elm_image_file_set(slideshow_image, media_path, NULL);
         elm_object_content_set(letterbox_bg, slideshow_image);
         evas_object_show(slideshow_image);
         evas_object_color_set(slideshow_image, 255, 255, 255, 255);  // Full opacity (premultiplied)
         INF("Showing image: %s", media_path);
      }
   }
   else if (is_video_file(media_path))
   {
      // Show video in letterbox
      if (slideshow_image) evas_object_hide(slideshow_image);
      if (slideshow_video)
      {
         elm_video_file_set(slideshow_video, media_path);
         elm_object_content_set(letterbox_bg, slideshow_video);
         elm_video_play(slideshow_video);
         evas_object_show(slideshow_video);
         evas_object_color_set(slideshow_video, 255, 255, 255, 255);  // Full opacity (premultiplied)
         INF("Showing video: %s", media_path);
      }
   }
}

// Timer callback for automatic slideshow
static Eina_Bool
slideshow_timer_cb(void *data EINA_UNUSED)
{
   if (slideshow_running)
   {
      show_next_media();
   }
   return ECORE_CALLBACK_RENEW;  // Keep the timer running
}

// Function to start/stop slideshow
static void
toggle_slideshow(void)
{
   slideshow_running = !slideshow_running;
   
   if (slideshow_running)
   {
      INF("Slideshow started");
      printf("Slideshow started\n");
   }
   else
   {
      INF("Slideshow paused");
      printf("Slideshow paused\n");
   }
}

// Function to toggle control visibility
static void
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

static void
on_done(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   // Cleanup slideshow resources
   if (slideshow_timer)
   {
      ecore_timer_del(slideshow_timer);
      slideshow_timer = NULL;
   }
   
   // Cleanup clock resources
   if (clock_timer)
   {
      ecore_timer_del(clock_timer);
      clock_timer = NULL;
   }
   
   if (clock_fade_animator)
   {
      ecore_animator_del(clock_fade_animator);
      clock_fade_animator = NULL;
   }
   
   // Cleanup fade animator
   if (fade_animator)
   {
      ecore_animator_del(fade_animator);
      fade_animator = NULL;
   }
   
   // Cleanup fade transition state
   if (next_media_path)
   {
      free(next_media_path);
      next_media_path = NULL;
   }
   
   // Stop any playing video before cleanup
   if (slideshow_video)
   {
      elm_video_stop(slideshow_video);
   }
   
   // Free media file paths (stringshare)
   const char *media_path;
   EINA_LIST_FREE(media_files, media_path)
   {
      eina_stringshare_del(media_path);
   }
   
   // Reset global pointers
   slideshow_image = NULL;
   slideshow_video = NULL;
   letterbox_bg = NULL;
   button_box = NULL;
   clock_label = NULL;
   
   // Called when the window is closed
   INF("Application shutdown requested");
   elm_exit();
}

static void
on_button_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   INF("Slideshow toggle button clicked!");
   toggle_slideshow();
}

// Button callback functions
static void
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
      INF("Switched to windowed mode");
      printf("Switched to windowed mode\n");
   }
}

static void
on_next_image_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   show_next_media();
   INF("Manual next media");
   printf("Next media\n");
}

static void
on_toggle_clock_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   toggle_clock_visibility();
   INF("Clock toggle button clicked!");
}

// Click handler for media area to toggle controls
static void
on_media_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   toggle_controls();
}

EAPI_MAIN int
elm_main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   Evas_Object *win, *box, *label;

   // Initialize logging domain
   _log_domain = eina_log_domain_register("efl-hello", EINA_COLOR_BLUE);
   if (_log_domain < 0)
   {
      EINA_LOG_CRIT("Could not register log domain: efl-hello");
      return -1;
   }
   
   INF("EFL Hello World application starting");

   // Initialize random seed for slideshow using Ecore time
   srand((unsigned int)ecore_time_get());

   // Set application policy
   elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

   // Create main window
   win = elm_win_util_standard_add("efl-hello", "EFL Hello World Slideshow");
   evas_object_smart_callback_add(win, "delete,request", on_done, NULL);
   
   // Set window background to black to match letterboxing
   Evas_Object *win_bg = elm_bg_add(win);
   elm_bg_color_set(win_bg, 0, 0, 0);  // Black background
   evas_object_size_hint_weight_set(win_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, win_bg);
   evas_object_show(win_bg);
   
   INF("Main window created");

   // Window setup complete
   INF("Window created and configured");

   // Create a vertical box container
   box = elm_box_add(win);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_object_content_set(win_bg, box);  // Set box as content of background
   evas_object_show(box);

   // Create a label
   label = elm_label_add(win);
   
   evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(label, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, label);
   evas_object_show(label);

   // Create letterbox background container
   letterbox_bg = elm_bg_add(win);
   elm_bg_color_set(letterbox_bg, 0, 0, 0);  // Black background for letterbox effect
   evas_object_size_hint_weight_set(letterbox_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(letterbox_bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_smart_callback_add(letterbox_bg, "clicked", on_media_click, NULL);
   elm_box_pack_end(box, letterbox_bg);
   evas_object_show(letterbox_bg);

   // Create image widget for slideshow with letterbox effect
   slideshow_image = elm_image_add(letterbox_bg);
   elm_image_aspect_fixed_set(slideshow_image, EINA_TRUE);  // Maintain aspect ratio
   elm_image_fill_outside_set(slideshow_image, EINA_FALSE);  // Don't fill outside bounds
   elm_image_resizable_set(slideshow_image, EINA_TRUE, EINA_TRUE);
   elm_image_smooth_set(slideshow_image, EINA_TRUE);
   evas_object_size_hint_weight_set(slideshow_image, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(slideshow_image, 0.5, 0.5);  // Center the image
   evas_object_smart_callback_add(slideshow_image, "clicked", on_media_click, NULL);
   
   // Set the image as the background's content
   elm_object_content_set(letterbox_bg, slideshow_image);
   evas_object_show(slideshow_image);

   // Create video widget for slideshow (will be shown in the same letterbox container)
   slideshow_video = elm_video_add(letterbox_bg);
   elm_video_remember_position_set(slideshow_video, EINA_FALSE);
   evas_object_size_hint_weight_set(slideshow_video, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(slideshow_video, 0.5, 0.5);  // Center the video
   evas_object_smart_callback_add(slideshow_video, "clicked", on_media_click, NULL);
   evas_object_hide(slideshow_video);  // Initially hidden

   // Create clock overlay widget with high-quality typography
   clock_label = elm_label_add(win);
   elm_label_line_wrap_set(clock_label, ELM_WRAP_NONE);
   evas_object_size_hint_weight_set(clock_label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(clock_label, 0.5, 0.1);  // Center horizontally, near top
   
   // Set initial clock text with shadow effect for better visibility
   elm_object_text_set(clock_label, 
      "<font_size=48><color=#FFFFFF><shadow><shadow_color=#000000><shadow_offset=2,2>12:00</shadow></shadow></color></font_size><br>"
      "<font_size=24><color=#CCCCCC><shadow><shadow_color=#000000><shadow_offset=1,1>Loading...</shadow></shadow></color></font_size>");
   
   // Position clock as overlay on the letterbox background
   evas_object_layer_set(clock_label, 100);  // High layer to ensure it's on top
   evas_object_smart_callback_add(clock_label, "clicked", on_media_click, NULL);
   evas_object_show(clock_label);
   
   // Add clock to the main box for proper positioning
   elm_box_pack_end(box, clock_label);
   
   INF("Clock widget created and positioned");

   // Create horizontal box for control buttons
   button_box = elm_box_add(win);
   elm_box_horizontal_set(button_box, EINA_TRUE);
   elm_box_homogeneous_set(button_box, EINA_TRUE);
   evas_object_size_hint_weight_set(button_box, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(button_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, button_box);
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

   // Create Fullscreen button
   Evas_Object *fullscreen_btn = elm_button_add(win);
   elm_object_text_set(fullscreen_btn, "Fullscreen");
   evas_object_smart_callback_add(fullscreen_btn, "clicked", on_fullscreen_click, win);
   evas_object_size_hint_weight_set(fullscreen_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(fullscreen_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, fullscreen_btn);
   evas_object_show(fullscreen_btn);

   // Create Toggle Clock button
   Evas_Object *clock_btn = elm_button_add(win);
   elm_object_text_set(clock_btn, "Toggle Clock");
   evas_object_smart_callback_add(clock_btn, "clicked", on_toggle_clock_click, NULL);
   evas_object_size_hint_weight_set(clock_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(clock_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(button_box, clock_btn);
   evas_object_show(clock_btn);

   // Load media files and start slideshow
   load_media_files();
   
   if (eina_list_count(media_files) > 0)
   {
      // Show first media immediately (without fade)
      current_media_index = rand() % eina_list_count(media_files);
      char *first_media = eina_list_nth(media_files, current_media_index);
      show_media_immediate(first_media);
      
      // Start slideshow timer
      slideshow_timer = ecore_timer_add(SLIDESHOW_INTERVAL, slideshow_timer_cb, NULL);
      INF("Slideshow timer started with %f second interval", SLIDESHOW_INTERVAL);
   }
   else
   {
      // Show placeholder text if no images found
      elm_image_file_set(slideshow_image, NULL, NULL);
      WRN("No images found - slideshow disabled");
   }

   // Start clock timer (always running regardless of media files)
   clock_timer = ecore_timer_add(CLOCK_UPDATE_INTERVAL, clock_timer_cb, NULL);
   update_clock_display();  // Update immediately
   INF("Clock timer started with %f second interval", CLOCK_UPDATE_INTERVAL);

   // Set window size and show
   evas_object_resize(win, 640, 480);
   evas_object_show(win);
   INF("Window displayed, entering main loop");

   // Start the main loop
   elm_run();

   // Cleanup logging domain
   if (_log_domain >= 0)
   {
      eina_log_domain_unregister(_log_domain);
      _log_domain = -1;
   }

   return 0;
}
ELM_MAIN()