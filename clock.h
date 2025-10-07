#ifndef CLOCK_H
#define CLOCK_H

#include "common.h"

// Clock state variables (to be accessed by other modules)
extern Evas_Object* clock_label;
extern Ecore_Timer* clock_timer;
extern Eina_Bool clock_visible;
extern Eina_Bool clock_is_24h;

// Function declarations for clock functionality

// Clock control functions
void toggle_clock(void);
Eina_Bool clock_timer_cb(void* data);
void clock_set_24h(Eina_Bool use_24h);

// Clock positioning and layout
void on_letterbox_resize(void* data, Evas* e, Evas_Object* obj, void* event_info);

// Clock initialization and cleanup
void clock_init(Evas_Object* parent_window);
void clock_start(void);
void clock_cleanup(void);

#endif /* CLOCK_H */