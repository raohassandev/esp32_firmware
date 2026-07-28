#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reads exactly request->content_len bytes under one cumulative deadline.
 * The returned buffer is NUL terminated and owned by the caller. */
esp_err_t http_body_read_bounded(httpd_req_t *request,
                                 size_t maximum_body_bytes,
                                 uint32_t deadline_ms,
                                 char **out_body);

/* Validates balanced JSON delimiters and limits nesting before cJSON recursion. */
bool http_json_depth_valid(const char *text, unsigned maximum_depth);

/* Reads, depth-checks, and parses one JSON request body. The returned cJSON tree
 * is owned by the caller. */
esp_err_t http_json_parse_bounded(httpd_req_t *request,
                                  size_t maximum_body_bytes,
                                  uint32_t deadline_ms,
                                  unsigned maximum_depth,
                                  cJSON **out_root);

#ifdef __cplusplus
}
#endif
