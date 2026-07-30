#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "meter_types.h"

#define SAFETY_ALARM_METER_OFFLINE (1u << 0)
#define SAFETY_ALARM_METER_STALE   (1u << 1)

esp_err_t safety_manager_init(void);
float safety_manager_limit_target_kw(float requested_kw, const meter_data_t *grid_meter, uint32_t now_ms);
uint32_t safety_manager_get_alarm_flags(void);

/*
 * The configured staleness threshold, in milliseconds: how old a meter sample may
 * be before this module stops treating it as usable and blocks control input.
 *
 * Exposed because it must be the ONLY definition of "stale" in the product. There
 * were three. This module used the configured value (default 1000 ms) to inhibit
 * control, while the alarm path and the dashboard each used their own fixed
 * 5000 ms. That left a window in which control was inhibited for staleness while
 * the dashboard reported the measurement good and no alarm was raised -- an
 * operator seeing a healthy plant that was not controlling, with nothing on screen
 * explaining why.
 *
 * The window is reachable in practice, not in theory. Measured on the site link:
 * mean 93 ms, but 24 % of transactions exceed 250 ms and the tail reaches 319 ms,
 * so one gateway stall plus a retry crosses 1000 ms without approaching 5000 ms.
 *
 * The configured value wins because it is the one an engineer can tune to the
 * site's measured latency; a compiled-in constant cannot be. Cheap enough for the
 * sampling path: a single word read, no allocation, no configuration snapshot.
 *
 * Returns a sane floor rather than zero before init, so an early caller cannot
 * conclude that every sample is stale.
 */
uint32_t safety_manager_meter_stale_timeout_ms(void);
