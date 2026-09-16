#pragma once

#include <stdbool.h>

#include "commissioning_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board-local Engineering boundary for the touchscreen commissioning wizard.
 * The UI sees only screen-owned DTOs/callbacks. Core configuration, credential
 * verification and safety APIs remain on the other side of this file. */
bool local_commissioning_backend_init(screen_commissioning_backend_t *backend);

#ifdef __cplusplus
}
#endif
