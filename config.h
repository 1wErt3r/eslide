#ifndef CONFIG_H
#define CONFIG_H

#include <Eina.h>

typedef struct {
    double slideshow_interval;
    double fade_duration;
    const char* images_dir;
    Eina_Bool fullscreen;
    Eina_Bool shuffle;
    Eina_Bool clock_visible;
    Eina_Bool clock_24h;         // false = 12-hour (default), true = 24-hour
    Eina_Bool weather_visible;   // weather overlay visibility
    const char* weather_station; // NOAA station code (e.g., KNYC)
    Eina_Bool news_visible;      // news overlay visibility
} App_Config;

// Initialize defaults from compile-time constants and current module defaults
App_Config config_defaults(void);

// Parse command-line arguments with Ecore_Getopt, preserving defaults
App_Config config_parse(int argc, char** argv);

// Log parsed configuration values for debugging/visibility
void config_log(const App_Config* cfg);

// Eet persistence API
void config_eet_init(void);
void config_eet_shutdown(void);
// Load config from Eet file at path; returns EINA_TRUE on success
Eina_Bool config_load_from_eet(App_Config* out_cfg, const char* path);
// Save config to Eet file at path; returns EINA_TRUE on success
Eina_Bool config_save_to_eet(const App_Config* cfg, const char* path);
// Merge CLI options over an existing config (in-place)
void config_merge_cli(App_Config* cfg, int argc, char** argv);

#endif /* CONFIG_H */