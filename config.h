#ifndef CONFIG_H
#define CONFIG_H

#include <Eina.h>

typedef struct {
    double slideshow_interval;
    double fade_duration;
    const char *images_dir;
    Eina_Bool fullscreen;
    Eina_Bool shuffle;
    Eina_Bool clock_visible;
    Eina_Bool clock_24h; // false = 12-hour (default), true = 24-hour
} App_Config;

// Initialize defaults from compile-time constants and current module defaults
App_Config config_defaults(void);

// Parse command-line arguments with Ecore_Getopt, preserving defaults
App_Config config_parse(int argc, char **argv);

// Log parsed configuration values for debugging/visibility
void config_log(const App_Config *cfg);

#endif /* CONFIG_H */