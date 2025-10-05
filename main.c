#include "common.h"
#include "ui.h"
#include "media.h"
#include "slideshow.h"
#include "clock.h"
#include "config.h"

EAPI_MAIN int
elm_main(int argc, char **argv)
{
    Evas_Object *win, *win_bg, *box;
    App_Config cfg;
    
    // Initialize logging
    common_init_logging();
    
    // Parse command-line options (defaults preserved) and log them
    cfg = config_parse(argc, argv);
    config_log(&cfg);

    // Apply initial UI state from parsed config before creating controls
    // Ensure clock toggle reflects --clock/--no-clock at startup
    clock_visible = cfg.clock_visible;
    // Ensure shuffle button initial label matches CLI
    is_shuffle_mode = cfg.shuffle;
    // Set clock format (12/24-hour) from CLI
    clock_set_24h(cfg.clock_24h);

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
    
    // Set configurable images directory before scanning
    media_set_images_dir(cfg.images_dir);
    // Scan for media files
    scan_media_files();
    
    // Show first media file if available
    if (get_media_file_count() > 0) {
        char *first_media = get_media_path_at_index(0);
        if (first_media) {
            show_media_immediate(first_media);
        }
    }
    
    // Apply runtime slideshow tuning from config, then start
    slideshow_set_interval(cfg.slideshow_interval);
    slideshow_set_fade_duration(cfg.fade_duration);
    // Start slideshow and clock
    slideshow_start();
    clock_start();
    
    // Set fullscreen from config and show window
    elm_win_fullscreen_set(win, cfg.fullscreen);
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
