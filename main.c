#include "common.h"
#include "ui.h"
#include "media.h"
#include "slideshow.h"
#include "clock.h"

EAPI_MAIN int
elm_main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
    Evas_Object *win, *win_bg, *box;
    
    // Initialize logging
    common_init_logging();
    
    // Create main window and get the background
    win = ui_create_main_window(&win_bg);
    if (!win) {
        ERR("Failed to create main window");
        goto error;
    }
    
    // Create main container box
    box = elm_box_add(win);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    
    // CRITICAL: Set box as content of background (not directly to window)
    elm_object_content_set(win_bg, box);
    evas_object_show(box);
    
    // Setup media display
    ui_setup_media_display(box);
    
    // Create controls
    ui_create_controls(box, win);
    
    // Initialize clock (using letterbox_bg as parent)
    clock_init(letterbox_bg);
    
    // Scan for media files
    scan_media_files();
    
    // Show first media file if available
    if (get_media_file_count() > 0) {
        char *first_media = get_media_path_at_index(0);
        if (first_media) {
            show_media_immediate(first_media);
        }
    }
    
    // Start slideshow and clock
    slideshow_start();
    clock_start();
    
    // Set fullscreen and show window
    elm_win_fullscreen_set(win, EINA_TRUE);
    evas_object_resize(win, 1920, 1080);
    evas_object_show(win);
    
    // Trigger initial clock positioning
    on_letterbox_resize(NULL, NULL, letterbox_bg, NULL);
    
    // Run main loop
    INF("Starting main loop");
    elm_run();
    
    // Cleanup
    slideshow_cleanup();
    clock_cleanup();
    media_cleanup();
    ui_cleanup();
    common_cleanup_logging();
    
    return 0;
    
error:
    common_cleanup_logging();
    return -1;
}

ELM_MAIN()
