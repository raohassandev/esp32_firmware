#include "config_manager_json_guard.h"

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_MANAGER_JSON_MAX_DEPTH 16U

static bool json_depth_within_limit(const char *text)
{
    if (!text) return false;

    size_t depth = 0U;
    bool in_string = false;
    bool escaped = false;

    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        const unsigned char ch = *cursor;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{' || ch == '[') {
            depth++;
            if (depth > CONFIG_MANAGER_JSON_MAX_DEPTH) return false;
        } else if (ch == '}' || ch == ']') {
            if (depth == 0U) return false;
            depth--;
        }
    }

    /* Syntax validity remains cJSON's responsibility. Rejecting an unterminated
     * string or open container here is safe and avoids invoking the recursive
     * parser on malformed input that already cannot be valid JSON. */
    return !in_string && !escaped && depth == 0U;
}

cJSON *config_manager_guarded_cjson_parse(const char *text)
{
    if (!json_depth_within_limit(text)) return NULL;
    return cJSON_Parse(text);
}
