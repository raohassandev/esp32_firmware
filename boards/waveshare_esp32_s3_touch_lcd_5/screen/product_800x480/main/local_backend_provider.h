#pragma once

#include <stdbool.h>

#include "screen_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board-local transport adapter. It reads the controller's EXISTING read-only
 * API over loopback and exposes provider-owned JSON to screen_runtime. */
bool local_backend_provider_init(screen_api_provider_t *provider);
bool local_backend_provider_fetch(const char *path);
void local_backend_provider_deinit(void);

#ifdef __cplusplus
}
#endif
