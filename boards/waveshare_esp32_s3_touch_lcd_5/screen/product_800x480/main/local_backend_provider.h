#pragma once

#include <stdbool.h>

#include "screen_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board-local read-model adapter. It projects the existing Product Core's
 * read-only cached snapshots into the already-established screen API JSON
 * shapes entirely in-process. No socket/TCP loopback and no write authority. */
bool local_backend_provider_init(screen_api_provider_t *provider);
bool local_backend_provider_fetch(const char *path);
void local_backend_provider_deinit(void);

#ifdef __cplusplus
}
#endif
