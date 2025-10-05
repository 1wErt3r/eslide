#include "slideshow.h"
#include "ui.h"

// Slideshow state variables
Eina_Bool slideshow_running = EINA_TRUE;
Eina_Bool is_shuffle_mode = EINA_FALSE;  // Default to sequential mode
Ecore_Timer *slideshow_timer = NULL;

// Fade transition variables
Ecore_Animator *fade_animator = NULL;
Eina_Bool is_fading = EINA_FALSE;
char *next_media_path = NULL;
double fade_start_time = 0.0;

// Runtime-configurable timings (initialized to compile-time defaults)
static double slideshow_interval_runtime = SLIDESHOW_INTERVAL;
static double fade_duration_runtime = FADE_DURATION;

// Preloading support: hidden Evas image used to warm cache for next image
static Evas_Object *preload_img = NULL;
// Navigation coalescing: queue next/prev requests during active fade
static int pending_nav = 0; // 0 = none, 1 = next, -1 = prev

// Helper to determine next index without mutating current state
static int _compute_next_index(void)
{
   int count = get_media_file_count();
   if (count <= 0) return -1;

   if (is_shuffle_mode)
   {
      if (count == 1)
         return 0;
      int idx;
      do {
         idx = rand() % count;
      } while (idx == current_media_index);
      return idx;
   }
   else
   {
      return (current_media_index + 1) % count;
   }
}

// Preload the next image into the Evas cache to reduce stutter
static void preload_next_image(void)
{
   int next_index = _compute_next_index();
   if (next_index < 0) return;

   char *next_path = get_media_path_at_index(next_index);
   if (!next_path) return;

   if (!is_image_file(next_path))
   {
      // Only preload images for now
      return;
   }

   if (!preload_img)
   {
      // Create a hidden Evas image tied to the same canvas
      Evas *evas = evas_object_evas_get(letterbox_bg);
      if (!evas) return;
      preload_img = evas_object_image_add(evas);
      if (!preload_img) return;
      evas_object_hide(preload_img);
      // Match some typical flags to resemble display image scaling behavior
      evas_object_image_smooth_scale_set(preload_img, EINA_TRUE);
   }

   // Set file and trigger asynchronous preload into cache
   evas_object_image_file_set(preload_img, next_path, NULL);
   evas_object_image_preload(preload_img, EINA_TRUE);
   DBG("Preloading next image: %s", next_path);
}

void
slideshow_set_interval(double seconds)
{
   if (seconds > 0.0)
      slideshow_interval_runtime = seconds;
}

void
slideshow_set_fade_duration(double seconds)
{
   if (seconds > 0.0)
      fade_duration_runtime = seconds;
}

// Fade animation callback function
Eina_Bool
fade_animator_cb(void *data EINA_UNUSED)
{
   double current_time = ecore_time_get();
   double elapsed = current_time - fade_start_time;
   double progress = elapsed / fade_duration_runtime;
   
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
         // Prepare the next image in advance
         preload_next_image();
         // Execute any queued navigation coalesced during fade
         if (pending_nav != 0)
         {
            int dir = pending_nav;
            pending_nav = 0;
            if (dir > 0) show_next_media(); else show_prev_media();
         }
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
void
start_fade_transition(const char *media_path)
{
   if (is_fading) return;  // Already fading

   is_fading = EINA_TRUE;
   next_media_path = strdup(media_path);
   fade_start_time = ecore_time_get();

   if (fade_animator)
      ecore_animator_del(fade_animator);

   fade_animator = ecore_animator_add(fade_animator_cb, NULL);
   // Kick off preload when fade starts to reduce stutter
   preload_next_image();
}

// Function to show the next media in the slideshow
void
show_next_media(void)
{
   char *media_path;
   int count;
   int new_index;
   
   count = get_media_file_count();
   if (count == 0) return;
   
   // Skip if already fading; queue request
   if (is_fading) { pending_nav = 1; return; }
   
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
   // Update progress overlay now
   ui_progress_update_index(current_media_index, count);
   
   if (media_path)
   {
      // Start fade transition to new media
      start_fade_transition(media_path);
      // Proactively preload the subsequent image
      preload_next_image();
   }
}

// Function to show the previous media in the slideshow
void
show_prev_media(void)
{
   char *media_path;
   int count;
   int new_index;

   count = get_media_file_count();
   if (count == 0) return;

   // Skip if already fading; queue request
   if (is_fading) { pending_nav = -1; return; }

   if (is_shuffle_mode)
   {
      // Random shuffle mode
      if (count == 1)
      {
         new_index = 0;
      }
      else
      {
         // Pick a different one than current
         do {
            new_index = rand() % count;
         } while (new_index == current_media_index);
      }
   }
   else
   {
      // Sequential mode - go to previous file in order
      new_index = (current_media_index - 1 + count) % count;
   }

   current_media_index = new_index;
   media_path = get_media_path_at_index(current_media_index);
   // Update progress overlay now
   ui_progress_update_index(current_media_index, count);

   if (media_path)
   {
      // Start fade transition to new media
      start_fade_transition(media_path);
      // Warm cache for the subsequent image
      preload_next_image();
   }
}

// Function to show media immediately (without fade, for initial load)
void
show_media_immediate(const char *media_path)
{
   if (!media_path) return;
   
   printf("show_media_immediate called with: %s\n", media_path);
   printf("slideshow_image: %p, slideshow_video: %p, letterbox_bg: %p\n", 
          slideshow_image, slideshow_video, letterbox_bg);
   
   if (is_image_file(media_path))
   {
      printf("Detected as image file: %s\n", media_path);
      // Show image in letterbox
      if (slideshow_video) evas_object_hide(slideshow_video);
      if (slideshow_image)
      {
         printf("Setting image file: %s\n", media_path);
         Eina_Bool result = elm_image_file_set(slideshow_image, media_path, NULL);
         printf("elm_image_file_set result: %d\n", result);
         elm_object_content_set(letterbox_bg, slideshow_image);
         evas_object_show(slideshow_image);
         evas_object_color_set(slideshow_image, 255, 255, 255, 255);  // Full opacity (premultiplied)
         printf("Image display setup completed\n");
         INF("Showing image: %s", media_path);
      }
      else
      {
         printf("ERROR: slideshow_image is NULL!\n");
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

   // Update compact progress overlay for the initially shown media
   // Use current_media_index and the loaded media list count
   ui_progress_update_index(current_media_index, eina_list_count(media_files));
}

// Timer callback for automatic slideshow
Eina_Bool
slideshow_timer_cb(void *data EINA_UNUSED)
{
   if (slideshow_running)
   {
      show_next_media();
   }
   return ECORE_CALLBACK_RENEW;  // Keep the timer running
}

// Function to start/stop slideshow
void
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

// Function to toggle shuffle mode
void
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

// Slideshow initialization
void
slideshow_init(Evas_Object *image_widget, Evas_Object *video_widget, Evas_Object *letterbox)
{
   printf("slideshow_init called with image: %p, video: %p, letterbox: %p\n", 
          image_widget, video_widget, letterbox);
   slideshow_image = image_widget;
   slideshow_video = video_widget;
   letterbox_bg = letterbox;
   printf("slideshow_init completed - stored image: %p, video: %p, letterbox: %p\n", 
          slideshow_image, slideshow_video, letterbox_bg);
}

// Start slideshow timer
void
slideshow_start(void)
{
   // Initialize random seed for slideshow using Ecore time
   srand((unsigned int)ecore_time_get());
   
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
      slideshow_timer = ecore_timer_add(slideshow_interval_runtime, slideshow_timer_cb, NULL);
      INF("Slideshow timer started with %f second interval", slideshow_interval_runtime);
   }
   else
   {
      // Show placeholder text if no images found
      if (slideshow_image)
         elm_image_file_set(slideshow_image, NULL, NULL);
      WRN("No images found - slideshow disabled");
   }
}

// Slideshow cleanup
void
slideshow_cleanup(void)
{
   // Cleanup slideshow resources
   if (slideshow_timer)
   {
      ecore_timer_del(slideshow_timer);
      slideshow_timer = NULL;
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
   
   // Reset global pointers
   slideshow_image = NULL;
   slideshow_video = NULL;
   letterbox_bg = NULL;

   // Cleanup preload image
   if (preload_img)
   {
      evas_object_del(preload_img);
      preload_img = NULL;
   }
}

// Convenience alias for previous navigation
void
slideshow_prev(void)
{
   show_prev_media();
}