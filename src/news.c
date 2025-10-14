#include "news.h"
#include "common.h"
#include <Elementary.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

// News state variables
Evas_Object* news_label = NULL;
Eina_List* _titles = NULL; // list of strdup'd char* titles
static int _current_index = 0;
static Ecore_Timer* _refresh_timer = NULL; // hourly fetch
static Ecore_Timer* _rotate_timer = NULL;  // rotate titles
static Ecore_Con_Url* _news_url = NULL;
static Eina_Bool _news_inflight = EINA_FALSE;
Eina_Bool news_visible = EINA_FALSE; // hidden by default

// Response buffer
typedef struct {
    char* buf;
    size_t len;
} NewsBuf;

static NewsBuf _nbuf = { NULL, 0 };

// Forward declarations
static Eina_Bool _news_fetch_cb(void* data);
static Eina_Bool _news_rotate_cb(void* data);
static void _news_update_label(const char* text);
static void _news_parse_rss_titles(const char* xml, size_t len);
static void _news_clear_titles(void);

// Ecore_Con URL event handlers
static Ecore_Event_Handler* _eh_data = NULL;
static Ecore_Event_Handler* _eh_complete = NULL;

static Eina_Bool _on_url_data(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_DATA)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Data* ev = (Ecore_Con_Event_Url_Data*) event;
    if (!ev || ev->url_con != _news_url || ev->size <= 0)
        return ECORE_CALLBACK_PASS_ON;

    size_t new_len = _nbuf.len + ev->size;
    char* n = (char*) realloc(_nbuf.buf, new_len + 1);
    if (!n)
        return ECORE_CALLBACK_PASS_ON;
    _nbuf.buf = n;
    memcpy(_nbuf.buf + _nbuf.len, ev->data, ev->size);
    _nbuf.len = new_len;
    _nbuf.buf[_nbuf.len] = '\0';
    DBG("News: received %zu bytes (total=%zu)", (size_t) ev->size, (size_t) _nbuf.len);
    return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool _on_url_complete(void* data, int type, void* event)
{
    (void) data;
    if (type != ECORE_CON_EVENT_URL_COMPLETE)
        return ECORE_CALLBACK_PASS_ON;
    Ecore_Con_Event_Url_Complete* ev = (Ecore_Con_Event_Url_Complete*) event;
    if (!ev || ev->url_con != _news_url)
        return ECORE_CALLBACK_PASS_ON;

    _news_inflight = EINA_FALSE;

    INF("News: request completed, status=%d, bytes=%zu", ev->status, (size_t) _nbuf.len);
    if (ev->status == 200) {
        if (_nbuf.buf) {
            _news_parse_rss_titles(_nbuf.buf, _nbuf.len);
        }
    } else {
        WRN("News fetch failed, HTTP status=%d", ev->status);
    }

    free(_nbuf.buf);
    _nbuf.buf = NULL;
    _nbuf.len = 0;

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

static void _news_update_label(const char* text)
{
    if (!news_label || !text)
        return;
    char formatted_text[1024];
    snprintf(formatted_text, sizeof(formatted_text),
        "<font=Open Sans:style=Light><color=#FFFFFF><font_size=24>%s</font_size></color></font>",
        text);
    elm_object_text_set(news_label, formatted_text);
    if (news_visible)
        evas_object_show(news_label);
}

static void _news_show_current(void)
{
    if (!_titles)
        return;
    int count = eina_list_count(_titles);
    if (count <= 0)
        return;
    if (_current_index < 0 || _current_index >= count)
        _current_index = 0;
    char* title = (char*) eina_list_nth(_titles, _current_index);
    if (title)
        _news_update_label(title);
}

static Eina_Bool _news_rotate_cb(void* data EINA_UNUSED)
{
    if (!_titles)
        return ECORE_CALLBACK_RENEW;
    int count = eina_list_count(_titles);
    if (count <= 0)
        return ECORE_CALLBACK_RENEW;
    _current_index = (_current_index + 1) % count;
    _news_show_current();
    return ECORE_CALLBACK_RENEW;
}

static void _news_clear_titles(void)
{
    void* itr;
    EINA_LIST_FREE(_titles, itr)
    {
        free(itr);
    }
    _titles = NULL;
    _current_index = 0;
}

static void _news_parse_rss_titles(const char* xml, size_t len)
{
    if (!xml || len == 0)
        return;

    xmlDocPtr doc = xmlParseMemory(xml, (int) len);
    if (!doc) {
        WRN("News: failed to parse RSS XML");
        return;
    }

    xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
    if (!xpath_ctx) {
        WRN("News: failed to create XPath context");
        xmlFreeDoc(doc);
        return;
    }

    xmlXPathObjectPtr xpath_obj
        = xmlXPathEvalExpression((const xmlChar*) "//item/title", xpath_ctx);
    if (!xpath_obj) {
        WRN("News: XPath evaluation failed");
        xmlXPathFreeContext(xpath_ctx);
        xmlFreeDoc(doc);
        return;
    }

    _news_clear_titles();

    if (xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
        int limit = xpath_obj->nodesetval->nodeNr;
        for (int i = 0; i < limit; i++) {
            xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                char* s = strdup((const char*) content);
                if (s)
                    _titles = eina_list_append(_titles, s);
                xmlFree(content);
            }
        }
        INF("News: parsed %d titles from RSS", eina_list_count(_titles));
    } else {
        WRN("News: no titles found in RSS feed");
    }

    xmlXPathFreeObject(xpath_obj);
    xmlXPathFreeContext(xpath_ctx);
    xmlFreeDoc(doc);

    // Start rotation timer if we have titles
    if (_rotate_timer) {
        ecore_timer_del(_rotate_timer);
        _rotate_timer = NULL;
    }
    if (_titles)
        _rotate_timer = ecore_timer_add(8.0, _news_rotate_cb, NULL);

    // Show the first title immediately
    _news_show_current();
}

static Eina_Bool _news_fetch_cb(void* data EINA_UNUSED)
{
    if (_news_inflight) {
        DBG("News fetch already in progress, skipping.");
        return ECORE_CALLBACK_RENEW;
    }

    if (!_news_url) {
        _news_url = ecore_con_url_new("https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml");
        if (!_news_url) {
            WRN("Failed to create Ecore_Con_Url for news");
            return ECORE_CALLBACK_RENEW;
        }
        ecore_con_url_timeout_set(_news_url, 10.0);
        ecore_con_url_additional_header_add(_news_url, "User-Agent", "eslide/1.0 (efl-hello)");
        ecore_con_url_additional_header_add(_news_url, "Accept", "application/rss+xml");
    }

    _ensure_event_handlers();

    INF("News: starting RSS fetch");
    if (!ecore_con_url_get(_news_url)) {
        WRN("News fetch could not be started.");
    } else {
        _news_inflight = EINA_TRUE;
    }

    return ECORE_CALLBACK_RENEW; // keep refresh timer running
}

void on_letterbox_resize_news(
    void* data EINA_UNUSED, Evas* e EINA_UNUSED, Evas_Object* obj, void* event_info EINA_UNUSED)
{
    if (!news_label || !obj)
        return;
    int x, y, w, h;
    evas_object_geometry_get(obj, &x, &y, &w, &h);

    Evas_Coord mw = 0, mh = 0;
    evas_object_size_hint_min_get(news_label, &mw, &mh);
    if (mw <= 0)
        mw = w * 0.85; // use 85% of width for better readability
    if (mh <= 0)
        mh = h * 0.25; // use 25% of screen height for multi-line titles, minimum 240px

    // Ensure minimum height for longer titles
    int min_height = 240;
    if (mh < min_height)
        mh = min_height;

    int margin = 20; // increased margin for better spacing
    int max_w = w - (2 * margin);
    int max_h = h - (2 * margin);
    if (max_w < 1)
        max_w = 1;
    if (max_h < 1)
        max_h = 1;

    if (mw > max_w)
        mw = max_w;
    if (mh > max_h)
        mh = max_h;

    evas_object_resize(news_label, mw, mh);

    // Position with more left margin and higher on screen
    int left_margin_offset = 80; // additional left margin
    int px = x + (w - mw) / 2 + left_margin_offset;
    int py = y + (h / 10); // position higher on screen for better visibility
    evas_object_move(news_label, px, py);
    evas_object_raise(news_label);
}

void news_init(Evas_Object* parent_window)
{
    news_label = elm_label_add(parent_window);
    elm_label_line_wrap_set(news_label, ELM_WRAP_MIXED);
    evas_object_layer_set(news_label, 1000);
    evas_object_size_hint_min_set(news_label, 800, 240); // increased for better typography
    if (news_visible)
        evas_object_show(news_label);
    else
        evas_object_hide(news_label);
}

void news_start(void)
{
    // Initialize Ecore_Con and URL subsystem
    if (ecore_con_init() <= 0) {
        WRN("Failed to initialize Ecore_Con; news fetch may not work");
    }
    if (ecore_con_url_init() <= 0) {
        WRN("Failed to initialize Ecore_Con_Url subsystem");
    }

    // Initial fetch
    _news_fetch_cb(NULL);

    // Hourly refresh
    if (_refresh_timer) {
        ecore_timer_del(_refresh_timer);
        _refresh_timer = NULL;
    }
    _refresh_timer = ecore_timer_add(3600.0, _news_fetch_cb, NULL);
    INF("News overlay polling started");
}

void news_toggle(void)
{
    news_visible = !news_visible;
    if (!news_label)
        return;
    if (news_visible) {
        evas_object_show(news_label);
        INF("News shown");
    } else {
        evas_object_hide(news_label);
        INF("News hidden");
    }
}

void news_set_visible(Eina_Bool visible)
{
    news_visible = visible;
    if (!news_label)
        return;
    if (visible)
        evas_object_show(news_label);
    else
        evas_object_hide(news_label);
}

void news_cleanup(void)
{
    if (_refresh_timer) {
        ecore_timer_del(_refresh_timer);
        _refresh_timer = NULL;
    }
    if (_rotate_timer) {
        ecore_timer_del(_rotate_timer);
        _rotate_timer = NULL;
    }
    if (_news_url) {
        ecore_con_url_free(_news_url);
        _news_url = NULL;
    }
    _news_inflight = EINA_FALSE;
    if (_eh_data) {
        ecore_event_handler_del(_eh_data);
        _eh_data = NULL;
    }
    if (_eh_complete) {
        ecore_event_handler_del(_eh_complete);
        _eh_complete = NULL;
    }
    free(_nbuf.buf);
    _nbuf.buf = NULL;
    _nbuf.len = 0;
    _news_clear_titles();
    news_label = NULL;

    // Shutdown Ecore_Con after we're done
    ecore_con_url_shutdown();
    ecore_con_shutdown();
}