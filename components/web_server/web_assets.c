#include "web_assets.h"
#include <stdint.h>

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

static const char *asset(const uint8_t *start, const uint8_t *end, size_t *length)
{
    if (length) *length = (size_t)(end - start);
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

const char *web_assets_js(size_t *length)
{
    return asset(app_js_start, app_js_end, length);
}
