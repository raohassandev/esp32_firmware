#include "web_assets.h"
#include <stdint.h>

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

const char *web_assets_index(size_t *length)
{
    if (length) *length = (size_t)(index_html_end - index_html_start);
    return (const char *)index_html_start;
}
