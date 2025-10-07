#include "common.h"
#include "ui.h"
#include "media.h"
#include "slideshow.h"
#include "clock.h"
#include "weather.h"
#include "news.h"
#include "config.h"


EAPI_MAIN int elm_main(int argc, char** argv)
{
    Evas_Object *win, *win_bg, *box;
    const char* cfg_path = "./eslide.cfg";

    // Check for help/version arguments early - exit before any UI initialization
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0
            || strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            // Let config_merge_cli handle the help/version display and exit
            App_Config temp_cfg = config_defaults();
            config_merge_cli(&temp_cfg, argc, argv);
            return 0; // This won't be reached due to exit() in ecore_getopt_parse
        }
    }

    // Initialize logging
    common_init_logging();

    // Parse command line arguments first - this will exit for --help/--version
    App_Config cfg = config_defaults();

    // Initialize Eet and load persisted config (same folder as executable)
    config_eet_init();
    App_Config loaded;
    if (config_load_from_eet(&loaded, cfg_path)) {
        cfg = loaded;
        INF("Loaded persisted configuration");
    }

    // Apply command line arguments over loaded/default config
    config_merge_cli(&cfg, argc, argv);
    config_log(&cfg);

    // Apply initial UI state from parsed config before creating controls
    // Ensure clock toggle reflects --clock/--no-clock at startup
    clock_visible = cfg.clock_visible;
    // Ensure shuffle button initial label matches CLI
    is_shuffle_mode = cfg.shuffle;
    // Set clock format (12/24-hour) from CLI
    clock_set_24h(cfg.clock_24h);
    // Ensure weather toggle reflects --weather/--no-weather at startup
    weather_visible = cfg.weather_visible;

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
    // Initialize weather overlay (using letterbox_bg as parent)
    weather_init(letterbox_bg);
    // Initialize news overlay (using letterbox_bg as parent)
    news_init(letterbox_bg);
    // Configure NOAA station from config
    weather_set_station(cfg.weather_station);

    // Set configurable images directory before scanning
    media_set_images_dir(cfg.images_dir);
    // Scan for media files
    scan_media_files();

    // Show first media file if available
    if (get_media_file_count() > 0) {
        char* first_media = get_media_path_at_index(0);
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
    // Start weather polling
    weather_start();
    // Start news polling and rotation
    news_start();

    // Set fullscreen from config and show window
    elm_win_fullscreen_set(win, cfg.fullscreen);
    evas_object_show(win);

    // Quotes overlay is OFF by default; enable via UI toggle

    // Trigger initial clock positioning
    on_letterbox_resize(NULL, NULL, letterbox_bg, NULL);
    // Trigger initial weather positioning
    on_letterbox_resize_weather(NULL, NULL, letterbox_bg, NULL);
    // Trigger initial news positioning
    on_letterbox_resize_news(NULL, NULL, letterbox_bg, NULL);

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
    cfg.weather_visible = weather_visible;
    config_save_to_eet(&cfg, cfg_path);

    // Cleanup
    slideshow_cleanup();
    clock_cleanup();
    weather_cleanup();
    news_cleanup();
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
