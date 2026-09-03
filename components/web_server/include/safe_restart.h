#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Replacement for esp_restart() used only by the Engineering web restart task.
 * The call never returns: it either restarts after confirmed safe-zero or deletes
 * the restart task after a bounded failure timeout. */
void web_safe_restart(void);

#ifdef __cplusplus
}
#endif
