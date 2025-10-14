#ifndef WEATHER_H
#define WEATHER_H

#include <Eina.h>
#include <Evas.h>

// Weather overlay state
extern Eina_Bool weather_visible;

// Initialize weather overlay label on the given parent (letterbox container)
void weather_init(Evas_Object* parent_window);

// Start periodic fetch (every 60 seconds) and initial update
void weather_start(void);

// Toggle visibility of the weather overlay
void weather_toggle(void);

// Explicitly set visibility
void weather_set_visible(Eina_Bool visible);

// Cleanup resources
void weather_cleanup(void);

// Reposition weather overlay when letterbox resizes (top-left corner)
void on_letterbox_resize_weather(void* data, Evas* e, Evas_Object* obj, void* event_info);

// Configure NOAA station code used for weather requests (e.g., "KNYC")
void weather_set_station(const char* station_code);

// Configure plaintext endpoint below weather overlay
void weather_set_endpoint(const char* endpoint_url);
void weather_set_endpoint_interval(double seconds);

#endif /* WEATHER_H */