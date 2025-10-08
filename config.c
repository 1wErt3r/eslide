#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <Ecore.h>
#include <Ecore_Getopt.h>
#include <Eet.h>
#include "common.h"
#include "config.h"

App_Config config_defaults(void)
{
    App_Config cfg;
    cfg.slideshow_interval = SLIDESHOW_INTERVAL;
    cfg.fade_duration = FADE_DURATION;
    cfg.images_dir = IMAGES_DIR;
    cfg.fullscreen = EINA_TRUE;       // matches current UI default
    cfg.shuffle = EINA_FALSE;         // matches slideshow default
    cfg.clock_visible = EINA_FALSE;   // matches clock default
    cfg.clock_24h = EINA_FALSE;       // default to 12-hour time
    cfg.weather_visible = EINA_FALSE; // weather overlay hidden by default
    cfg.weather_station = "KNYC";     // default NOAA station
    cfg.news_visible = EINA_FALSE;    // news overlay hidden by default
    return cfg;
}

static const Ecore_Getopt _opts = { .prog = "eslide",
    .usage = "Usage: eslide [options]",
    .version = "1.0.0",
    .copyright = NULL,
    .license = NULL,
    .strict = EINA_TRUE,
    .descs = { ECORE_GETOPT_STORE_DOUBLE('i', "interval", "Seconds between transitions."),
        ECORE_GETOPT_STORE_DOUBLE('f', "fade", "Fade transition duration (seconds)."),
        ECORE_GETOPT_STORE_STR('d', "images-dir", "Directory with media files."),
        ECORE_GETOPT_STORE_TRUE('F', "fullscreen", "Start in fullscreen mode."),
        ECORE_GETOPT_STORE_FALSE(0, "no-fullscreen", "Do not start in fullscreen."),
        ECORE_GETOPT_STORE_TRUE('s', "shuffle", "Enable shuffle mode."),
        ECORE_GETOPT_STORE_FALSE(0, "no-shuffle", "Disable shuffle mode."),
        ECORE_GETOPT_STORE_TRUE('c', "clock", "Show clock overlay."),
        ECORE_GETOPT_STORE_FALSE(0, "no-clock", "Hide clock overlay."),
        ECORE_GETOPT_STORE_TRUE(0, "clock-24h", "Use 24-hour time format (default is 12-hour)."),
        ECORE_GETOPT_STORE_FALSE(0, "clock-12h", "Use 12-hour time format."),
        ECORE_GETOPT_STORE_TRUE(0, "weather", "Show weather overlay."),
        ECORE_GETOPT_STORE_FALSE(0, "no-weather", "Hide weather overlay."),
        ECORE_GETOPT_STORE_STR(0, "weather-station", "NOAA station code (e.g., KNYC)."),
        ECORE_GETOPT_STORE_TRUE(0, "news", "Show news overlay."),
        ECORE_GETOPT_STORE_FALSE(0, "no-news", "Hide news overlay."),

        ECORE_GETOPT_VERSION('V', "version"), ECORE_GETOPT_HELP('h', "help"),
        ECORE_GETOPT_SENTINEL } };

// Eet data descriptor for App_Config
static Eet_Data_Descriptor* _cfg_edd = NULL;

static void _config_edd_setup(void)
{
    if (_cfg_edd) {
        return;
    }
    Eet_Data_Descriptor_Class eddc;
    EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, App_Config);
    _cfg_edd = eet_data_descriptor_stream_new(&eddc);
    if (!_cfg_edd) {
        ERR("Failed to create Eet data descriptor for App_Config");
        return;
    }
    EET_DATA_DESCRIPTOR_ADD_BASIC(
        _cfg_edd, App_Config, "slideshow_interval", slideshow_interval, EET_T_DOUBLE);
    EET_DATA_DESCRIPTOR_ADD_BASIC(
        _cfg_edd, App_Config, "fade_duration", fade_duration, EET_T_DOUBLE);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "images_dir", images_dir, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "fullscreen", fullscreen, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "shuffle", shuffle, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "clock_visible", clock_visible, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "clock_24h", clock_24h, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(
        _cfg_edd, App_Config, "weather_visible", weather_visible, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(
        _cfg_edd, App_Config, "weather_station", weather_station, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(_cfg_edd, App_Config, "news_visible", news_visible, EET_T_INT);
}

void config_eet_init(void)
{
    if (eet_init() == 0) {
        INF("Eet initialized");
    }
    _config_edd_setup();
}

void config_eet_shutdown(void)
{
    if (_cfg_edd) {
        eet_data_descriptor_free(_cfg_edd);
        _cfg_edd = NULL;
    }
    eet_shutdown();
}

Eina_Bool config_load_from_eet(App_Config* out_cfg, const char* path)
{
    if (!out_cfg || !path) {
        return EINA_FALSE;
    }
    _config_edd_setup();
    Eet_File* ef = eet_open(path, EET_FILE_MODE_READ);
    if (!ef) {
        INF("No config file at %s; using defaults", path);
        return EINA_FALSE;
    }
    App_Config* read_cfg = eet_data_read(ef, _cfg_edd, "config");
    eet_close(ef);
    if (!read_cfg) {
        WRN("Failed to read config from %s", path);
        return EINA_FALSE;
    }
    *out_cfg = *read_cfg;
    free(read_cfg);
    return EINA_TRUE;
}

Eina_Bool config_save_to_eet(const App_Config* cfg, const char* path)
{
    if (!cfg || !path) {
        return EINA_FALSE;
    }
    _config_edd_setup();
    Eet_File* ef = eet_open(path, EET_FILE_MODE_WRITE);
    if (!ef) {
        ERR("Failed to open %s for writing", path);
        return EINA_FALSE;
    }
    int write_success = eet_data_write(ef, _cfg_edd, "config", cfg, EET_COMPRESSION_DEFAULT);
    eet_close(ef);
    if (!write_success) {
        ERR("Failed to write config to %s", path);
        return EINA_FALSE;
    }
    INF("Config saved to %s", path);
    return EINA_TRUE;
}

// Parse command-line arguments, merging over an existing cfg in-place
void config_merge_cli(App_Config* cfg, int argc, char** argv)
{
    if (!cfg) {
        return;
    }

    double interval = cfg->slideshow_interval;
    double fade = cfg->fade_duration;
    char* images_dir = (char*) cfg->images_dir;
    Eina_Bool fullscreen = cfg->fullscreen;
    Eina_Bool shuffle = cfg->shuffle;
    Eina_Bool clock = cfg->clock_visible;
    Eina_Bool clock_24h = cfg->clock_24h;
    Eina_Bool weather = cfg->weather_visible;
    char* weather_station = (char*) cfg->weather_station;
    Eina_Bool news = cfg->news_visible;

    Ecore_Getopt_Value values[]
        = { ECORE_GETOPT_VALUE_DOUBLE(interval), ECORE_GETOPT_VALUE_DOUBLE(fade),
              ECORE_GETOPT_VALUE_STR(images_dir), ECORE_GETOPT_VALUE_BOOL(fullscreen),
              ECORE_GETOPT_VALUE_BOOL(fullscreen), ECORE_GETOPT_VALUE_BOOL(shuffle),
              ECORE_GETOPT_VALUE_BOOL(shuffle), ECORE_GETOPT_VALUE_BOOL(clock),
              ECORE_GETOPT_VALUE_BOOL(clock), ECORE_GETOPT_VALUE_BOOL(clock_24h),
              ECORE_GETOPT_VALUE_BOOL(clock_24h), ECORE_GETOPT_VALUE_BOOL(weather),
              ECORE_GETOPT_VALUE_BOOL(weather), ECORE_GETOPT_VALUE_STR(weather_station),
              ECORE_GETOPT_VALUE_BOOL(news), ECORE_GETOPT_VALUE_BOOL(news),
              ECORE_GETOPT_VALUE_NONE, // version handled by Ecore_Getopt
              ECORE_GETOPT_VALUE_NONE, // help handled by Ecore_Getopt
              ECORE_GETOPT_VALUE_NONE };

    int args = ecore_getopt_parse(&_opts, values, argc, argv);
    if (args < 0) {
        WRN("Failed to parse command-line options");
        return; // leave cfg unchanged on parse failure
    }

    // Merge back into cfg
    cfg->slideshow_interval = interval;
    cfg->fade_duration = fade;
    if (images_dir) {
        cfg->images_dir = images_dir;
    }
    cfg->fullscreen = fullscreen;
    cfg->shuffle = shuffle;
    cfg->clock_visible = clock;
    cfg->clock_24h = clock_24h;
    cfg->weather_visible = weather;
    if (weather_station) {
        cfg->weather_station = weather_station;
    }
    cfg->news_visible = news;
}

// Retain original API for callers expecting a full parse from defaults
App_Config config_parse(int argc, char** argv)
{
    App_Config cfg = config_defaults();
    config_merge_cli(&cfg, argc, argv);
    return cfg;
}

// Get XDG config path following XDG Base Directory Specification
char* config_get_xdg_config_path(const char* app_name, const char* filename)
{
    if (!app_name || !filename) return NULL;
    
    const char* xdg_config_home = getenv("XDG_CONFIG_HOME");
    char* config_path = NULL;
    
    if (xdg_config_home && *xdg_config_home) {
        // Use XDG_CONFIG_HOME if set
        config_path = malloc(strlen(xdg_config_home) + strlen(app_name) + strlen(filename) + 3);
        if (config_path) {
            sprintf(config_path, "%s/%s/%s", xdg_config_home, app_name, filename);
        }
    } else {
        // Fallback to ~/.config/app_name/filename
        const char* home = getenv("HOME");
        if (home && *home) {
            config_path = malloc(strlen(home) + strlen("/.config/") + strlen(app_name) + strlen(filename) + 3);
            if (config_path) {
                sprintf(config_path, "%s/.config/%s/%s", home, app_name, filename);
            }
        }
    }
    
    return config_path;
}

// Get config path with backwards compatibility fallback
char* config_get_config_path_with_fallback(const char* app_name, const char* filename)
{
    if (!app_name || !filename) return NULL;
    
    // First try XDG path
    char* xdg_path = config_get_xdg_config_path(app_name, filename);
    if (xdg_path) {
        // Check if XDG config exists
        if (access(xdg_path, F_OK) == 0) {
            return xdg_path;
        }
        
        // Check if XDG directory exists, if not create it
        char* dir_end = strrchr(xdg_path, '/');
        if (dir_end) {
            *dir_end = '\0'; // Temporarily null-terminate at directory
            struct stat st;
            if (stat(xdg_path, &st) != 0) {
                // Directory doesn't exist, try to create it
                char* mkdir_cmd = malloc(strlen("mkdir -p ") + strlen(xdg_path) + 1);
                if (mkdir_cmd) {
                    sprintf(mkdir_cmd, "mkdir -p %s", xdg_path);
                    int result = system(mkdir_cmd);
                    free(mkdir_cmd);
                    if (result != 0) {
                        WRN("Failed to create XDG config directory: %s", xdg_path);
                    }
                }
            }
            *dir_end = '/'; // Restore the slash
        }
        
        // XDG path doesn't exist yet, but we'll use it for new configs
        free(xdg_path);
    }
    
    // Check for backwards compatibility - config file in current working directory
    // This provides backwards compatibility with the old behavior
    char* legacy_path = malloc(2 + strlen(filename) + 1);
    if (legacy_path) {
        sprintf(legacy_path, "./%s", filename);
        
        // If legacy config exists in current directory, use it (backwards compatibility)
        if (access(legacy_path, F_OK) == 0) {
            return legacy_path;
        }
        
        free(legacy_path);
    }
    
    // No legacy config found, return XDG path (or NULL if XDG not available)
    return config_get_xdg_config_path(app_name, filename);
}

void config_log(const App_Config* cfg)
{
    if (!cfg) {
        return;
    }
    INF("Config: interval=%.2f s, fade=%.2f s, images_dir=%s, fullscreen=%s, shuffle=%s, clock=%s, "
        "clock_format=%s, weather=%s, station=%s, news=%s",
        cfg->slideshow_interval, cfg->fade_duration, cfg->images_dir ? cfg->images_dir : "(null)",
        cfg->fullscreen ? "true" : "false", cfg->shuffle ? "true" : "false",
        cfg->clock_visible ? "true" : "false", cfg->clock_24h ? "24h" : "12h",
        cfg->weather_visible ? "true" : "false",
        cfg->weather_station ? cfg->weather_station : "(null)",
        cfg->news_visible ? "true" : "false");
}