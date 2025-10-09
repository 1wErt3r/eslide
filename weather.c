#include "weather.h"
#include "common.h"
#include <Elementary.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

// Weather state variables
Evas_Object* weather_label = NULL;
static Evas_Object* endpoint_label = NULL;
static Ecore_Timer* weather_timer = NULL;
static Ecore_Timer* endpoint_timer = NULL;
static Ecore_Con_Url* weather_url = NULL;
static Ecore_Con_Url* endpoint_url_con = NULL;
static Eina_Bool _weather_used_https = EINA_FALSE;
static Eina_Bool _weather_inflight = EINA_FALSE;
static Eina_Bool _endpoint_inflight = EINA_FALSE;
Eina_Bool weather_visible = EINA_TRUE; // hidden by default
static char _station[16] = "KNYC";     // default NOAA station
static char* _endpoint_url = NULL;      // plaintext endpoint URL
static double _endpoint_interval = 60.0; // polling interval for endpoint

// Response buffer for incoming data
typedef struct {
    char* buf;
    size_t len;
} WeatherBuf;

static WeatherBuf _wbuf = { NULL, 0 };
typedef struct {
    char* buf;
    size_t len;
} EndpointBuf;

static EndpointBuf _epbuf = { NULL, 0 };

// Forward declarations
static Eina_Bool _weather_fetch_cb(void* data);
static void _weather_update_label(const char* text);
static void _weather_parse_xml_and_update(const char* xml, size_t len);
static void _weather_process_response(const char* buf, size_t len);
static void _endpoint_update_label(const char* text);
static Eina_Bool _endpoint_fetch_cb(void* data);

// Ecore_Con URL event handlers
static Ecore_Event_Handler* _eh_data = NULL;
static Ecore_Event_Handler* _eh_complete = NULL;
static Ecore_Event_Handler* _eh_ep_data = NULL;
static Ecore_Event_Handler* _eh_ep_complete = NULL;

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

// Endpoint event handlers
static Eina_Bool _on_ep_url_data(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_DATA)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Data* ev = (Ecore_Con_Event_Url_Data*) event;
    if (!ev || ev->url_con != endpoint_url_con || ev->size <= 0)
        return ECORE_CALLBACK_PASS_ON;

    size_t new_len = _epbuf.len + ev->size;
    char* n = (char*) realloc(_epbuf.buf, new_len + 1);
    if (!n)
        return ECORE_CALLBACK_PASS_ON;
    _epbuf.buf = n;
    memcpy(_epbuf.buf + _epbuf.len, ev->data, ev->size);
    _epbuf.len = new_len;
    _epbuf.buf[_epbuf.len] = '\0';
    DBG("Endpoint: received %zu bytes (total=%zu)", (size_t) ev->size, (size_t) _epbuf.len);
    return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool _on_ep_url_complete(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_COMPLETE)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Complete* ev = (Ecore_Con_Event_Url_Complete*) event;
    if (!ev || ev->url_con != endpoint_url_con)
        return ECORE_CALLBACK_PASS_ON;

    _endpoint_inflight = EINA_FALSE;

    INF("Endpoint: request completed, status=%d, bytes=%zu", ev->status, (size_t) _epbuf.len);
    if (ev->status == 200) {
        if (_epbuf.buf) {
            // Basic sanitation: collapse newlines to spaces, trim trailing whitespace
            for (size_t i = 0; i < _epbuf.len; i++) {
                if (_epbuf.buf[i] == '\r' || _epbuf.buf[i] == '\n')
                    _epbuf.buf[i] = ' ';
            }
            // Trim leading/trailing spaces
            size_t start = 0;
            while (start < _epbuf.len && isspace((unsigned char) _epbuf.buf[start])) start++;
            size_t end = _epbuf.len;
            while (end > start && isspace((unsigned char) _epbuf.buf[end - 1])) end--;
            char* line = NULL;
            size_t outlen = (end > start) ? (end - start) : 0;
            if (outlen > 0) {
                line = (char*) malloc(outlen + 1);
                if (line) {
                    memcpy(line, _epbuf.buf + start, outlen);
                    line[outlen] = '\0';
                }
            }
            if (line) {
                _endpoint_update_label(line);
                free(line);
            } else {
                _endpoint_update_label("(empty)");
            }
        }
    } else {
        WRN("Endpoint fetch failed, HTTP status=%d", ev->status);
        _endpoint_update_label("Endpoint error");
    }

    free(_epbuf.buf);
    _epbuf.buf = NULL;
    _epbuf.len = 0;
    return ECORE_CALLBACK_PASS_ON;
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

static void _endpoint_update_label(const char* text)
{
    if (!endpoint_label || !text)
        return;
    char formatted_text[512];
    snprintf(formatted_text, sizeof(formatted_text),
        "<font=Open Sans:style=Light><color=#FFFFFF><font_size=18>%s</font_size></color></font>",
        text);
    elm_object_text_set(endpoint_label, formatted_text);
    INF("Endpoint: label updated to '%s'", text);
    if (weather_visible)
        evas_object_show(endpoint_label);
}

// Process XML response from NOAA API
static void _weather_process_response(const char* buf, size_t len)
{
    if (!buf || len == 0)
        return;

    DBG("Weather: processing XML response");
    _weather_parse_xml_and_update(buf, len);
}


// Extract temperature from NOAA XML response using libxml2
static void _weather_parse_xml_and_update(const char* xml, size_t len)
{
    if (!xml || len == 0)
        return;

    // Initialize libxml2 parser
    xmlDocPtr doc = xmlParseMemory(xml, (int) len);
    if (!doc) {
        WRN("Weather: failed to parse XML response");
        return;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) {
        WRN("Weather: no root element in XML");
        xmlFreeDoc(doc);
        return;
    }

    // Use XPath to find the temp_f element
    xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
    if (!xpath_ctx) {
        WRN("Weather: failed to create XPath context");
        xmlFreeDoc(doc);
        return;
    }

    xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression((const xmlChar*) "//temp_f", xpath_ctx);
    if (!xpath_obj) {
        WRN("Weather: XPath evaluation failed");
        xmlXPathFreeContext(xpath_ctx);
        xmlFreeDoc(doc);
        return;
    }

    if (xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
        xmlNodePtr temp_node = xpath_obj->nodesetval->nodeTab[0];
        xmlChar* temp_content = xmlNodeGetContent(temp_node);
        if (temp_content) {
            char* endptr = NULL;
            double fahrenheit = strtod((const char*) temp_content, &endptr);
            if (endptr != (const char*) temp_content) {
                char label[64];
                snprintf(label, sizeof(label), "%.1f°F", fahrenheit);
                _weather_update_label(label);
                INF("Weather: parsed temperature %.1f°F from XML using XPath", fahrenheit);
            } else {
                WRN("Weather: failed to parse temperature value from XML");
            }
            xmlFree(temp_content);
        }
    } else {
        WRN("Weather: temp_f element not found in XML response");
    }

    xmlXPathFreeObject(xpath_obj);
    xmlXPathFreeContext(xpath_ctx);

    xmlFreeDoc(doc);
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
        ecore_con_url_additional_header_add(weather_url, "Accept", "application/vnd.noaa.obs+xml");
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

// Timer callback: trigger endpoint fetch
static Eina_Bool _endpoint_fetch_cb(void* data EINA_UNUSED)
{
    if (!_endpoint_url || !_endpoint_url[0])
        return ECORE_CALLBACK_RENEW;
    if (_endpoint_inflight)
        return ECORE_CALLBACK_RENEW;
    if (!endpoint_url_con) {
        endpoint_url_con = ecore_con_url_new(_endpoint_url);
        if (!endpoint_url_con) {
            WRN("Failed to create Ecore_Con_Url for endpoint");
            return ECORE_CALLBACK_RENEW;
        }
        ecore_con_url_timeout_set(endpoint_url_con, 8.0);
        ecore_con_url_additional_header_add(endpoint_url_con, "User-Agent", "eslide/1.0 (efl-hello)");
        ecore_con_url_additional_header_add(endpoint_url_con, "Accept", "text/plain");
    }
    // Register handlers once
    if (!_eh_ep_data)
        _eh_ep_data = ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _on_ep_url_data, NULL);
    if (!_eh_ep_complete)
        _eh_ep_complete
            = ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _on_ep_url_complete, NULL);

    INF("Endpoint: GET %s", _endpoint_url);
    _endpoint_update_label("…");
    if (!ecore_con_url_get(endpoint_url_con)) {
        WRN("Endpoint fetch could not be started.");
    } else {
        _endpoint_inflight = EINA_TRUE;
    }
    return ECORE_CALLBACK_RENEW;
}

// Position lower-left with margin; ensure size fits within container
void on_letterbox_resize_weather(
    void* data EINA_UNUSED, Evas* e EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    if ((!weather_label && !endpoint_label) || !obj)
        return;
    int x, y, w, h;
    evas_object_geometry_get(obj, &x, &y, &w, &h);
    // Determine label sizes
    Evas_Coord w_mw = 0, w_mh = 0;
    if (weather_label)
        evas_object_size_hint_min_get(weather_label, &w_mw, &w_mh);
    if (w_mw <= 0)
        w_mw = 240;
    if (w_mh <= 0)
        w_mh = 48;
    Evas_Coord e_mw = 0, e_mh = 0;
    if (endpoint_label)
        evas_object_size_hint_min_get(endpoint_label, &e_mw, &e_mh);
    if (e_mw <= 0)
        e_mw = 240;
    if (e_mh <= 0)
        e_mh = 36;

    // Asymmetric horizontal margins: slightly larger on the right
    int margin_left = 12;
    int margin_right = 20;
    int bottom_margin = 12;
    int max_w = w - (margin_left + margin_right);
    int max_h = h - (2 * bottom_margin);
    if (max_w < 1)
        max_w = 1;
    if (max_h < 1)
        max_h = 1;

    // Clamp to container bounds
    if (w_mw > max_w)
        w_mw = max_w;
    if (w_mh > max_h)
        w_mh = max_h;
    if (e_mw > max_w)
        e_mw = max_w;
    if (e_mh > max_h)
        e_mh = max_h;

    // Apply sizes (endpoint takes full width)
    if (weather_label)
        evas_object_resize(weather_label, w_mw, w_mh);
    if (endpoint_label)
        evas_object_resize(endpoint_label, max_w, e_mh);

    // Stack endpoint below weather; ensure endpoint sits at bottom, weather above it
    int px = x + margin_left;
    int ep_py = y + h - bottom_margin - e_mh;
    int w_py = ep_py - w_mh;
    if (endpoint_label) {
        evas_object_move(endpoint_label, px, ep_py);
        evas_object_raise(endpoint_label);
    }
    if (weather_label) {
        // If no endpoint configured, keep weather at bottom
        int wy = endpoint_label ? w_py : ep_py;
        evas_object_move(weather_label, px, wy);
        evas_object_raise(weather_label);
    }
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

    // Endpoint label
    endpoint_label = elm_label_add(parent_window);
    elm_object_text_set(endpoint_label, "");
    // Match styling with weather label
    evas_object_layer_set(endpoint_label, 1000);
    elm_label_line_wrap_set(endpoint_label, ELM_WRAP_MIXED);
    evas_object_size_hint_min_set(endpoint_label, 240, 36);
    if (weather_visible && _endpoint_url)
        evas_object_show(endpoint_label);
    else
        evas_object_hide(endpoint_label);
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
    // Initialize libxml2
    xmlInitParser();
    INF("libxml2 initialized for XML parsing");
    _weather_fetch_cb(NULL);
    if (weather_timer) {
        ecore_timer_del(weather_timer);
        weather_timer = NULL;
    }
    weather_timer = ecore_timer_add(60.0, _weather_fetch_cb, NULL);
    INF("Weather overlay polling started");

    // Start endpoint polling if configured
    if (_endpoint_url && _endpoint_url[0]) {
        _endpoint_fetch_cb(NULL);
        if (endpoint_timer) {
            ecore_timer_del(endpoint_timer);
            endpoint_timer = NULL;
        }
        endpoint_timer = ecore_timer_add(_endpoint_interval, _endpoint_fetch_cb, NULL);
        INF("Endpoint polling started (interval=%.1f s)", _endpoint_interval);
    }
}

void weather_toggle(void)
{
    weather_visible = !weather_visible;
    if (!weather_label)
        return;
    if (weather_visible) {
        evas_object_show(weather_label);
        if (endpoint_label && _endpoint_url && _endpoint_url[0])
            evas_object_show(endpoint_label);
        INF("Weather shown");
    } else {
        evas_object_hide(weather_label);
        if (endpoint_label)
            evas_object_hide(endpoint_label);
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
    if (endpoint_label) {
        if (visible && _endpoint_url && _endpoint_url[0])
            evas_object_show(endpoint_label);
        else
            evas_object_hide(endpoint_label);
    }
}

void weather_cleanup(void)
{
    if (weather_timer) {
        ecore_timer_del(weather_timer);
        weather_timer = NULL;
    }
    if (endpoint_timer) {
        ecore_timer_del(endpoint_timer);
        endpoint_timer = NULL;
    }
    if (weather_url) {
        ecore_con_url_free(weather_url);
        weather_url = NULL;
    }
    if (endpoint_url_con) {
        ecore_con_url_free(endpoint_url_con);
        endpoint_url_con = NULL;
    }
    _weather_inflight = EINA_FALSE;
    _endpoint_inflight = EINA_FALSE;
    if (_eh_data) {
        ecore_event_handler_del(_eh_data);
        _eh_data = NULL;
    }
    if (_eh_complete) {
        ecore_event_handler_del(_eh_complete);
        _eh_complete = NULL;
    }
    if (_eh_ep_data) {
        ecore_event_handler_del(_eh_ep_data);
        _eh_ep_data = NULL;
    }
    if (_eh_ep_complete) {
        ecore_event_handler_del(_eh_ep_complete);
        _eh_ep_complete = NULL;
    }
    free(_wbuf.buf);
    _wbuf.buf = NULL;
    _wbuf.len = 0;
    free(_epbuf.buf);
    _epbuf.buf = NULL;
    _epbuf.len = 0;
    weather_label = NULL;
    endpoint_label = NULL;
    // Cleanup libxml2
    xmlCleanupParser();
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

void weather_set_endpoint(const char* endpoint_url)
{
    if (_endpoint_url) {
        free(_endpoint_url);
        _endpoint_url = NULL;
    }
    if (endpoint_url && endpoint_url[0]) {
        _endpoint_url = strdup(endpoint_url);
        INF("Endpoint: set to %s", _endpoint_url);
        if (endpoint_label && weather_visible)
            evas_object_show(endpoint_label);
    } else {
        if (endpoint_label)
            evas_object_hide(endpoint_label);
    }
}

void weather_set_endpoint_interval(double seconds)
{
    if (seconds > 0.1)
        _endpoint_interval = seconds;
    else
        _endpoint_interval = 60.0;
    INF("Endpoint: interval set to %.1f seconds", _endpoint_interval);
    if (endpoint_timer) {
        ecore_timer_del(endpoint_timer);
        endpoint_timer = ecore_timer_add(_endpoint_interval, _endpoint_fetch_cb, NULL);
    }
}