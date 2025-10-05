#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
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

// Media file structure to store paths
typedef struct _MediaFile {
   char *path;
   Eina_Bool is_image;
} MediaFile;

// Global variables
static Eina_Bool is_fullscreen = EINA_TRUE;
static Evas_Object *slideshow_image = NULL;
static Evas_Object *slideshow_video = NULL;
static Evas_Object *letterbox_bg = NULL;
static Evas_Object *button_box = NULL;
static Ecore_Timer *slideshow_timer = NULL;
static Eina_List *media_files = NULL;
static int current_media_index = 0;
static Eina_Bool slideshow_running = EINA_TRUE;
static Eina_Bool controls_visible = EINA_FALSE;
static Eina_Bool is_shuffle_mode = EINA_FALSE;  // Default to sequential mode

// Digital clock variables
static Evas_Object *clock_label = NULL;
static Ecore_Timer *clock_timer = NULL;
static Eina_Bool clock_visible = EINA_FALSE;  // Clock hidden by default

// Fade transition variables
static Ecore_Animator *fade_animator = NULL;
static Eina_Bool is_fading = EINA_FALSE;
static char *next_media_path = NULL;
static double fade_start_time = 0.0;

// Slideshow configuration
#define SLIDESHOW_INTERVAL 10.0  // 10 seconds between images
#define IMAGES_DIR "./images/"
#define FADE_DURATION 0.5  // 0.5 seconds fade duration

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

// Function to free a media file structure
static void
free_media_file(MediaFile *media_file)
{
   if (!media_file) return;
   
   if (media_file->path) {
      free(media_file->path);
      media_file->path = NULL;
   }
   free(media_file);
}

// Function to scan and store media files from the images directory (lazy loading)
static void
scan_media_files(void)
{
   DIR *dir;
   struct dirent *entry;
   Eina_Strbuf *filepath_buf;
   char *filepath;
   struct stat file_stat;
   
   // Free any existing media file list before scanning
   MediaFile *media_file;
   EINA_LIST_FREE(media_files, media_file) {
      free_media_file(media_file);
   }
   media_files = NULL;
   
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
         filepath = strdup(eina_strbuf_string_get(filepath_buf));
         
         // Check if it's a regular file
         if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode))
         {
            // Create media file structure
            MediaFile *new_media = malloc(sizeof(MediaFile));
            if (new_media)
            {
               new_media->path = filepath;
               new_media->is_image = is_image_file(entry->d_name);
               media_files = eina_list_append(media_files, new_media);
               
               if (new_media->is_image)
                  INF("Added image: %s", new_media->path);
               else
                  INF("Added video: %s", new_media->path);
            }
            else
            {
               free(filepath);
            }
         }
         else
         {
            free(filepath);
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

// Function to get the count of media files (scans directory each time for up-to-date count)
static int
get_media_file_count(void)
{
   // Scan media files to ensure we have current count
   scan_media_files();
   return eina_list_count(media_files);
}

// Function to get path of media at specific index
static char*
get_media_path_at_index(int index)
{
   if (index < 0)
      return NULL;
      
   scan_media_files();  // Refresh the list to ensure it's current
   unsigned int media_count = eina_list_count(media_files);
   if ((unsigned int)index >= media_count)
      return NULL;
      
   MediaFile *media_file = eina_list_nth(media_files, index);
   if (!media_file)
      return NULL;
      
   return media_file->path;
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
   
   count = get_media_file_count();
   if (count == 0) return;
   
   // Skip if already fading
   if (is_fading) return;
   
   if (is_shuffle_mode)
   {
      // Random shuffle mode
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
   }
   else
   {
      // Sequential mode - go to next file in order
      new_index = (current_media_index + 1) % count;
   }
   
   current_media_index = new_index;
   media_path = get_media_path_at_index(current_media_index);
   
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

// Timer callback for digital clock updates
static Eina_Bool
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
   
   // Cleanup clock timer
   if (clock_timer)
   {
      ecore_timer_del(clock_timer);
      clock_timer = NULL;
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
   
   // Free media file structures
   MediaFile *media_file;
   EINA_LIST_FREE(media_files, media_file)
   {
      free_media_file(media_file);
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

// Function to toggle shuffle mode
static void
toggle_shuffle_mode(void)
{
   is_shuffle_mode = !is_shuffle_mode;
   
   if (is_shuffle_mode)
   {
      INF("Shuffle mode enabled");
      printf("Shuffle mode enabled\n");
   }
   else
   {
      INF("Sequential mode enabled");
      printf("Sequential mode enabled\n");
   }
}

// Callback for shuffle toggle button
static void
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

// Click handler for media area to toggle controls
static void
on_media_click(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   toggle_controls();
}

// Callback to reposition clock when letterbox is resized
static void
on_letterbox_resize(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (!clock_label) return;
   
   int x, y, w, h;
   evas_object_geometry_get(obj, &x, &y, &w, &h);
   
   // Position clock in lower right corner with padding, moved further left for larger container
   evas_object_move(clock_label, x + w - 180, y + h - 60);
}

// Toggle clock visibility
static void
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

// Clock toggle button callback
static void
on_clock_toggle_click(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   toggle_clock();
   
   // Update button text
   if (clock_visible)
      elm_object_text_set(obj, "Clock: ON");
   else
      elm_object_text_set(obj, "Clock: OFF");
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
   win = elm_win_util_standard_add("eslide", "eslide");
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

   // Set up resize callback for letterbox to reposition clock
   evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_RESIZE, on_letterbox_resize, NULL);
   
   // Create digital clock label as an overlay on the letterbox
   clock_label = elm_label_add(win);  // Add to window instead of letterbox for proper layering
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
   
   // Initial positioning will be done by the resize callback

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

   // Load media files and start slideshow
   int media_count = get_media_file_count();
   
   if (media_count > 0)
   {
      // Show first media immediately (without fade)
      // In sequential mode, start with the first file; in shuffle, pick a random one
      if (is_shuffle_mode)
      {
         current_media_index = rand() % media_count;
      }
      else
      {
         current_media_index = 0;  // Start with first file in sequential mode
      }
      
      char *first_media = get_media_path_at_index(current_media_index);
      if (first_media)
      {
         show_media_immediate(first_media);
      }
      
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

   // Initialize and start the digital clock timer
   clock_timer_cb(NULL);  // Update clock immediately
   clock_timer = ecore_timer_add(1.0, clock_timer_cb, NULL);  // Update every second
   INF("Digital clock timer started");

   // Set to fullscreen mode if enabled
   if (is_fullscreen)
   {
      elm_win_fullscreen_set(win, EINA_TRUE);
      INF("Application started in fullscreen mode");
   }
   
   // Set window size and show
   evas_object_resize(win, 640, 480);
   evas_object_show(win);
   
   // Trigger initial clock positioning
   on_letterbox_resize(NULL, NULL, letterbox_bg, NULL);
   
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
