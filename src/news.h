#ifndef NEWS_H
#define NEWS_H

#include <Eina.h>
#include <Evas.h>

// News overlay state
extern Eina_Bool news_visible;

// Initialize news overlay label on the given parent (letterbox container)
void news_init(Evas_Object* parent_window);

// Start periodic fetch (hourly) and begin rotating through titles
void news_start(void);

// Toggle visibility of the news overlay
void news_toggle(void);

// Explicitly set visibility
void news_set_visible(Eina_Bool visible);

// Cleanup resources
void news_cleanup(void);

// Reposition news overlay when letterbox resizes (top-center)
void on_letterbox_resize_news(void* data, Evas* e, Evas_Object* obj, void* event_info);

#endif /* NEWS_H */