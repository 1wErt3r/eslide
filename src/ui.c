#include "ui.h"
#include "slideshow.h"
#include "clock.h"
#include "media.h"
#include "weather.h"
#include "news.h"
#include <strings.h>
#include <string.h>
#include <Ecore.h>
#include <Evas.h>

// UI state variables
Eina_Bool is_fullscreen = EINA_TRUE;
Eina_Bool controls_visible = EINA_FALSE;
Evas_Object* button_box = NULL;
static Ecore_Timer* controls_hide_timer = NULL;
static double controls_inactivity_seconds = 20.0; // auto-hide after 20s

// Progress overlay state
static Evas_Object* progress_label = NULL;
static Eina_Bool progress_visible = EINA_FALSE;

static void _progress_on_resize(
    void* data EINA_UNUSED, Evas* e EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    if (!progress_label || !obj)
        return;
    int x, y, w, h;
    evas_object_geometry_get(obj, &x, &y, &w, &h);
    // Determine the label's minimum size so it isn't 0x0
    Evas_Coord mw = 0, mh = 0;
    evas_object_size_hint_min_get(progress_label, &mw, &mh);
    if (mw <= 0)
        mw = 60;
    if (mh <= 0)
        mh = 24;
    evas_object_resize(progress_label, mw, mh);

    // Position near top-right with a 12px margin
    int margin = 12;
    int px = x + w - margin - mw;
    if (px < x + margin)
        px = x + margin;
    int py = y + margin;
    evas_object_move(progress_label, px, py);
    // Ensure overlay stays above other content
    evas_object_raise(progress_label);
}

static void _controls_hide(void)
{
    controls_visible = EINA_FALSE;
    if (button_box)
        evas_object_hide(button_box);
    INF("Controls auto-hidden due to inactivity");
}

static Eina_Bool _controls_hide_cb(void* data EINA_UNUSED)
{
    controls_hide_timer = NULL;
    _controls_hide();
    return ECORE_CALLBACK_CANCEL;
}

static void controls_reset_inactivity_timer(void)
{
    if (!controls_visible)
        return; // only when visible
    if (controls_hide_timer) {
        ecore_timer_del(controls_hide_timer);
        controls_hide_timer = NULL;
    }
    controls_hide_timer = ecore_timer_add(controls_inactivity_seconds, _controls_hide_cb, NULL);
}

// Slideshow widget variables (defined here since they're created in UI)
Evas_Object* slideshow_image = NULL;
Evas_Object* slideshow_video = NULL;
Evas_Object* letterbox_bg = NULL;

// Helper to ensure directory paths end with a trailing slash
static char* _ensure_trailing_slash(const char* path)
{
    if (!path)
        return NULL;
    size_t len = strlen(path);
    if (len == 0)
        return NULL;
    if (path[len - 1] == '/') {
        char* out = malloc(len + 1);
        if (!out)
            return NULL;
        memcpy(out, path, len + 1);
        return out;
    }
    char* out = malloc(len + 2);
    if (!out)
        return NULL;
    memcpy(out, path, len);
    out[len] = '/';
    out[len + 1] = '\0';
    return out;
}

// Double-click on letterbox to toggle fullscreen
static void on_letterbox_mouse_down(
    void* data EINA_UNUSED, Evas* e EINA_UNUSED, Evas_Object* obj, void* event_info)
{
    controls_reset_inactivity_timer();
    Evas_Event_Mouse_Down* ev = (Evas_Event_Mouse_Down*) event_info;
    if (!ev)
        return;
    // Left button double-click toggles fullscreen
    if (ev->button == 1 && (ev->flags & EVAS_BUTTON_DOUBLE_CLICK)) {
        Evas_Object* win = elm_object_top_widget_get(obj);
        if (!win)
            return;
        is_fullscreen = !is_fullscreen;
        elm_win_fullscreen_set(win, is_fullscreen);
        if (!is_fullscreen) {
            evas_object_resize(win, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
        }
        INF("Double-click: Toggle fullscreen");
    }
}

// Event handler functions
void on_done(void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
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

void on_button_click(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    INF("Slideshow toggle button clicked!");
    toggle_slideshow();
}

void on_fullscreen_click(void* data, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    Evas_Object* win = (Evas_Object*) data;

    is_fullscreen = !is_fullscreen;
    elm_win_fullscreen_set(win, is_fullscreen);

    if (is_fullscreen) {
        INF("Switched to fullscreen mode");
        printf("Switched to fullscreen mode\n");
    } else {
        // Set window size when switching to windowed mode
        evas_object_resize(win, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
        INF("Switched to windowed mode (%dx%d)", DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
        printf("Switched to windowed mode (%dx%d)\n", DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
    }
}

void on_next_image_click(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    show_next_media();
    INF("Manual next media");
    printf("Next media\n");
}

void on_prev_image_click(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    show_prev_media();
    INF("Manual previous media");
    printf("Previous media\n");
}

void on_shuffle_click(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    toggle_shuffle_mode();

    // Update button text to reflect current mode
    Evas_Object* shuffle_btn = (Evas_Object*) data;
    if (shuffle_btn) {
        if (is_shuffle_mode)
            elm_object_text_set(shuffle_btn, "Shuffle: ON");
        else
            elm_object_text_set(shuffle_btn, "Shuffle: OFF");
    }
}

void on_media_click(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info EINA_UNUSED)
{
    toggle_controls();
    // Start/reset inactivity timer when controls are shown
    controls_reset_inactivity_timer();
}

void on_clock_toggle_click(void* data EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    toggle_clock();

    // Update button text
    if (clock_visible)
        elm_object_text_set(obj, "Clock: ON");
    else
        elm_object_text_set(obj, "Clock: OFF");
}

void on_weather_toggle_click(void* data EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    weather_toggle();
    if (weather_visible)
        elm_object_text_set(obj, "Weather: ON");
    else
        elm_object_text_set(obj, "Weather: OFF");
}

void on_news_toggle_click(void* data EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    news_toggle();
    elm_object_text_set(obj, news_visible ? "News: ON" : "News: OFF");
}

// Callback for fileselector button when a folder is chosen
static void on_images_dir_chosen(
    void* data EINA_UNUSED, Evas_Object* obj EINA_UNUSED, void* event_info)
{
    const char* chosen_path = (const char*) event_info;
    if (!chosen_path || !*chosen_path)
        return;

    char* normalized = _ensure_trailing_slash(chosen_path);
    if (!normalized)
        return;

    INF("Images directory chosen: %s", normalized);
    media_set_images_dir(normalized);

    // Refresh media listing and show first item if available
    scan_media_files();
    int count = get_media_file_count();
    if (count > 0) {
        current_media_index = 0;
        char* first = get_media_path_at_index(0);
        if (first) {
            show_media_immediate(first);
        }
        ui_progress_update_index(current_media_index, count);
    } else {
        WRN("No media found in selected directory");
    }
}


// Progress overlay toggle button handler
static void on_progress_toggle_click(
    void* data EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    controls_reset_inactivity_timer();
    progress_visible = !progress_visible;
    ui_progress_set_visible(progress_visible);
    elm_object_text_set(obj, progress_visible ? "Progress: ON" : "Progress: OFF");
}



// Function to toggle control visibility
void toggle_controls(void)
{
    controls_visible = !controls_visible;

    if (button_box) {
        if (controls_visible) {
            evas_object_show(button_box);
            INF("Controls shown");
            controls_reset_inactivity_timer();
        } else {
            evas_object_hide(button_box);
            INF("Controls hidden");
            if (controls_hide_timer) {
                ecore_timer_del(controls_hide_timer);
                controls_hide_timer = NULL;
            }
        }
    }
}

// UI creation and setup functions
Evas_Object* ui_create_main_window(Evas_Object** win_bg_out)
{
    Evas_Object* win;

    // Set application policy
    elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

    // Create main window
    win = elm_win_util_standard_add("eslide", "eslide");
    evas_object_smart_callback_add(win, "delete,request", on_done, NULL);

    // Set a reasonable default window size
    evas_object_resize(win, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

    // Set window background to black to match letterboxing
    Evas_Object* win_bg = elm_bg_add(win);
    elm_bg_color_set(win_bg, 0, 0, 0); // Black background
    evas_object_size_hint_weight_set(win_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_win_resize_object_add(win, win_bg);
    evas_object_show(win_bg);

    // Return the background via output parameter
    if (win_bg_out)
        *win_bg_out = win_bg;

    INF("Main window created");

    return win;
}

void ui_setup_media_display(Evas_Object* parent_box)
{
    // Create letterbox background container
    letterbox_bg = elm_bg_add(parent_box);
    printf("Created letterbox_bg: %p\n", letterbox_bg);
    elm_bg_color_set(letterbox_bg, 0, 0, 0); // Black background
    evas_object_size_hint_weight_set(letterbox_bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(letterbox_bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(letterbox_bg, "clicked", on_media_click, NULL);
    // Reset inactivity timer on mouse movement and clicks
    evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_MOUSE_MOVE,
        (Evas_Object_Event_Cb) controls_reset_inactivity_timer, NULL);
    evas_object_event_callback_add(
        letterbox_bg, EVAS_CALLBACK_MOUSE_DOWN, on_letterbox_mouse_down, NULL);
    elm_box_pack_end(parent_box, letterbox_bg);
    evas_object_show(letterbox_bg);
    printf("Letterbox_bg shown\n");

    // Create image widget for slideshow with letterbox effect
    slideshow_image = elm_image_add(letterbox_bg);
    printf("Created slideshow_image: %p\n", slideshow_image);
    elm_image_aspect_fixed_set(slideshow_image, EINA_TRUE);  // Maintain aspect ratio
    elm_image_fill_outside_set(slideshow_image, EINA_FALSE); // Don't fill outside bounds
    elm_image_resizable_set(slideshow_image, EINA_TRUE, EINA_TRUE);
    elm_image_smooth_set(slideshow_image, EINA_TRUE);
    evas_object_size_hint_weight_set(slideshow_image, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(slideshow_image, 0.5, 0.5); // Center the image
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
    evas_object_size_hint_align_set(slideshow_video, 0.5, 0.5); // Center the video
    evas_object_smart_callback_add(slideshow_video, "clicked", on_media_click, NULL);
    evas_object_hide(slideshow_video); // Initially hidden

    // Set up resize callback for letterbox to reposition clock and weather
    evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_RESIZE, on_letterbox_resize, NULL);
    evas_object_event_callback_add(
        letterbox_bg, EVAS_CALLBACK_RESIZE, on_letterbox_resize_weather, NULL);
    evas_object_event_callback_add(
        letterbox_bg, EVAS_CALLBACK_RESIZE, on_letterbox_resize_news, NULL);
    // Set up resize callback to keep progress overlay anchored
    evas_object_event_callback_add(letterbox_bg, EVAS_CALLBACK_RESIZE, _progress_on_resize, NULL);

    // Create compact progress label overlay (hidden by default)
    progress_label = elm_label_add(letterbox_bg);
    elm_object_text_set(progress_label, "");
    evas_object_color_set(progress_label, 255, 255, 255, 255);
    // Give the label a sensible minimum size so it is visible when shown
    evas_object_size_hint_min_set(progress_label, 60, 24);
    evas_object_hide(progress_label);
    // Initial position calculation
    _progress_on_resize(NULL, NULL, letterbox_bg, NULL);

    // Initialize slideshow with the created widgets
    slideshow_init(slideshow_image, slideshow_video, letterbox_bg);
}

void ui_create_controls(Evas_Object* parent_box, Evas_Object* win)
{
    // Create horizontal box for control buttons
    button_box = elm_box_add(win);
    elm_box_horizontal_set(button_box, EINA_TRUE);
    elm_box_homogeneous_set(button_box, EINA_TRUE);
    evas_object_size_hint_weight_set(button_box, EVAS_HINT_EXPAND, 0.0);
    evas_object_size_hint_align_set(button_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(parent_box, button_box);
    evas_object_hide(button_box); // Initially hidden

    // Create Toggle Slideshow button (UTF-8 play/pause symbol)
    Evas_Object* toggle_btn = elm_button_add(win);
    elm_object_text_set(toggle_btn, "⏯");
    evas_object_smart_callback_add(toggle_btn, "clicked", on_button_click, NULL);
    evas_object_size_hint_weight_set(toggle_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(toggle_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, toggle_btn);
    evas_object_show(toggle_btn);

    // Create Previous Image button (UTF-8 left arrow)
    Evas_Object* prev_btn = elm_button_add(win);
    elm_object_text_set(prev_btn, "◀");
    evas_object_smart_callback_add(prev_btn, "clicked", on_prev_image_click, NULL);
    evas_object_size_hint_weight_set(prev_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(prev_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, prev_btn);
    evas_object_show(prev_btn);

    // Create Next Image button (UTF-8 right arrow)
    Evas_Object* next_btn = elm_button_add(win);
    elm_object_text_set(next_btn, "▶");
    evas_object_smart_callback_add(next_btn, "clicked", on_next_image_click, NULL);
    evas_object_size_hint_weight_set(next_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(next_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, next_btn);
    evas_object_show(next_btn);

    // Create Shuffle Toggle button
    Evas_Object* shuffle_btn = elm_button_add(win);
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
    Evas_Object* clock_btn = elm_button_add(win);
    if (clock_visible)
        elm_object_text_set(clock_btn, "Clock: ON");
    else
        elm_object_text_set(clock_btn, "Clock: OFF");
    evas_object_smart_callback_add(clock_btn, "clicked", on_clock_toggle_click, clock_btn);
    evas_object_size_hint_weight_set(clock_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(clock_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, clock_btn);
    evas_object_show(clock_btn);

    // Create Weather Toggle button
    Evas_Object* weather_btn = elm_button_add(win);
    if (weather_visible)
        elm_object_text_set(weather_btn, "Weather: ON");
    else
        elm_object_text_set(weather_btn, "Weather: OFF");
    evas_object_smart_callback_add(weather_btn, "clicked", on_weather_toggle_click, weather_btn);
    evas_object_size_hint_weight_set(weather_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(weather_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, weather_btn);
    evas_object_show(weather_btn);

    // Create News Toggle button
    Evas_Object* news_btn = elm_button_add(win);
    elm_object_text_set(news_btn, news_visible ? "News: ON" : "News: OFF");
    evas_object_smart_callback_add(news_btn, "clicked", on_news_toggle_click, news_btn);
    evas_object_size_hint_weight_set(news_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(news_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, news_btn);
    evas_object_show(news_btn);

    // Create Progress overlay toggle button
    Evas_Object* progress_btn = elm_button_add(win);
    elm_object_text_set(progress_btn, "Progress: OFF");
    evas_object_smart_callback_add(progress_btn, "clicked", on_progress_toggle_click, progress_btn);
    evas_object_size_hint_weight_set(progress_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(progress_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, progress_btn);
    evas_object_show(progress_btn);

    // Create Images Directory picker button (folder-only)
    Evas_Object* dir_btn = elm_fileselector_button_add(win);
    elm_object_text_set(dir_btn, "Folder…");
    elm_fileselector_button_folder_only_set(dir_btn, EINA_TRUE);
    // Prefer opening in an inner window to keep context
    elm_fileselector_button_inwin_mode_set(dir_btn, EINA_TRUE);
    // Start from current images directory if available
    const char* start_dir = media_get_images_dir();
    if (start_dir)
        elm_fileselector_button_path_set(dir_btn, start_dir);
    evas_object_smart_callback_add(dir_btn, "file,chosen", on_images_dir_chosen, NULL);
    evas_object_size_hint_weight_set(dir_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(dir_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, dir_btn);
    evas_object_show(dir_btn);

    // Create Fullscreen button
    Evas_Object* fullscreen_btn = elm_button_add(win);
    elm_object_text_set(fullscreen_btn, "Fullscreen");
    evas_object_smart_callback_add(fullscreen_btn, "clicked", on_fullscreen_click, win);
    evas_object_size_hint_weight_set(fullscreen_btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(fullscreen_btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(button_box, fullscreen_btn);
    evas_object_show(fullscreen_btn);
}

// UI initialization and cleanup
void ui_init(void)
{
    // UI initialization is handled by the individual creation functions
}

void ui_cleanup(void)
{
    // Reset global pointers
    button_box = NULL;
}

// Progress overlay API implementations
void ui_progress_update_index(int index, int count)
{
    if (!progress_label)
        return;
    if (index < 0 || count <= 0)
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d", index + 1, count);
    elm_object_text_set(progress_label, buf);
    if (progress_visible)
        evas_object_show(progress_label);
}

Eina_Bool ui_is_fullscreen(void)
{
    return is_fullscreen;
}

void ui_progress_set_visible(Eina_Bool visible)
{
    progress_visible = visible;
    if (!progress_label)
        return;
    if (visible) {
        _progress_on_resize(NULL, NULL, letterbox_bg, NULL);
        evas_object_raise(progress_label);
        // Ensure text is up-to-date the moment it is shown
        ui_progress_update_index(current_media_index, get_media_file_count());
        evas_object_show(progress_label);
    } else {
        evas_object_hide(progress_label);
    }
}