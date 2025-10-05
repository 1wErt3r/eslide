#include <stdio.h>
#include <string.h>
#include <Ecore.h>
#include <Ecore_Getopt.h>
#include "common.h"
#include "config.h"

App_Config
config_defaults(void)
{
    App_Config cfg;
    cfg.slideshow_interval = SLIDESHOW_INTERVAL;
    cfg.fade_duration = FADE_DURATION;
    cfg.images_dir = IMAGES_DIR;
    cfg.fullscreen = EINA_TRUE;     // matches current UI default
    cfg.shuffle = EINA_FALSE;       // matches slideshow default
    cfg.clock_visible = EINA_FALSE; // matches clock default
    cfg.clock_24h = EINA_FALSE;     // default to 12-hour time
    return cfg;
}

static const Ecore_Getopt _opts = {
    .prog = "eslide",
    .usage = "Usage: eslide [options]",
    .version = NULL,
    .copyright = NULL,
    .license = NULL,
    .strict = EINA_TRUE,
    .descs = {
        ECORE_GETOPT_STORE_DOUBLE('i', "interval", "Seconds between transitions."),
        ECORE_GETOPT_STORE_DOUBLE('f', "fade", "Fade transition duration (seconds)."),
        ECORE_GETOPT_STORE_STR   ('d', "images-dir", "Directory with media files."),
        ECORE_GETOPT_STORE_TRUE  ('F', "fullscreen", "Start in fullscreen mode."),
        ECORE_GETOPT_STORE_FALSE (0,   "no-fullscreen", "Do not start in fullscreen."),
        ECORE_GETOPT_STORE_TRUE  ('s', "shuffle", "Enable shuffle mode."),
        ECORE_GETOPT_STORE_FALSE (0,   "no-shuffle", "Disable shuffle mode."),
        ECORE_GETOPT_STORE_TRUE  ('c', "clock", "Show clock overlay."),
        ECORE_GETOPT_STORE_FALSE (0,   "no-clock", "Hide clock overlay."),
        ECORE_GETOPT_STORE_TRUE  (0,   "clock-24h", "Use 24-hour time format (default is 12-hour)."),
        ECORE_GETOPT_STORE_FALSE (0,   "clock-12h", "Use 12-hour time format."),
        ECORE_GETOPT_VERSION     ('V', "version"),
        ECORE_GETOPT_HELP        ('h', "help"),
        ECORE_GETOPT_SENTINEL
    }
};

App_Config
config_parse(int argc, char **argv)
{
    App_Config cfg = config_defaults();

    double interval = cfg.slideshow_interval;
    double fade = cfg.fade_duration;
    char *images_dir = (char *)cfg.images_dir;
    Eina_Bool fullscreen = cfg.fullscreen;
    Eina_Bool shuffle = cfg.shuffle;
    Eina_Bool clock = cfg.clock_visible;
    Eina_Bool clock_24h = cfg.clock_24h;

    Ecore_Getopt_Value values[] = {
        ECORE_GETOPT_VALUE_DOUBLE(interval),
        ECORE_GETOPT_VALUE_DOUBLE(fade),
        ECORE_GETOPT_VALUE_STR(images_dir),
        ECORE_GETOPT_VALUE_BOOL(fullscreen),
        ECORE_GETOPT_VALUE_BOOL(fullscreen),
        ECORE_GETOPT_VALUE_BOOL(shuffle),
        ECORE_GETOPT_VALUE_BOOL(shuffle),
        ECORE_GETOPT_VALUE_BOOL(clock),
        ECORE_GETOPT_VALUE_BOOL(clock),
        ECORE_GETOPT_VALUE_BOOL(clock_24h),
        ECORE_GETOPT_VALUE_BOOL(clock_24h),
        ECORE_GETOPT_VALUE_NONE, // version handled by Ecore_Getopt
        ECORE_GETOPT_VALUE_NONE, // help handled by Ecore_Getopt
        ECORE_GETOPT_VALUE_NONE
    };

    int args = ecore_getopt_parse(&_opts, values, argc, argv);
    if (args < 0)
    {
        WRN("Failed to parse command-line options");
        return cfg; // return defaults
    }

    // Update cfg with parsed values
    cfg.slideshow_interval = interval;
    cfg.fade_duration = fade;
    if (images_dir) cfg.images_dir = images_dir;
    cfg.fullscreen = fullscreen;
    cfg.shuffle = shuffle;
    cfg.clock_visible = clock;
    cfg.clock_24h = clock_24h;

    return cfg;
}

void
config_log(const App_Config *cfg)
{
    if (!cfg) return;
    INF("Config: interval=%.2f s, fade=%.2f s, images_dir=%s, fullscreen=%s, shuffle=%s, clock=%s, clock_format=%s",
        cfg->slideshow_interval,
        cfg->fade_duration,
        cfg->images_dir ? cfg->images_dir : "(null)",
        cfg->fullscreen ? "true" : "false",
        cfg->shuffle ? "true" : "false",
        cfg->clock_visible ? "true" : "false",
        cfg->clock_24h ? "24h" : "12h");
}