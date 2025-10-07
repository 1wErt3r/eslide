#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <Elementary.h>
#include <Emotion.h>
#include <Eina.h>
#include <Ecore.h>

// Application configuration constants
#define SLIDESHOW_INTERVAL 10.0  // 10 seconds between images
#define IMAGES_DIR "./images/"
#define FADE_DURATION 0.5  // 0.5 seconds fade duration
// Default window size
#define DEFAULT_WINDOW_WIDTH 640
#define DEFAULT_WINDOW_HEIGHT 480

// Media file structure to store paths
typedef struct _MediaFile {
   char *path;
   Eina_Bool is_image;
} MediaFile;

// Global logging domain (to be initialized in main)
extern int _log_domain;

// Logging macros for our application
#define CRITICAL(...) EINA_LOG_DOM_CRIT(_log_domain, __VA_ARGS__)
#define ERR(...)      EINA_LOG_DOM_ERR(_log_domain, __VA_ARGS__)
#define WRN(...)      EINA_LOG_DOM_WARN(_log_domain, __VA_ARGS__)
#define INF(...)      EINA_LOG_DOM_INFO(_log_domain, __VA_ARGS__)
#define DBG(...)      EINA_LOG_DOM_DBG(_log_domain, __VA_ARGS__)

// Function to initialize logging domain
int common_init_logging(void);

// Function to cleanup logging domain
void common_cleanup_logging(void);

#endif /* COMMON_H */