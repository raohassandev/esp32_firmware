#include "http_json.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

esp_err_t http_body_read_bounded(httpd_req_t *request,
                                 size_t maximum_body_bytes,
                                 uint32_t deadline_ms,
                                 char **out_body)
{
    if (!request || !out_body || maximum_body_bytes == 0U || deadline_ms == 0U ||
        request->content_len <= 0 || (size_t)request->content_len > maximum_body_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    *out_body = NULL;
    size_t content_length = (size_t)request->content_len;
    char *body = malloc(content_length + 1U);
    if (!body) return ESP_ERR_NO_MEM;

    const uint64_t deadline = now_ms() + deadline_ms;
    size_t offset = 0;
    while (offset < content_length) {
        if (now_ms() >= deadline) {
            free(body);
            return ESP_ERR_TIMEOUT;
        }
        int received = httpd_req_recv(request, body + offset, content_length - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            free(body);
            return ESP_FAIL;
        }
        offset += (size_t)received;
    }

    body[offset] = '\0';
    *out_body = body;
    return ESP_OK;
}

bool http_json_depth_valid(const char *text, unsigned maximum_depth)
{
    if (!text || maximum_depth == 0U) return false;
    unsigned depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (*cursor == '\\') escaped = true;
            else if (*cursor == '"') in_string = false;
            continue;
        }
        if (*cursor == '"') in_string = true;
        else if (*cursor == '{' || *cursor == '[') {
            if (++depth > maximum_depth) return false;
        } else if (*cursor == '}' || *cursor == ']') {
            if (depth == 0U) return false;
            depth--;
        }
    }
    return !in_string && !escaped && depth == 0U;
}

esp_err_t http_json_parse_bounded(httpd_req_t *request,
                                  size_t maximum_body_bytes,
                                  uint32_t deadline_ms,
                                  unsigned maximum_depth,
                                  cJSON **out_root)
{
    if (!out_root) return ESP_ERR_INVALID_ARG;
    *out_root = NULL;

    char *body = NULL;
    esp_err_t err = http_body_read_bounded(request, maximum_body_bytes,
                                           deadline_ms, &body);
    if (err != ESP_OK) return err;

    if (!http_json_depth_valid(body, maximum_depth)) {
        free(body);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(body, strlen(body) + 1U);
    free(body);
    if (!root) return ESP_ERR_INVALID_ARG;
    *out_root = root;
    return ESP_OK;
}
