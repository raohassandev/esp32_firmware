#pragma once
#include <stdint.h>

#define APP_FIRMWARE_VERSION "0.1.0"

typedef enum {
    APP_MODE_DISABLED = 0,
    APP_MODE_GRID,
    APP_MODE_GENERATOR,
    APP_MODE_MANUAL,
    APP_MODE_FAILSAFE,
    APP_MODE_EMERGENCY
} app_mode_t;
