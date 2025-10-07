#ifndef NET_H
#define NET_H

#include "common.h"

// Networking overlay state
extern Evas_Object* net_label;

// Initialize network label overlay and positioning
void net_init(Evas_Object* parent_window);

// Start asynchronous HTTP request to fetch a short message
void net_fetch_start(void);

// Start periodic refresh timer (seconds)
void net_refresh_start(double interval_seconds);
// Stop periodic refresh timer
void net_refresh_stop(void);

// Cleanup network resources
void net_cleanup(void);

#endif /* NET_H */