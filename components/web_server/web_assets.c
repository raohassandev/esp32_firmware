#include "web_assets.h"
#include <stdint.h>

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");
extern const uint8_t wifi_css_start[] asm("_binary_wifi_css_start");
extern const uint8_t wifi_css_end[] asm("_binary_wifi_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t wifi_utils_js_start[] asm("_binary_wifi_utils_js_start");
extern const uint8_t wifi_utils_js_end[] asm("_binary_wifi_utils_js_end");
extern const uint8_t wifi_guard_js_start[] asm("_binary_wifi_guard_js_start");
extern const uint8_t wifi_guard_js_end[] asm("_binary_wifi_guard_js_end");
extern const uint8_t wifi_js_start[] asm("_binary_wifi_js_start");
extern const uint8_t wifi_js_end[] asm("_binary_wifi_js_end");

static const char *asset(const uint8_t *start, const uint8_t *end, size_t *length)
{
    size_t size = (size_t)(end - start);
    if (size > 0 && start[size - 1] == '\0') {
        --size;
    }
    if (length) *length = size;
    return (const char *)start;
}

const char *web_assets_index(size_t *length)
{
    return asset(index_html_start, index_html_end, length);
}

const char *web_assets_css(size_t *length)
{
    return asset(app_css_start, app_css_end, length);
}

const char *web_assets_wifi_css(size_t *length)
{
    return asset(wifi_css_start, wifi_css_end, length);
}

const char *web_assets_js(size_t *length)
{
    return asset(app_js_start, app_js_end, length);
}

const char *web_assets_wifi_utils_js(size_t *length)
{
    return asset(wifi_utils_js_start, wifi_utils_js_end, length);
}

const char *web_assets_wifi_guard_js(size_t *length)
{
    return asset(wifi_guard_js_start, wifi_guard_js_end, length);
}

const char *web_assets_wifi_js(size_t *length)
{
    return asset(wifi_js_start, wifi_js_end, length);
}
