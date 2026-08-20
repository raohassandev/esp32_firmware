#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "source_detection_config.h"
#include "source_detection_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOURCE_DETECTION_REASON_TEXT_MAX 192U
#define SOURCE_DETECTION_LIMITATION_TEXT_MAX 192U

typedef struct {
    uint32_t config_generation;
    source_detection_mode_t mode;
    source_state_t state;
    source_state_t candidate_state;
    source_tariff_t tariff;
    source_reason_t reason;
    bool configured;
    bool evidence_fresh;
    bool transition_pending;
    bool conflict;
    bool fail_closed;
    bool detection_only;
    uint32_t evaluated_ms;
    uint32_t successful_reads;
    uint32_t failed_reads;
    esp_err_t last_error;

    bool single_has_sample;
    uint16_t single_raw_value;
    uint32_t single_age_ms;

    bool grid_has_sample;
    float grid_power_kw;
    uint32_t grid_age_ms;

    bool generator_has_sample;
    float generator_power_kw;
    uint32_t generator_age_ms;

    char reason_text[SOURCE_DETECTION_REASON_TEXT_MAX];
    char limitation_text[SOURCE_DETECTION_LIMITATION_TEXT_MAX];
} source_detection_status_t;

esp_err_t source_detection_init(void);
esp_err_t source_detection_get_status(source_detection_status_t *out_status);
void source_detection_notify_config_changed(void);
const char *source_detection_reason_message(source_reason_t reason);
const char *source_detection_mode_name(source_detection_mode_t mode);

/* One fail-closed source-attribution verdict for every operator surface.
 * Returns "grid" or "generator" only when the resolved source is configured,
 * fresh and conflict-free; every other state returns "unknown". */
const char *source_detection_attributed_to(const source_detection_status_t *status);

#ifdef __cplusplus
}
#endif
