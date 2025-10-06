#ifndef UI_H
#define UI_H

#include "common.h"

// UI state variables (to be accessed by other modules)
extern Eina_Bool is_fullscreen;
extern Eina_Bool controls_visible;
extern Evas_Object *button_box;

// Function declarations for UI functionality

// Event handler functions
void on_done(void *data, Evas_Object *obj, void *event_info);
void on_button_click(void *data, Evas_Object *obj, void *event_info);
void on_fullscreen_click(void *data, Evas_Object *obj, void *event_info);
void on_next_image_click(void *data, Evas_Object *obj, void *event_info);
void on_shuffle_click(void *data, Evas_Object *obj, void *event_info);
void on_media_click(void *data, Evas_Object *obj, void *event_info);
void on_clock_toggle_click(void *data, Evas_Object *obj, void *event_info);

// UI control functions
void toggle_controls(void);

// UI creation and setup functions
Evas_Object* ui_create_main_window(Evas_Object **win_bg_out);
void ui_create_controls(Evas_Object *parent_box, Evas_Object *win);
void ui_setup_media_display(Evas_Object *parent_box);

// UI initialization and cleanup
void ui_init(void);
void ui_cleanup(void);

// Progress overlay API
void ui_progress_update_index(int index, int count);
void ui_progress_set_visible(Eina_Bool visible);
// UI state accessors
Eina_Bool ui_is_fullscreen(void);

#endif /* UI_H */