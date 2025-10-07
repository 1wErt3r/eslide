#include "common.h"
#include "ui.h"
#include "media.h"
#include "slideshow.h"
#include "clock.h"
#include "config.h"
#include "net.h"

// Forward declaration to satisfy C99 when header guards collide
void net_set_station(const char *station_id);

EAPI_MAIN int
elm_main(int argc, char **argv)
{
    Evas_Object *win, *win_bg, *box;
    App_Config cfg;
    const char *cfg_path = "./eslide.cfg";
    
    // Initialize logging
    common_init_logging();
    
    // Initialize Eet and load persisted config (same folder as executable)
    config_eet_init();
    cfg = config_defaults();
    App_Config loaded;
    if (config_load_from_eet(&loaded, cfg_path)) {
        cfg = loaded;
        INF("Loaded persisted configuration");
    }

    // Merge command-line options over loaded/default config and log
    config_merge_cli(&cfg, argc, argv);
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
    // Initialize network overlay label
    net_init(letterbox_bg);
    // Apply CLI-configured weather location, if provided
    if (cfg.weather_location && *cfg.weather_location)
        net_set_station(cfg.weather_location);
    
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

    // Quotes overlay is OFF by default; enable via UI toggle
    
    // Trigger initial clock positioning
    on_letterbox_resize(NULL, NULL, letterbox_bg, NULL);
    
    // Run main loop
    INF("Starting main loop");
    elm_run();
    
    // Before cleanup, persist current settings to Eet file
    // Capture latest runtime values from modules
    cfg.slideshow_interval = slideshow_get_interval();
    cfg.fade_duration = slideshow_get_fade_duration();
    cfg.fullscreen = ui_is_fullscreen();
    cfg.shuffle = is_shuffle_mode;
    cfg.clock_visible = clock_visible;
    cfg.clock_24h = clock_is_24h;
    config_save_to_eet(&cfg, cfg_path);

    // Cleanup
    slideshow_cleanup();
    clock_cleanup();
    net_cleanup();
    media_cleanup();
    ui_cleanup();
    config_eet_shutdown();
    common_cleanup_logging();
    
    return 0;
    
error:
    config_eet_shutdown();
    common_cleanup_logging();
    return -1;
}

ELM_MAIN()
