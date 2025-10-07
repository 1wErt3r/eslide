#include "weather.h"
#include "common.h"
#include <Elementary.h>
#include <Ecore.h>
#include <Ecore_Con.h>

// Weather state variables
Evas_Object* weather_label = NULL;
static Ecore_Timer* weather_timer = NULL;
static Ecore_Con_Url* weather_url = NULL;
static Eina_Bool _weather_used_https = EINA_FALSE;
static Eina_Bool _weather_inflight = EINA_FALSE;
Eina_Bool weather_visible = EINA_TRUE; // hidden by default
static char _station[16] = "KNYC";     // default NOAA station

// Response buffer for incoming data
typedef struct {
    char* buf;
    size_t len;
} WeatherBuf;

static WeatherBuf _wbuf = { NULL, 0 };

// Forward declarations
static Eina_Bool _weather_fetch_cb(void* data);
static void _weather_update_label(const char* text);
static void _weather_parse_json_and_update(const char* json, size_t len);
static void _weather_process_response(const char* buf, size_t len);

// Ecore_Con URL event handlers
static Ecore_Event_Handler* _eh_data = NULL;
static Ecore_Event_Handler* _eh_complete = NULL;

static Eina_Bool _on_url_data(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_DATA)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Data* ev = (Ecore_Con_Event_Url_Data*) event;
    if (!ev || ev->url_con != weather_url || ev->size <= 0)
        return ECORE_CALLBACK_PASS_ON;

    // Append incoming data to buffer
    size_t new_len = _wbuf.len + ev->size;
    char* n = (char*) realloc(_wbuf.buf, new_len + 1);
    if (!n)
        return ECORE_CALLBACK_PASS_ON;
    _wbuf.buf = n;
    memcpy(_wbuf.buf + _wbuf.len, ev->data, ev->size);
    _wbuf.len = new_len;
    _wbuf.buf[_wbuf.len] = '\0';
    DBG("Weather: received %zu bytes (total=%zu)", (size_t) ev->size, (size_t) _wbuf.len);
    return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool _on_url_complete(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_COMPLETE)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Complete* ev = (Ecore_Con_Event_Url_Complete*) event;
    if (!ev || ev->url_con != weather_url)
        return ECORE_CALLBACK_PASS_ON;

    // This request is no longer in flight
    _weather_inflight = EINA_FALSE;

    INF("Weather: request completed, status=%d, bytes=%zu", ev->status, (size_t) _wbuf.len);
    // Status code check (200 OK)
    if (ev->status == 200) {
        if (_wbuf.buf) {
            _weather_process_response(_wbuf.buf, _wbuf.len);
        }
    } else {
        WRN("Weather fetch failed, HTTP status=%d", ev->status);
        // If HTTPS was used, immediately try HTTP fallback once
        if (_weather_used_https) {
            INF("Weather: trying HTTP fallback");
            char url[160];
            snprintf(url, sizeof(url), "http://api.weather.gov/stations/%s/observations/latest",
                _station);
            ecore_con_url_url_set(weather_url, url);
            _weather_used_https = EINA_FALSE;
            // Immediately trigger a new fetch with the updated URL
            _weather_fetch_cb(NULL);
        }
    }

    // Reset buffer for the next request
    free(_wbuf.buf);
    _wbuf.buf = NULL;
    _wbuf.len = 0;

    return ECORE_CALLBACK_PASS_ON;
}

static void _ensure_event_handlers(void)
{
    if (!_eh_data)
        _eh_data = ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _on_url_data, NULL);
    if (!_eh_complete)
        _eh_complete
            = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _on_url_complete, NULL);
}

static void _weather_update_label(const char* text)
{
    if (!weather_label || !text)
        return;
    // Use simple label text; show temperature only
    char formatted_text[512];
    snprintf(formatted_text, sizeof(formatted_text),
        "<font=Open Sans:style=Light><color=#FFFFFF><font_size=24>%s</font_size></color></font>",
        text);
    elm_object_text_set(weather_label, formatted_text);
    INF("Weather: label updated to '%s'", text);
    if (weather_visible)
        evas_object_show(weather_label);
}

// Decide whether buffer is XML or JSON and parse accordingly
static void _weather_process_response(const char* buf, size_t len)
{
    if (!buf || len == 0)
        return;

    DBG("Weather: processing JSON response");
    _weather_parse_json_and_update(buf, len);
}


// Extract properties.temperature.value (degC) from api.weather.gov JSON without external deps
static void _weather_parse_json_and_update(const char* json, size_t len)
{
    if (!json || len == 0)
        return;
    const char* base = json;
    const char* t = strstr(base, "\"temperature\"");
    if (!t) {
        WRN("Weather: JSON temperature object not found");
        return;
    }
    const char* vkey = strstr(t, "\"value\"");
    if (!vkey) {
        WRN("Weather: JSON temperature value not found");
        return;
    }
    const char* colon = strchr(vkey, ':');
    if (!colon) {
        WRN("Weather: JSON parsing error (no colon)");
        return;
    }
    const char* v = colon + 1;
    while (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')
        v++;
    if (strncmp(v, "null", 4) == 0) {
        WRN("Weather: temperature value is null");
        return;
    }
    char* endptr = NULL;
    double celsius = strtod(v, &endptr);
    if (endptr == v) {
        WRN("Weather: failed to parse numeric temperature");
        return;
    }
    double fahrenheit = (celsius * 9.0 / 5.0) + 32.0;
    char label[64];
    snprintf(label, sizeof(label), "%.1f°F", fahrenheit);
    _weather_update_label(label);
}

// Timer callback: trigger a fetch
static Eina_Bool _weather_fetch_cb(void* data EINA_UNUSED)
{
    // Don't start a new fetch if one is already in progress
    if (_weather_inflight) {
        DBG("Weather fetch already in progress, skipping.");
        return ECORE_CALLBACK_RENEW;
    }

    // Create the URL object on the first call
    if (!weather_url) {
        char url[160];
        snprintf(
            url, sizeof(url), "https://api.weather.gov/stations/%s/observations/latest", _station);
        weather_url = ecore_con_url_new(url);
        _weather_used_https = EINA_TRUE;
        if (!weather_url) {
            WRN("Failed to create Ecore_Con_Url for weather");
            return ECORE_CALLBACK_RENEW;
        }
        ecore_con_url_timeout_set(weather_url, 8.0);
        ecore_con_url_additional_header_add(weather_url, "User-Agent", "eslide/1.0 (efl-hello)");
        ecore_con_url_additional_header_add(weather_url, "Accept", "application/json");
    }

    // Make sure our event handlers are registered.
    _ensure_event_handlers();

    INF("Weather: starting fetch from NOAA station %s", _station);
    INF("Weather: GET %s://api.weather.gov/stations/%s/observations/latest",
        _weather_used_https ? "https" : "http", _station);
    _weather_update_label("…");
    if (!ecore_con_url_get(weather_url)) {
        WRN("Weather fetch could not be started.");
    } else {
        _weather_inflight = EINA_TRUE;
    }

    return ECORE_CALLBACK_RENEW; // keep timer running
}

// Position lower-left with margin; ensure size fits within container
void on_letterbox_resize_weather(
    void* data EINA_UNUSED, Evas* e EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    if (!weather_label || !obj)
        return;
    int x, y, w, h;
    evas_object_geometry_get(obj, &x, &y, &w, &h);
    Evas_Coord mw = 0, mh = 0;
    evas_object_size_hint_min_get(weather_label, &mw, &mh);
    if (mw <= 0)
        mw = 240; // larger default width
    if (mh <= 0)
        mh = 48; // larger default height

    int margin = 12;
    int max_w = w - (2 * margin);
    int max_h = h - (2 * margin);
    if (max_w < 1)
        max_w = 1;
    if (max_h < 1)
        max_h = 1;

    // Clamp to container bounds
    if (mw > max_w)
        mw = max_w;
    if (mh > max_h)
        mh = max_h;

    // Apply size
    evas_object_resize(weather_label, mw, mh);

    // Position to lower-left with margin
    int px = x + margin;
    int py = y + h - margin - mh;
    evas_object_move(weather_label, px, py);
    evas_object_raise(weather_label);
}

void weather_init(Evas_Object* parent_window)
{
    weather_label = elm_label_add(parent_window);
    elm_object_text_set(weather_label, "");
    evas_object_layer_set(weather_label, 1000);
    // Make the weather label larger and allow wrapping to fit width
    elm_label_line_wrap_set(weather_label, ELM_WRAP_MIXED);
    // Give the label a larger minimum size and hide initially
    evas_object_size_hint_min_set(weather_label, 240, 48);
    if (weather_visible)
        evas_object_show(weather_label);
    else
        evas_object_hide(weather_label);
}

void weather_start(void)
{
    // Initial fetch immediately, then every 240 seconds
    // Ensure Ecore_Con is initialized before URL usage
    if (ecore_con_init() <= 0) {
        WRN("Failed to initialize Ecore_Con; weather fetch may not work");
    } else {
        INF("Ecore_Con initialized for weather overlay");
    }
    // Initialize URL subsystem
    if (ecore_con_url_init() <= 0) {
        WRN("Failed to initialize Ecore_Con_Url subsystem");
    } else {
        INF("Ecore_Con_Url initialized");
    }
    _weather_fetch_cb(NULL);
    if (weather_timer) {
        ecore_timer_del(weather_timer);
        weather_timer = NULL;
    }
    weather_timer = ecore_timer_add(60.0, _weather_fetch_cb, NULL);
    INF("Weather overlay polling started");
}

void weather_toggle(void)
{
    weather_visible = !weather_visible;
    if (!weather_label)
        return;
    if (weather_visible) {
        evas_object_show(weather_label);
        INF("Weather shown");
    } else {
        evas_object_hide(weather_label);
        INF("Weather hidden");
    }
}

void weather_set_visible(Eina_Bool visible)
{
    weather_visible = visible;
    if (!weather_label)
        return;
    if (visible)
        evas_object_show(weather_label);
    else
        evas_object_hide(weather_label);
}

void weather_cleanup(void)
{
    if (weather_timer) {
        ecore_timer_del(weather_timer);
        weather_timer = NULL;
    }
    if (weather_url) {
        ecore_con_url_free(weather_url);
        weather_url = NULL;
    }
    _weather_inflight = EINA_FALSE;
    if (_eh_data) {
        ecore_event_handler_del(_eh_data);
        _eh_data = NULL;
    }
    if (_eh_complete) {
        ecore_event_handler_del(_eh_complete);
        _eh_complete = NULL;
    }
    free(_wbuf.buf);
    _wbuf.buf = NULL;
    _wbuf.len = 0;
    weather_label = NULL;
    // Shutdown Ecore_Con after we're done
    ecore_con_url_shutdown();
    ecore_con_shutdown();
}

void weather_set_station(const char* station_code)
{
    if (!station_code || strlen(station_code) == 0) {
        return;
    }
    snprintf(_station, sizeof(_station), "%s", station_code);
    INF("Weather: station set to %s", _station);
    if (weather_url) {
        // Update URL to use new station for subsequent fetches
        char url[160];
        const char* scheme = _weather_used_https ? "https" : "http";
        snprintf(url, sizeof(url), "%s://api.weather.gov/stations/%s/observations/latest", scheme,
            _station);
        ecore_con_url_url_set(weather_url, url);
    }
}