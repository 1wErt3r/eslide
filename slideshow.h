#ifndef SLIDESHOW_H
#define SLIDESHOW_H

#include "common.h"
#include "media.h"

// Slideshow state variables (to be accessed by other modules)
extern Eina_Bool slideshow_running;
extern Eina_Bool is_shuffle_mode;
extern Evas_Object *slideshow_image;
extern Evas_Object *slideshow_video;
extern Evas_Object *letterbox_bg;
extern Ecore_Timer *slideshow_timer;

// Fade transition variables
extern Ecore_Animator *fade_animator;
extern Eina_Bool is_fading;
extern char *next_media_path;
extern double fade_start_time;

// Function declarations for slideshow functionality

// Slideshow control functions
void toggle_slideshow(void);
void show_next_media(void);
void show_media_immediate(const char *media_path);
void toggle_shuffle_mode(void);

// Fade transition functions
Eina_Bool fade_animator_cb(void *data);
void start_fade_transition(const char *media_path);

// Timer callback functions
Eina_Bool slideshow_timer_cb(void *data);

// Slideshow initialization and cleanup
void slideshow_init(Evas_Object *image_widget, Evas_Object *video_widget, Evas_Object *letterbox);
void slideshow_start(void);
void slideshow_cleanup(void);

// Runtime configuration setters
void slideshow_set_interval(double seconds);
void slideshow_set_fade_duration(double seconds);

#endif /* SLIDESHOW_H */