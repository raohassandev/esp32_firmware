#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "modbus_types.h"

#define APP_CONFIG_MAGIC 0x50564447u
#define APP_CONFIG_VERSION 6u
/* Core reserved for deterministic work: the control loop and the meter
 * acquisition tasks are pinned here, leaving the other core for the Wi-Fi stack,
 * lwIP and the HTTP server.
 *
 * Priorities alone already make control preempt acquisition, but priority does
 * not help when the delay comes from the network stack rather than from a task
 * queue. Pinning makes "control is never delayed by the web interface"
 * structural instead of probabilistic: on ESP-IDF the Wi-Fi and TCP/IP tasks
 * default to core 0, so control on core 1 cannot queue behind them.
 *
 * On a single-core build there is nothing to choose, so fall back to core 0. */
#if defined(CONFIG_FREERTOS_UNICORE)
#define PVDG_CONTROL_CORE 0
#else
#define PVDG_CONTROL_CORE 1
#endif

#define APP_MAX_METERS 4
#define APP_MAX_INVERTERS 12
/* Must equal SOURCE_MAX_GENERATORS in control_engine/source_mode.h. Defined
 * here so config_manager does not have to depend on control_engine; the two are
 * checked against each other by a _Static_assert in the control engine. */
#define APP_MAX_GENERATORS 3

typedef enum {
    APP_WIFI_IP_DHCP = 0,
    APP_WIFI_IP_STATIC = 1
} app_wifi_ip_mode_t;

typedef struct {
    bool enabled;
    char ssid[33];
    char password[65];
    app_wifi_ip_mode_t ip_mode;
    char static_ip[16];
    char gateway[16];
    char netmask[16];
    char dns1[16];
    char dns2[16];
} app_wifi_sta_profile_t;

typedef struct {
    app_wifi_sta_profile_t primary;
    app_wifi_sta_profile_t fallback;
    bool scan_before_connect;
    bool fallback_ap_enabled;
    char fallback_ap_ssid[33];
    char fallback_ap_password[65];
    uint8_t max_retries_per_profile;
    uint32_t reconnect_backoff_ms;
} app_wifi_config_t;

/* What a meter measures. The control engine selects by role rather than by
 * array position, so reordering the meter list cannot silently change which
 * physical instrument the control loop regulates against. The name field is a
 * display label and is never interpreted. */
typedef enum {
    METER_ROLE_UNASSIGNED = 0,
    METER_ROLE_GRID = 1,
    METER_ROLE_GENERATOR = 2,
    METER_ROLE_LOAD = 3,
    METER_ROLE_PV = 4
} meter_role_t;

#define METER_GENERATOR_INDEX_NONE 0xFFu
#define METER_ROLE_INDEX_NONE 0xFFu

/*
 * WHICH INSTRUMENT IS ON THE OTHER END OF THE WIRE. A COMMISSIONED FACT, NEVER
 * AN INFERENCE.
 *
 * This exists because a register's MEANING is a property of a meter family, not
 * of the Modbus transport. The concrete case is EM500 register 0x2100: on that
 * Lovato-derived family it is the documented "OR of all digital inputs", so it
 * is a BITMASK and "any non-zero word means generator" is the correct reading of
 * it. On a meter that is not from that family the same word may be an
 * enumeration, a counter or something else entirely, and applying the bitmask
 * rule to it would silently invent a source state from a number nobody has
 * interpreted. So the rule is confined to the family it was derived from, and
 * the family is stated by the commissioning engineer rather than guessed from
 * scale factors, register addresses or reply shapes.
 *
 * UNDECLARED IS ZERO AND IS REFUSED. A zeroed or newly migrated configuration
 * therefore declares no model, which keeps the commissioning gate closed instead
 * of assuming the one model whose semantics this firmware knows. That is the
 * fail-closed direction: the cost is one commissioning field, and the cost of the
 * other direction is a mis-read source state driving PV against a genset.
 *
 * Append, never renumber: these values are persisted in NVS and exposed over the
 * API.
 */
typedef enum {
    METER_MODEL_UNDECLARED = 0,
    /* Lovato-derived EM500/DMG610-compatible. The ONLY family whose register
     * semantics this firmware claims to know, and the only one in scope for this
     * release phase. */
    METER_MODEL_EM500_LOVATO = 1,
    /* Any other instrument reached as a plain Modbus endpoint. Deferred for this
     * phase: see docs/RELEASE_READINESS.md section 4c for the unpark criteria. */
    METER_MODEL_GENERIC_MODBUS = 2,
    METER_MODEL_COUNT
} meter_model_t;

/*
 * Does this meter belong to the Lovato-derived EM500 family?
 *
 * THE ONLY PLACE THE BITMASK QUESTION IS ANSWERED. Register 0x2100's "any
 * non-zero word means generator" reading is licensed by this predicate and by
 * nothing else -- not by the register address, not by the scale factor, not by
 * the shape of the reply. Written as a single inline so that every caller asks
 * the identical question and a future meter family is added in one place.
 */
static inline bool meter_model_is_em500(uint32_t model)
{
    return model == (uint32_t)METER_MODEL_EM500_LOVATO;
}

/*
 * Is this meter model in scope for the current release phase?
 *
 * Fail-closed by construction: it names the models that ARE permitted rather
 * than the ones that are not, so a model appended to the enum tomorrow is
 * refused until somebody deliberately adds it here. UNDECLARED is refused for
 * the same reason -- "we do not know what is wired" is not a licence to command
 * a plant. See docs/RELEASE_READINESS.md section 4c for the unpark criteria.
 */
static inline bool meter_model_in_phase_scope(uint32_t model)
{
    return meter_model_is_em500(model);
}

/* Which meter fills which role, resolved from the configuration. Reported
 * rather than enforced during load, so an ambiguous assignment keeps control
 * fail-closed instead of causing a commissioned configuration to be discarded. */
typedef struct {
    uint8_t grid_index;
    uint8_t grid_count;
    uint8_t generator_index[APP_MAX_GENERATORS];
    uint8_t generator_count;
    bool duplicate_generator;
    bool valid;
} meter_role_assignment_t;

typedef struct {
    bool enabled;
    char name[24];
    modbus_endpoint_t endpoint;
    uint8_t function_code;
    uint16_t active_power_address;
    modbus_data_type_t active_power_type;
    modbus_word_order_t active_power_order;
    float active_power_scale;
    uint32_t poll_interval_ms;
    /* Appended in schema 4. Kept last so schema 3 remains a byte-exact prefix. */
    uint8_t role;             /* meter_role_t */
    uint8_t generator_index;  /* 0..SOURCE_MAX_GENERATORS-1, else METER_GENERATOR_INDEX_NONE */
    /* Appended in schema 6.
     *
     * WHY THIS IS 32 BITS FOR A THREE-VALUED ENUM. Migration in this firmware is
     * dispatched on the stored blob SIZE, so a new schema must be a different size
     * from every old one or a schema-5 blob would be silently reinterpreted as a
     * schema-6 one with whatever bytes happened to follow. role and
     * generator_index leave two bytes of tail padding in this struct, so a
     * uint8_t here is absorbed by that padding: sizeof(meter_config_t) is 124
     * bytes both with and without it (measured, not assumed), and the two schemas
     * would be indistinguishable. A 32-bit field consumes the padding and adds
     * four bytes, making schema 6 strictly larger. The _Static_assert in
     * config_manager.c is what actually enforces that and will fail the build if
     * this is ever narrowed. */
    uint32_t model;           /* meter_model_t */
} meter_config_t;

typedef struct {
    bool enabled;
    char name[24];
    modbus_endpoint_t endpoint;
    float rated_power_kw;
    uint16_t power_limit_address;
    uint8_t power_limit_function;
    float raw_units_per_percent;
    float minimum_percent;
    float maximum_percent;
} inverter_config_t;

/* Rate limiting for the PV command, configurable independently per source.
 *
 * Disabling the ramp removes only the rate limit. The export/import policy, the
 * generator minimum-loading limit and every safety clamp still apply, so a
 * disabled ramp means "reach the allowed target immediately", never "ignore the
 * limits".
 *
 * The down rate should exceed the up rate: reducing PV is the direction that
 * protects a generator from under-loading and reverse power. */
typedef struct {
    bool enabled;
    float up_percent_per_second;
    float down_percent_per_second;
} ramp_profile_t;

typedef struct {
    bool enabled;
    float grid_import_target_kw;
    float deadband_kw;
    float kp;
    float ki;
    float ramp_up_percent_per_second;
    float ramp_down_percent_per_second;
    uint32_t interval_ms;
    uint32_t meter_stale_timeout_ms;
    /* Appended in schema 5. control_config_t is embedded in app_config_t ahead
     * of wifi_provision_id, so growing it shifts that field: schema 4 is NOT a
     * byte-exact prefix of schema 5 and must be migrated field by field, never
     * memcpy'd wholesale. The blob sizes differ, which is what keeps the two
     * distinguishable on load.
     * ramp_up/down_percent_per_second above are retained as the legacy fields
     * that seed generator_ramp on upgrade. */
    ramp_profile_t grid_ramp;
    ramp_profile_t generator_ramp;
} control_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    meter_config_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    inverter_config_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
    /* Identifies the build whose compiled-in Wi-Fi credentials were last
     * applied. When a flashed build carries a different id, its credentials are
     * written once and this is updated, so commissioning can hand a device new
     * credentials without erasing NVS and without overriding what an operator
     * later sets through the web UI. Kept last so older layouts stay a prefix. */
    uint32_t wifi_provision_id;
} app_config_t;
