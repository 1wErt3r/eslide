#include <string.h>
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

    Ecore_Getopt_Value values[]
        = { ECORE_GETOPT_VALUE_DOUBLE(interval), ECORE_GETOPT_VALUE_DOUBLE(fade),
              ECORE_GETOPT_VALUE_STR(images_dir), ECORE_GETOPT_VALUE_BOOL(fullscreen),
              ECORE_GETOPT_VALUE_BOOL(fullscreen), ECORE_GETOPT_VALUE_BOOL(shuffle),
              ECORE_GETOPT_VALUE_BOOL(shuffle), ECORE_GETOPT_VALUE_BOOL(clock),
              ECORE_GETOPT_VALUE_BOOL(clock), ECORE_GETOPT_VALUE_BOOL(clock_24h),
              ECORE_GETOPT_VALUE_BOOL(clock_24h), ECORE_GETOPT_VALUE_BOOL(weather),
              ECORE_GETOPT_VALUE_BOOL(weather), ECORE_GETOPT_VALUE_STR(weather_station),
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
}

// Retain original API for callers expecting a full parse from defaults
App_Config config_parse(int argc, char** argv)
{
    App_Config cfg = config_defaults();
    config_merge_cli(&cfg, argc, argv);
    return cfg;
}

void config_log(const App_Config* cfg)
{
    if (!cfg) {
        return;
    }
    INF("Config: interval=%.2f s, fade=%.2f s, images_dir=%s, fullscreen=%s, shuffle=%s, clock=%s, "
        "clock_format=%s, weather=%s, station=%s",
        cfg->slideshow_interval, cfg->fade_duration, cfg->images_dir ? cfg->images_dir : "(null)",
        cfg->fullscreen ? "true" : "false", cfg->shuffle ? "true" : "false",
        cfg->clock_visible ? "true" : "false", cfg->clock_24h ? "24h" : "12h",
        cfg->weather_visible ? "true" : "false",
        cfg->weather_station ? cfg->weather_station : "(null)");
}