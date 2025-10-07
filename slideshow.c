#include "slideshow.h"
#include "ui.h"

// Slideshow state variables
Eina_Bool slideshow_running = EINA_TRUE;
Eina_Bool is_shuffle_mode = EINA_FALSE; // Default to sequential mode
Ecore_Timer* slideshow_timer = NULL;

// Fade transition variables
Ecore_Animator* fade_animator = NULL;
Eina_Bool is_fading = EINA_FALSE;
char* next_media_path = NULL;
double fade_start_time = 0.0;
// Dedicated overlay to guarantee smooth crossfade independent of media load
static Evas_Object* fade_overlay = NULL;
// Hold overlay at full opacity until new media is fully ready
static Eina_Bool waiting_media_ready = EINA_FALSE;
static double waiting_start_time = 0.0;
// Forward declaration for image load readiness callback
static void _on_image_load_ready(void* data, Evas_Object* obj, void* event_info);

// Runtime-configurable timings (initialized to compile-time defaults)
static double slideshow_interval_runtime = SLIDESHOW_INTERVAL;
static double fade_duration_runtime = FADE_DURATION;

// Preloading support: hidden Evas image used to warm cache for next image
static Evas_Object* preload_img = NULL;
// Navigation coalescing: queue next/prev requests during active fade
static int pending_nav = 0; // 0 = none, 1 = next, -1 = prev

// Ensure the fade overlay exists and is configured
static void _ensure_fade_overlay(void)
{
    if (fade_overlay)
        return;
    if (!letterbox_bg)
        return;

    Evas* evas = evas_object_evas_get(letterbox_bg);
    if (!evas)
        return;

    fade_overlay = evas_object_rectangle_add(evas);
    // Start fully transparent black
    evas_object_color_set(fade_overlay, 0, 0, 0, 0);
    // Do not block input beneath
    evas_object_pass_events_set(fade_overlay, EINA_TRUE);
    // Keep overlay above content
    evas_object_raise(fade_overlay);
}

// Match overlay geometry to the letterbox area
static void _update_fade_overlay_geometry(void)
{
    if (!fade_overlay || !letterbox_bg)
        return;
    Evas_Coord x, y, w, h;
    evas_object_geometry_get(letterbox_bg, &x, &y, &w, &h);
    evas_object_move(fade_overlay, x, y);
    evas_object_resize(fade_overlay, w, h);
}

// Helper to determine next index without mutating current state
static int _compute_next_index(void)
{
    int count = get_media_file_count();
    if (count <= 0)
        return -1;

    if (is_shuffle_mode) {
        if (count == 1)
            return 0;
        int idx;
        do {
            idx = rand() % count;
        } while (idx == current_media_index);
        return idx;
    } else {
        return (current_media_index + 1) % count;
    }
}

// Preload the next image into the Evas cache to reduce stutter
static void preload_next_image(void)
{
    int next_index = _compute_next_index();
    if (next_index < 0)
        return;

    char* next_path = get_media_path_at_index(next_index);
    if (!next_path)
        return;

    if (!is_image_file(next_path)) {
        // Only preload images for now
        return;
    }

    if (!preload_img) {
        // Create a hidden Evas image tied to the same canvas
        Evas* evas = evas_object_evas_get(letterbox_bg);
        if (!evas)
            return;
        preload_img = evas_object_image_add(evas);
        if (!preload_img)
            return;
        evas_object_hide(preload_img);
        // Match some typical flags to resemble display image scaling behavior
        evas_object_image_smooth_scale_set(preload_img, EINA_TRUE);
    }

    // Set file and trigger asynchronous preload into cache
    evas_object_image_file_set(preload_img, next_path, NULL);
    evas_object_image_preload(preload_img, EINA_TRUE);
    DBG("Preloading next image: %s", next_path);
}

void slideshow_set_interval(double seconds)
{
    if (seconds > 0.0)
        slideshow_interval_runtime = seconds;
}

void slideshow_set_fade_duration(double seconds)
{
    // Allow setting to 0 or negative to disable fading entirely
    if (seconds <= 0.0)
        fade_duration_runtime = 0.0;
    else
        fade_duration_runtime = seconds;
}

double slideshow_get_interval(void)
{
    return slideshow_interval_runtime;
}

double slideshow_get_fade_duration(void)
{
    return fade_duration_runtime;
}

// Fade animation callback function
Eina_Bool fade_animator_cb(void* data EINA_UNUSED)
{
    double current_time = ecore_time_get();
    double elapsed = current_time - fade_start_time;
    double progress = (fade_duration_runtime > 0.0) ? (elapsed / fade_duration_runtime) : 1.0;
    if (progress < 0.0)
        progress = 0.0;
    if (progress > 1.0)
        progress = 1.0;
    // Smoothstep easing for extra smooth fade
    double t = progress;
    double eased = t * t * (3.0 - 2.0 * t);

    // Calculate fade effect for overlay
    int alpha;

    if (next_media_path) {
        // Phase 1: Fading out current media
        if (progress >= 1.0) {
            // Fade out complete - switch to new media and start fade in
            progress = 1.0;
            // Keep overlay fully opaque during swap
            alpha = 255;
            if (fade_overlay)
                evas_object_color_set(fade_overlay, 0, 0, 0, alpha);

            // If we're not already waiting for readiness, perform the swap now
            if (!waiting_media_ready) {
                // Load the new media
                if (is_image_file(next_media_path)) {
                    // Show image in letterbox
                    if (slideshow_video)
                        evas_object_hide(slideshow_video);
                    if (slideshow_image) {
                        elm_image_file_set(slideshow_image, next_media_path, NULL);
                        elm_object_content_set(letterbox_bg, slideshow_image);
                        evas_object_show(slideshow_image);
                        INF("Showing image: %s", next_media_path);

                        // Hold overlay until the image reports 'load,ready'
                        waiting_media_ready = EINA_TRUE;
                        waiting_start_time = current_time;
                        // Begin preloading on the display image to trigger callback when ready
                        {
                            Evas_Object* img_obj = elm_image_object_get(slideshow_image);
                            if (img_obj)
                                evas_object_image_preload(img_obj, EINA_TRUE);
                        }
                        // Ensure only one callback instance is registered
                        evas_object_smart_callback_del(
                            slideshow_image, "load,ready", _on_image_load_ready);
                        evas_object_smart_callback_add(
                            slideshow_image, "load,ready", _on_image_load_ready, NULL);
                    }
                } else if (is_video_file(next_media_path)) {
                    // Show video in letterbox
                    if (slideshow_image)
                        evas_object_hide(slideshow_image);
                    if (slideshow_video) {
                        elm_video_file_set(slideshow_video, next_media_path);
                        elm_object_content_set(letterbox_bg, slideshow_video);
                        elm_video_play(slideshow_video);
                        evas_object_show(slideshow_video);
                        INF("Showing video: %s", next_media_path);
                    }

                    // For videos, proceed to fade-in immediately
                    free(next_media_path);
                    next_media_path = NULL;
                    waiting_media_ready = EINA_FALSE;
                    fade_start_time = current_time; // Reset timer for fade in
                }
            }

            // Timeout fallback: if image does not report ready, proceed anyway
            if (waiting_media_ready
                && (current_time - waiting_start_time)
                    > (fade_duration_runtime > 0.0 ? (fade_duration_runtime * 2.0) : 1.0)) {
                WRN("Image load timeout; proceeding with fade-in");
                free(next_media_path);
                next_media_path = NULL;
                waiting_media_ready = EINA_FALSE;
                fade_start_time = current_time;
            }
        } else {
            // Fading out - increase overlay alpha smoothly
            alpha = (int) (255 * eased);
            if (alpha < 0)
                alpha = 0;
            if (alpha > 255)
                alpha = 255;
            if (fade_overlay)
                evas_object_color_set(fade_overlay, 0, 0, 0, alpha);
        }
    } else {
        // Phase 2: Fading in new media
        if (progress >= 1.0) {
            // Fade in complete - hide overlay
            alpha = 0;
            if (fade_overlay)
                evas_object_color_set(fade_overlay, 0, 0, 0, alpha);
            if (fade_overlay)
                evas_object_hide(fade_overlay);

            // Animation complete
            fade_animator = NULL;
            is_fading = EINA_FALSE;
            // Prepare the next image in advance
            preload_next_image();
            // Execute any queued navigation coalesced during fade
            if (pending_nav != 0) {
                int dir = pending_nav;
                pending_nav = 0;
                if (dir > 0)
                    show_next_media();
                else
                    show_prev_media();
            }
            return ECORE_CALLBACK_CANCEL;
        } else {
            // Fading in - decrease overlay alpha smoothly
            alpha = (int) (255 * (1.0 - eased));
            if (alpha < 0)
                alpha = 0;
            if (alpha > 255)
                alpha = 255;
            if (fade_overlay)
                evas_object_color_set(fade_overlay, 0, 0, 0, alpha);
        }
    }

    return ECORE_CALLBACK_RENEW;
}

// Function to start fade transition to new media
void start_fade_transition(const char* media_path)
{
    if (is_fading)
        return; // Already fading

    // If fading is disabled, switch immediately without animator
    if (fade_duration_runtime <= 0.0) {
        if (!media_path)
            return;
        if (is_image_file(media_path)) {
            if (slideshow_video)
                evas_object_hide(slideshow_video);
            if (slideshow_image) {
                elm_image_file_set(slideshow_image, media_path, NULL);
                elm_object_content_set(letterbox_bg, slideshow_image);
                evas_object_show(slideshow_image);
                INF("Showing image (no fade): %s", media_path);
            }
        } else if (is_video_file(media_path)) {
            if (slideshow_image)
                evas_object_hide(slideshow_image);
            if (slideshow_video) {
                elm_video_file_set(slideshow_video, media_path);
                elm_object_content_set(letterbox_bg, slideshow_video);
                elm_video_play(slideshow_video);
                evas_object_show(slideshow_video);
                INF("Showing video (no fade): %s", media_path);
            }
        }

        // Ensure overlay is hidden
        _ensure_fade_overlay();
        _update_fade_overlay_geometry();
        if (fade_overlay) {
            evas_object_color_set(fade_overlay, 0, 0, 0, 0);
            evas_object_hide(fade_overlay);
        }
        return;
    }

    is_fading = EINA_TRUE;
    next_media_path = strdup(media_path);
    fade_start_time = ecore_time_get();

    // Ensure and prepare overlay for crossfade
    _ensure_fade_overlay();
    _update_fade_overlay_geometry();
    if (fade_overlay) {
        // Start transparent and show overlay; animator will raise alpha
        evas_object_color_set(fade_overlay, 0, 0, 0, 0);
        evas_object_show(fade_overlay);
        // Keep overlay above content
        evas_object_raise(fade_overlay);
    }

    if (fade_animator)
        ecore_animator_del(fade_animator);

    fade_animator = ecore_animator_add(fade_animator_cb, NULL);
    // Kick off preload when fade starts to reduce stutter
    preload_next_image();
}

// Image load-ready callback: begin fade-in once display image is ready
static void _on_image_load_ready(
    void* data EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    // Detach callback to avoid repeated triggers
    evas_object_smart_callback_del(obj, "load,ready", _on_image_load_ready);
    if (!is_fading)
        return;
    if (!waiting_media_ready)
        return;

    // Proceed to fade-in phase now that image is ready
    if (next_media_path) {
        free(next_media_path);
        next_media_path = NULL;
    }
    waiting_media_ready = EINA_FALSE;
    fade_start_time = ecore_time_get();
}

// Function to show the next media in the slideshow
void show_next_media(void)
{
    char* media_path;
    int count;
    int new_index;

    count = get_media_file_count();
    if (count == 0)
        return;

    // Skip if already fading; queue request
    if (is_fading) {
        pending_nav = 1;
        return;
    }

    if (is_shuffle_mode) {
        // Random shuffle mode
        if (count == 1) {
            // Only one file, use it
            new_index = 0;
        } else {
            // Multiple files - pick a different one than current
            do {
                new_index = rand() % count;
            } while (new_index == current_media_index);
        }
    } else {
        // Sequential mode - go to next file in order
        new_index = (current_media_index + 1) % count;
    }

    current_media_index = new_index;
    media_path = get_media_path_at_index(current_media_index);
    // Update progress overlay now
    ui_progress_update_index(current_media_index, count);

    if (media_path) {
        // Start fade transition to new media
        start_fade_transition(media_path);
        // Proactively preload the subsequent image
        preload_next_image();
    }
}

// Function to show the previous media in the slideshow
void show_prev_media(void)
{
    char* media_path;
    int count;
    int new_index;

    count = get_media_file_count();
    if (count == 0)
        return;

    // Skip if already fading; queue request
    if (is_fading) {
        pending_nav = -1;
        return;
    }

    if (is_shuffle_mode) {
        // Random shuffle mode
        if (count == 1) {
            new_index = 0;
        } else {
            // Pick a different one than current
            do {
                new_index = rand() % count;
            } while (new_index == current_media_index);
        }
    } else {
        // Sequential mode - go to previous file in order
        new_index = (current_media_index - 1 + count) % count;
    }

    current_media_index = new_index;
    media_path = get_media_path_at_index(current_media_index);
    // Update progress overlay now
    ui_progress_update_index(current_media_index, count);

    if (media_path) {
        // Start fade transition to new media
        start_fade_transition(media_path);
        // Warm cache for the subsequent image
        preload_next_image();
    }
}

// Function to show media immediately (without fade, for initial load)
void show_media_immediate(const char* media_path)
{
    if (!media_path)
        return;

    printf("show_media_immediate called with: %s\n", media_path);
    printf("slideshow_image: %p, slideshow_video: %p, letterbox_bg: %p\n", slideshow_image,
        slideshow_video, letterbox_bg);

    if (is_image_file(media_path)) {
        printf("Detected as image file: %s\n", media_path);
        // Show image in letterbox
        if (slideshow_video)
            evas_object_hide(slideshow_video);
        if (slideshow_image) {
            printf("Setting image file: %s\n", media_path);
            Eina_Bool result = elm_image_file_set(slideshow_image, media_path, NULL);
            printf("elm_image_file_set result: %d\n", result);
            elm_object_content_set(letterbox_bg, slideshow_image);
            evas_object_show(slideshow_image);
            evas_object_color_set(
                slideshow_image, 255, 255, 255, 255); // Full opacity (premultiplied)
            printf("Image display setup completed\n");
            INF("Showing image: %s", media_path);
        } else {
            printf("ERROR: slideshow_image is NULL!\n");
        }
    } else if (is_video_file(media_path)) {
        // Show video in letterbox
        if (slideshow_image)
            evas_object_hide(slideshow_image);
        if (slideshow_video) {
            elm_video_file_set(slideshow_video, media_path);
            elm_object_content_set(letterbox_bg, slideshow_video);
            elm_video_play(slideshow_video);
            evas_object_show(slideshow_video);
            evas_object_color_set(
                slideshow_video, 255, 255, 255, 255); // Full opacity (premultiplied)
            INF("Showing video: %s", media_path);
        }
    }

    // Make sure overlay is hidden for immediate show
    _ensure_fade_overlay();
    _update_fade_overlay_geometry();
    if (fade_overlay) {
        evas_object_color_set(fade_overlay, 0, 0, 0, 0);
        evas_object_hide(fade_overlay);
    }

    // Update compact progress overlay for the initially shown media
    // Use current_media_index and the loaded media list count
    ui_progress_update_index(current_media_index, eina_list_count(media_files));
}

// Timer callback for automatic slideshow
Eina_Bool slideshow_timer_cb(void* data EINA_UNUSED)
{
    if (slideshow_running) {
        show_next_media();
    }
    return ECORE_CALLBACK_RENEW; // Keep the timer running
}

// Function to start/stop slideshow
void toggle_slideshow(void)
{
    slideshow_running = !slideshow_running;

    if (slideshow_running) {
        INF("Slideshow started");
        printf("Slideshow started\n");
    } else {
        INF("Slideshow paused");
        printf("Slideshow paused\n");
    }
}

// Function to toggle shuffle mode
void toggle_shuffle_mode(void)
{
    is_shuffle_mode = !is_shuffle_mode;

    if (is_shuffle_mode) {
        INF("Shuffle mode enabled");
        printf("Shuffle mode enabled\n");
    } else {
        INF("Sequential mode enabled");
        printf("Sequential mode enabled\n");
    }
}

// Slideshow initialization
void slideshow_init(Evas_Object* image_widget, Evas_Object* video_widget, Evas_Object* letterbox)
{
    printf("slideshow_init called with image: %p, video: %p, letterbox: %p\n", image_widget,
        video_widget, letterbox);
    slideshow_image = image_widget;
    slideshow_video = video_widget;
    letterbox_bg = letterbox;
    printf("slideshow_init completed - stored image: %p, video: %p, letterbox: %p\n",
        slideshow_image, slideshow_video, letterbox_bg);

    // Prepare overlay now that letterbox is available
    _ensure_fade_overlay();
    _update_fade_overlay_geometry();
}

// Start slideshow timer
void slideshow_start(void)
{
    // Initialize random seed for slideshow using Ecore time
    srand((unsigned int) ecore_time_get());

    // Load media files and start slideshow
    int media_count = get_media_file_count();

    if (media_count > 0) {
        // Show first media immediately (without fade)
        // In sequential mode, start with the first file; in shuffle, pick a random one
        if (is_shuffle_mode) {
            current_media_index = rand() % media_count;
        } else {
            current_media_index = 0; // Start with first file in sequential mode
        }

        char* first_media = get_media_path_at_index(current_media_index);
        if (first_media) {
            show_media_immediate(first_media);
        }

        // Start slideshow timer
        slideshow_timer = ecore_timer_add(slideshow_interval_runtime, slideshow_timer_cb, NULL);
        INF("Slideshow timer started with %f second interval", slideshow_interval_runtime);
    } else {
        // Show placeholder text if no images found
        if (slideshow_image)
            elm_image_file_set(slideshow_image, NULL, NULL);
        WRN("No images found - slideshow disabled");
    }
}

// Slideshow cleanup
void slideshow_cleanup(void)
{
    // Cleanup slideshow resources
    if (slideshow_timer) {
        ecore_timer_del(slideshow_timer);
        slideshow_timer = NULL;
    }

    // Cleanup fade animator
    if (fade_animator) {
        ecore_animator_del(fade_animator);
        fade_animator = NULL;
    }

    // Cleanup fade transition state
    if (next_media_path) {
        free(next_media_path);
        next_media_path = NULL;
    }

    // Stop any playing video before cleanup
    if (slideshow_video) {
        elm_video_stop(slideshow_video);
    }

    // Reset global pointers
    slideshow_image = NULL;
    slideshow_video = NULL;
    letterbox_bg = NULL;

    // Cleanup preload image
    if (preload_img) {
        evas_object_del(preload_img);
        preload_img = NULL;
    }

    // Cleanup fade overlay
    if (fade_overlay) {
        evas_object_del(fade_overlay);
        fade_overlay = NULL;
    }
}

// Convenience alias for previous navigation
void slideshow_prev(void)
{
    show_prev_media();
}