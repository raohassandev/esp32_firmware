#include "config_manager.h"
#include "device_identity.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define NS "pvdg"
#define KEY "config"
#define MASKED_PASSWORD "********"
#define CONFIG_JSON_MAX_DEPTH 16U

/* Label the recovery access point is built from. The MAC suffix is appended at
 * runtime, so what is stored here is a name, not a credential.
 *
 * Kept byte-identical to what shipped before, so upgrading a commissioned unit
 * does not rename the network an engineer already has saved on their phone. */
#define RECOVERY_AP_SSID_BASE "Automatrix-PVDG"

static const char *TAG = "config";
static app_config_t s_cfg;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    char ssid[33];
    char password[65];
} legacy_wifi_config_v1_t;

/* Frozen control layout as it stood through schema 4, before the per-source ramp
 * profiles. Every legacy app layout embeds this rather than the live
 * control_config_t, so growing control_config_t cannot change their sizes. */
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
} legacy_control_config_v4_t;

/* Frozen copy of meter_config_t as it stood through schema 3. The legacy
 * layouts below MUST NOT reference the live meter_config_t: appending a field
 * to it would silently change their sizes, no stored blob would match any
 * known schema, and a commissioned controller would fall back to defaults and
 * lose its Wi-Fi credentials. Never edit this struct. */
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
} legacy_meter_config_v3_t;

/*
 * THE INVERTER LAYOUT EVERY SCHEMA UP TO 7 STORED.
 *
 * Six frozen legacy layouts referenced the LIVE inverter_config_t, so none of
 * them was actually frozen: adding comms_failsafe_ms to the live struct silently
 * changed all six, and the _Static_assert chain caught it. Without that chain a
 * commissioned schema-6 blob would have been read with every inverter's fields
 * shifted by four bytes.
 *
 * A frozen layout must not name a live type. This is the shape as it stood
 * through schema 7, and it is what every legacy struct below uses.
 */
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
} legacy_inverter_config_v7_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    legacy_wifi_config_v1_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v3_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    legacy_control_config_v4_t control;
} legacy_app_config_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v3_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    legacy_control_config_v4_t control;
} legacy_app_config_v2_t;

/* Frozen schema 4 layouts. Same rule as the v3 snapshot above: these must never
 * reference the live meter_config_t or control_config_t, because growing either
 * would change these sizes and a stored blob would stop matching any schema. */
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
    uint8_t role;
    uint8_t generator_index;
} legacy_meter_config_v4_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v3_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    legacy_control_config_v4_t control;
    uint32_t wifi_provision_id;
} legacy_app_config_v3_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v4_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    legacy_control_config_v4_t control;
    uint32_t wifi_provision_id;
} legacy_app_config_v4_t;

/* Frozen schema 5 layouts, taken before meter_config_t gained `model`. Same rule
 * again: nothing here may reference a live struct. control_config_t had already
 * grown its two ramp profiles by schema 5, so that growth is frozen here too
 * rather than reusing legacy_control_config_v4_t. Never edit these structs. */
typedef struct {
    bool enabled;
    float up_percent_per_second;
    float down_percent_per_second;
} legacy_ramp_profile_v5_t;

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
    legacy_ramp_profile_v5_t grid_ramp;
    legacy_ramp_profile_v5_t generator_ramp;
} legacy_control_config_v5_t;

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
    uint8_t role;
    uint8_t generator_index;
} legacy_meter_config_v5_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v5_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    legacy_control_config_v5_t control;
    uint32_t wifi_provision_id;
} legacy_app_config_v5_t;


/* SCHEMA 6, FROZEN. Identical to the live meter_config_t except that it stops
 * before phase_control_basis. Frozen so a later growth of the live struct cannot
 * change how a stored schema-6 blob is read. */
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
    uint8_t role;
    uint8_t generator_index;
    uint32_t model;
} legacy_meter_config_v6_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    legacy_meter_config_v6_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
    uint32_t wifi_provision_id;
} legacy_app_config_v6_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    meter_config_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    legacy_inverter_config_v7_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
    uint32_t wifi_provision_id;
} legacy_app_config_v7_t;

/* The missing link in the chain, found by tests/phase_scope_source_contract.py
 * once it started checking every adjacent pair rather than only the newest.
 * Every other pair was asserted; schemas 1 and 2 never were. If they were the
 * same size a commissioned schema-1 blob would load as schema 2 with the
 * appended fields made of whatever followed in NVS. */
_Static_assert(sizeof(legacy_app_config_v2_t) > sizeof(legacy_app_config_v1_t),
               "schema 2 must stay distinguishable from schema 1 by blob size");
_Static_assert(sizeof(legacy_app_config_v3_t) == sizeof(legacy_app_config_v2_t) + sizeof(uint32_t),
               "schema 2 must remain a byte-exact prefix of schema 3");
_Static_assert(sizeof(legacy_app_config_v4_t) > sizeof(legacy_app_config_v3_t),
               "schema 4 must stay distinguishable from schema 3 by blob size");
_Static_assert(sizeof(legacy_app_config_v5_t) > sizeof(legacy_app_config_v4_t),
               "schema 5 must stay distinguishable from schema 4 by blob size");
/* THE ONE THAT CATCHES A NARROWED meter_config_t.model. role and generator_index
 * leave tail padding in meter_config_t, so an 8- or 16-bit model field is
 * absorbed by it and leaves sizeof(app_config_t) unchanged -- at which point a
 * commissioned schema-5 blob would load as schema 6 and every meter would silently
 * acquire whatever model value the padding bytes happened to hold. This assert
 * fails the build before that can ship. */
_Static_assert(sizeof(legacy_app_config_v6_t) > sizeof(legacy_app_config_v5_t),
               "schema 6 must stay distinguishable from schema 5 by blob size");
/* THE ONE THAT CATCHES A NARROWED meter_config_t.phase_control_basis, for exactly
 * the reason the model field needed one: a narrower field is absorbed by tail
 * padding, sizeof(app_config_t) does not change, and a commissioned schema-6 blob
 * then loads as schema 7 with every meter acquiring a control basis made of
 * padding bytes -- which decides whether a site's export limit is enforced on the
 * worst phase or on the total. */
_Static_assert(sizeof(legacy_app_config_v7_t) > sizeof(legacy_app_config_v6_t),
               "schema 7 must stay distinguishable from schema 6 by blob size");
/* THE ONE THAT CATCHES A NARROWED inverter_config_t.comms_failsafe_ms, for the
 * same reason the two above exist: a narrower field is absorbed by tail padding,
 * sizeof(app_config_t) does not change, and a commissioned schema-7 blob then
 * loads as schema 8 with every inverter acquiring a fail-safe timeout made of
 * padding bytes -- which decides whether the controller believes the machine
 * will protect itself when the link drops. */
_Static_assert(sizeof(app_config_t) > sizeof(legacy_app_config_v7_t),
               "schema 8 must stay distinguishable from schema 7 by blob size");

static void defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = APP_CONFIG_MAGIC;
    c->version = APP_CONFIG_VERSION;
    strlcpy(c->device_name, CONFIG_PVDG_DEVICE_NAME, sizeof(c->device_name));

    c->wifi.primary.enabled = true;
    strlcpy(c->wifi.primary.ssid, CONFIG_PVDG_PRIMARY_WIFI_SSID, sizeof(c->wifi.primary.ssid));
    strlcpy(c->wifi.primary.password, CONFIG_PVDG_PRIMARY_WIFI_PASSWORD, sizeof(c->wifi.primary.password));
    c->wifi.primary.ip_mode = APP_WIFI_IP_DHCP;

    /* The second saved station starts empty and disabled. A relocated unit gets
     * its extra networks from the operator through /api/wifi/config at runtime;
     * nothing is seeded from the build, so no site's SSID or passphrase can be
     * carried in the firmware image. */
    c->wifi.fallback.enabled = false;
    c->wifi.fallback.ssid[0] = '\0';
    c->wifi.fallback.password[0] = '\0';
    c->wifi.fallback.ip_mode = APP_WIFI_IP_DHCP;

    c->wifi.scan_before_connect = true;
    /* The recovery access point is not optional and is never a last resort: it
     * runs alongside the station for the life of the unit. */
    c->wifi.fallback_ap_enabled = true;
    /* Recovery access point.
     *
     * The password is deliberately left EMPTY here and generated per device on
     * first use (see ensure_recovery_ap_secret). It used to be a literal shared by
     * every unit, committed to a public repository -- which meant anyone who read
     * the repository could join the setup network of any controller in the field.
     * A default credential is not a default: it is a published one.
     *
     * The SSID carries the last three bytes of the station MAC so that units are
     * distinguishable on site. That is an identifier, not a secret, and it is safe
     * to derive from the MAC; the password is not, precisely because the
     * derivation would be public.
     *
     * The suffix comes from device_identity so that the AP name, the DHCP host
     * name and the mDNS label are all built from one definition of "which unit
     * is this" rather than three copies of the same MAC arithmetic. */
    char suffix[DEVICE_IDENTITY_SUFFIX_SIZE] = {0};
    if (device_identity_suffix(suffix, sizeof(suffix)) != ESP_OK) {
        strlcpy(suffix, "000000", sizeof(suffix));
    }
    snprintf(c->wifi.fallback_ap_ssid, sizeof(c->wifi.fallback_ap_ssid),
             RECOVERY_AP_SSID_BASE "-%s", suffix);
    strlcpy(c->wifi.fallback_ap_password, CONFIG_PVDG_RECOVERY_AP_PASSWORD,
            sizeof(c->wifi.fallback_ap_password));
    c->wifi.max_retries_per_profile = 5;
    c->wifi.reconnect_backoff_ms = 2000;

    for (uint8_t n = 0; n < APP_MAX_METERS; ++n) {
        c->meters[n].role = METER_ROLE_UNASSIGNED;
        c->meters[n].generator_index = METER_GENERATOR_INDEX_NONE;
    }
    c->meter_count = 1;
    meter_config_t *m = &c->meters[0];
    m->enabled = true;
    m->role = METER_ROLE_GRID;
    m->generator_index = METER_GENERATOR_INDEX_NONE;
    /* Deliberately left UNDECLARED even though the register mapping seeded below
     * is the EM500 one. The template is a starting point for commissioning, not
     * an observation of what is physically wired, and the whole point of this
     * field is that the model is stated by an engineer rather than inferred from
     * a register address. An out-of-box unit therefore reports "no meter model
     * declared" and the commissioning gate stays closed, which is where a unit
     * that has never been commissioned belongs. */
    m->model = METER_MODEL_UNDECLARED;
    strlcpy(m->name, "Grid Meter", sizeof(m->name));
    strlcpy(m->endpoint.host, CONFIG_PVDG_DEFAULT_ZLAN_HOST, sizeof(m->endpoint.host));
    m->endpoint.port = CONFIG_PVDG_DEFAULT_ZLAN_PORT;
    m->endpoint.unit_id = 1;
    /* Measured against the site EM500 through its ZLAN gateway, 129 back-to-back
     * transactions: mean 93 ms, p50 29 ms, p90 294 ms, max 319 ms. The latency is
     * bimodal -- 74 % complete under 50 ms and 24 % take over 250 ms -- which looks
     * like a periodic stall in the gateway or the meter's own update cycle rather
     * than jitter.
     *
     * The previous 300 ms was set from best-case figures and sat inside that tail:
     * about 3 % of perfectly good responses overran it and were recorded as
     * failures, which wastes the full timeout, triggers backoff, and feeds the
     * quality window that blocks control input when success drops below 80 %. A
     * healthy meter was being intermittently reported as unhealthy.
     *
     * 800 ms clears the measured tail with margin. It does not slow the control
     * loop: control reads the cached sample and applies its own staleness rule, and
     * the poll task runs below control on the same core. The cost is that a
     * genuinely dead endpoint takes 800 ms to declare, which the failure backoff
     * then spaces out anyway. */
    m->endpoint.timeout_ms = 800;
    m->function_code = 3;
    m->active_power_address = 57;
    m->active_power_type = MODBUS_DATA_INT32;
    m->active_power_order = MODBUS_ORDER_ABCD;
    m->active_power_scale = 0.00001f;
    /* Zero: issue the next read as soon as the previous transaction completes, so
     * the sample rate is set by the device and the network rather than by an
     * arbitrary period. An engineer can set any positive value to slow a bus or a
     * gateway that cannot sustain the rate. */
    m->poll_interval_ms = 0;

    c->inverter_count = 1;
    inverter_config_t *i = &c->inverters[0];
    strlcpy(i->name, "Inverter 1", sizeof(i->name));
    strlcpy(i->endpoint.host, "192.168.1.210", sizeof(i->endpoint.host));
    i->endpoint.port = 502;
    i->endpoint.unit_id = 1;
    i->endpoint.timeout_ms = 500;
    i->rated_power_kw = 100.0f;
    i->power_limit_function = 6;
    i->raw_units_per_percent = 100.0f;
    i->maximum_percent = 100.0f;

    c->control.enabled = false;
    c->control.grid_import_target_kw = 5.0f;
    c->control.deadband_kw = 2.0f;
    c->control.kp = 0.30f;
    c->control.ki = 0.05f;
    c->control.ramp_up_percent_per_second = 5.0f;
    c->control.ramp_down_percent_per_second = 20.0f;
    /* No machine to protect on the grid, so the command may step straight to
     * whatever the export policy allows. Disabling the ramp removes only the
     * rate limit - the policy target and safety clamps still apply. */
    c->control.grid_ramp.enabled = false;
    c->control.grid_ramp.up_percent_per_second = 100.0f;
    c->control.grid_ramp.down_percent_per_second = 100.0f;
    /* A generator must be ramped, and downward faster than upward: reducing PV
     * is the direction that protects against under-loading and reverse power. */
    c->control.generator_ramp.enabled = true;
    c->control.generator_ramp.up_percent_per_second = 5.0f;
    c->control.generator_ramp.down_percent_per_second = 20.0f;
    /* Fast but FIXED. A measured EM500 answers in under 40 ms, so 250 ms threw
     * most of the available responsiveness away. Deliberately not
     * poll-on-completion like acquisition: a control loop with a jittering period
     * has a jittering integral term, and determinism is worth more here than the
     * last few milliseconds. Commands are issued on change plus a keepalive, so a
     * fast loop does not mean fast Modbus writes. */
    c->control.interval_ms = 20;
    /* Four missed polls at the 250 ms default. Tightened from 3000 ms because
     * the measurement is now much fresher: a stale gate far longer than the
     * poll interval lets control keep acting on an old sample. Shorter is the
     * safe direction - it fails closed sooner. */
    c->control.meter_stale_timeout_ms = 1000;
    c->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
}

/* True when the active recovery passphrase is the one compiled into every image
 * built from this source, i.e. a passphrase that is public knowledge. Used to
 * raise the startup warning; it never reveals the value. */
bool config_manager_recovery_ap_is_build_default(const app_config_t *c)
{
    if (!c) return false;
    if (strlen(CONFIG_PVDG_RECOVERY_AP_PASSWORD) < DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH) return false;
    return strcmp(c->wifi.fallback_ap_password, CONFIG_PVDG_RECOVERY_AP_PASSWORD) == 0;
}

#ifndef CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING
static bool apply_build_provisioning(app_config_t *c)
{
    (void)c;
    return false;
}
#else
static bool apply_build_provisioning(app_config_t *c)
{
    if (CONFIG_PVDG_WIFI_PROVISION_ID <= 0) return false;
    if ((uint32_t)CONFIG_PVDG_WIFI_PROVISION_ID <= c->wifi_provision_id) return false;

    const char *ssid = CONFIG_PVDG_PRIMARY_WIFI_SSID;
    if (!ssid[0]) {
        ESP_LOGW(TAG, "Build provisioning %d ignored: no primary SSID compiled in",
                 CONFIG_PVDG_WIFI_PROVISION_ID);
        return false;
    }

    strlcpy(c->wifi.primary.ssid, ssid, sizeof(c->wifi.primary.ssid));
    strlcpy(c->wifi.primary.password, CONFIG_PVDG_PRIMARY_WIFI_PASSWORD, sizeof(c->wifi.primary.password));
    c->wifi.primary.enabled = true;
    c->wifi.primary.ip_mode = APP_WIFI_IP_DHCP;

    const char *fallback_ssid = CONFIG_PVDG_DEFAULT_WIFI_SSID;
    strlcpy(c->wifi.fallback.ssid, fallback_ssid, sizeof(c->wifi.fallback.ssid));
    strlcpy(c->wifi.fallback.password, CONFIG_PVDG_DEFAULT_WIFI_PASSWORD, sizeof(c->wifi.fallback.password));
    c->wifi.fallback.enabled = fallback_ssid[0] != '\0' && strcmp(fallback_ssid, ssid) != 0;

    c->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
    ESP_LOGW(TAG, "Applied build provisioning %d: primary SSID '%s', fallback '%s'%s",
             CONFIG_PVDG_WIFI_PROVISION_ID, c->wifi.primary.ssid,
             c->wifi.fallback.ssid, c->wifi.fallback.enabled ? "" : " (disabled)");
    return true;
}
#endif

static bool profile_valid(const app_wifi_sta_profile_t *p)
{
    if (!p->enabled) return true;
    if (!p->ssid[0] || p->ip_mode > APP_WIFI_IP_STATIC) return false;
    if (p->ip_mode == APP_WIFI_IP_STATIC &&
        (!p->static_ip[0] || !p->gateway[0] || !p->netmask[0])) return false;
    return true;
}

static bool endpoint_valid(const modbus_endpoint_t *e)
{
    return e && e->host[0] && e->port > 0 && e->unit_id > 0 && e->unit_id <= 247 &&
           e->timeout_ms >= 100U && e->timeout_ms <= 60000U;
}

static bool meter_valid(const meter_config_t *m)
{
    if (m->role > METER_ROLE_PV) return false;
    /* Range check only. Whether the declared model is IN SCOPE for this release
     * phase is a commissioning-gate question, not a validity question: an
     * out-of-scope model is a perfectly well-formed configuration that the gate
     * refuses to commission, and rejecting it here would discard the blob and
     * take the commissioned Wi-Fi credentials with it. Only a value no build ever
     * wrote -- corruption -- fails this. */
    if (m->model >= (uint32_t)METER_MODEL_COUNT) return false;
    if (m->role == METER_ROLE_GENERATOR) {
        if (m->generator_index >= APP_MAX_GENERATORS) return false;
    } else if (m->generator_index != METER_GENERATOR_INDEX_NONE) {
        return false;
    }
    if (!m->enabled) return true;
    return endpoint_valid(&m->endpoint) && (m->function_code == 3 || m->function_code == 4) &&
           m->active_power_type <= MODBUS_DATA_FLOAT32 && m->active_power_order <= MODBUS_ORDER_DCBA &&
           isfinite(m->active_power_scale) && m->active_power_scale != 0.0f &&
           /* Zero is legal and means "poll again as soon as the previous
            * transaction completes", so acquisition runs at the rate the device
            * answers rather than at an arbitrary period. The old 100 ms floor made
            * that unconfigurable. The upper bound is a sanity limit, not policy:
            * an hour between polls is a typo, not an intention. */
           m->poll_interval_ms <= 3600000U;
}

/* Deliberately NOT part of valid(): a configuration that fails the role rules is
 * still a configuration worth keeping. Discarding it would fall back to defaults
 * and lose commissioned Wi-Fi credentials. Instead the assignment is reported so
 * control can stay fail-closed until an engineer resolves it. */
const char *meter_model_name(uint32_t model)
{
    switch (model) {
    case METER_MODEL_EM500_LOVATO: return "em500_lovato";
    case METER_MODEL_GENERIC_MODBUS: return "generic_modbus";
    case METER_MODEL_UNDECLARED:
    default: return "undeclared";
    }
}

const char *meter_role_name(uint8_t role)
{
    switch (role) {
    case METER_ROLE_GRID: return "grid";
    case METER_ROLE_GENERATOR: return "generator";
    case METER_ROLE_LOAD: return "load";
    case METER_ROLE_PV: return "pv";
    case METER_ROLE_UNASSIGNED:
    default: return "unassigned";
    }
}

meter_role_assignment_t config_manager_role_assignment(const app_config_t *c)
{
    meter_role_assignment_t out = {
        .grid_index = METER_ROLE_INDEX_NONE,
        .grid_count = 0U,
        .generator_count = 0U,
    };
    for (uint8_t n = 0; n < APP_MAX_GENERATORS; ++n) out.generator_index[n] = METER_ROLE_INDEX_NONE;
    if (!c) return out;

    for (uint8_t n = 0; n < c->meter_count && n < APP_MAX_METERS; ++n) {
        if (!c->meters[n].enabled) continue;
        if (c->meters[n].role == METER_ROLE_GRID) {
            if (out.grid_count == 0U) out.grid_index = n;
            out.grid_count++;
        } else if (c->meters[n].role == METER_ROLE_GENERATOR) {
            const uint8_t slot = c->meters[n].generator_index;
            if (slot < APP_MAX_GENERATORS && out.generator_index[slot] == METER_ROLE_INDEX_NONE) {
                out.generator_index[slot] = n;
                out.generator_count++;
            } else {
                /* Duplicate or out-of-range generator slot: refuse to guess. */
                out.duplicate_generator = true;
            }
        }
    }
    out.valid = out.grid_count == 1U && !out.duplicate_generator;
    return out;
}

static bool inverter_valid(const inverter_config_t *i)
{
    if (!i->enabled) return true;
    return endpoint_valid(&i->endpoint) && isfinite(i->rated_power_kw) && i->rated_power_kw > 0.0f &&
           isfinite(i->raw_units_per_percent) && i->raw_units_per_percent > 0.0f &&
           isfinite(i->minimum_percent) && isfinite(i->maximum_percent) &&
           i->minimum_percent >= 0.0f && i->maximum_percent >= i->minimum_percent &&
           i->maximum_percent <= 100.0f &&
           (i->power_limit_function == 0 || i->power_limit_function == 6 || i->power_limit_function == 16) &&
           /*
            * THE ORDERING BETWEEN TWO INDEPENDENT SAFETIES.
            *
            * The inverter reverts to its own safe output after
            * comms_failsafe_ms of silence. The controller drops a silent
            * inverter from the commandable fleet after
            * INVERTER_COMMS_FAIL_GRACE_MS. The controller must wait LONGER.
            *
            * Get it the other way round and the controller stops counting a
            * machine that is still holding the last setpoint it was given, and
            * goes on holding it -- uncurtailed -- until its own timer runs out.
            * The controller meanwhile redistributes that capacity to the
            * remaining inverters, so the plant produces more than the setpoint
            * it believes it is enforcing. On an export-limited site that is the
            * export it exists to prevent.
            *
            * Zero means NOT STATED and is accepted: seven of eight brands do not
            * document this, so refusing an unstated value would make most
            * plants uncommissionable. What is refused is a STATED value that
            * puts the two safeties in the dangerous order.
            */
           (i->comms_failsafe_ms == 0U ||
            i->comms_failsafe_ms < INVERTER_COMMS_FAIL_GRACE_MS);
}

static bool ramp_profile_valid(const ramp_profile_t *r)
{
    /* A rate of zero with the ramp enabled would freeze the command forever, so
     * an enabled profile must be able to move in both directions. */
    if (!isfinite(r->up_percent_per_second) || !isfinite(r->down_percent_per_second) ||
        r->up_percent_per_second < 0.0f || r->down_percent_per_second < 0.0f ||
        r->up_percent_per_second > 10000.0f || r->down_percent_per_second > 10000.0f) {
        return false;
    }
    if (r->enabled && (r->up_percent_per_second <= 0.0f || r->down_percent_per_second <= 0.0f)) {
        return false;
    }
    return true;
}

static bool control_valid(const control_config_t *c)
{
    return isfinite(c->grid_import_target_kw) && isfinite(c->deadband_kw) &&
           isfinite(c->kp) && isfinite(c->ki) &&
           isfinite(c->ramp_up_percent_per_second) && isfinite(c->ramp_down_percent_per_second) &&
           c->deadband_kw >= 0.0f && c->kp >= 0.0f && c->ki >= 0.0f &&
           c->ramp_up_percent_per_second >= 0.0f && c->ramp_down_percent_per_second >= 0.0f &&
           c->interval_ms >= 50U && c->meter_stale_timeout_ms >= 100U &&
           ramp_profile_valid(&c->grid_ramp) && ramp_profile_valid(&c->generator_ramp);
}

static bool valid(const app_config_t *c)
{
    if (!c || c->magic != APP_CONFIG_MAGIC || c->version != APP_CONFIG_VERSION ||
        !profile_valid(&c->wifi.primary) || !profile_valid(&c->wifi.fallback) ||
        !c->wifi.primary.enabled || c->wifi.max_retries_per_profile == 0 ||
        c->wifi.max_retries_per_profile > 20 || c->wifi.reconnect_backoff_ms < 500U ||
        c->wifi.reconnect_backoff_ms > 60000U || c->meter_count > APP_MAX_METERS ||
        c->inverter_count > APP_MAX_INVERTERS || !control_valid(&c->control)) return false;

    for (uint8_t n = 0; n < c->meter_count; ++n) if (!meter_valid(&c->meters[n])) return false;
    for (uint8_t n = 0; n < c->inverter_count; ++n) if (!inverter_valid(&c->inverters[n])) return false;
    return true;
}

/* Schema 3 and earlier had no meter role. A commissioned controller with a
 * single meter was, by construction, measuring the grid - that is the only
 * meter the control engine ever read. Anything beyond the first is left
 * unassigned so an engineer must state what it measures rather than the
 * upgrade guessing and the control loop silently regulating against it. */
static void upgrade_meters_from_v3(meter_config_t *next, uint8_t count,
                                   const legacy_meter_config_v3_t *old)
{
    for (uint8_t n = 0; n < APP_MAX_METERS; ++n) {
        next[n].enabled = old[n].enabled;
        memcpy(next[n].name, old[n].name, sizeof(next[n].name));
        next[n].endpoint = old[n].endpoint;
        next[n].function_code = old[n].function_code;
        next[n].active_power_address = old[n].active_power_address;
        next[n].active_power_type = old[n].active_power_type;
        next[n].active_power_order = old[n].active_power_order;
        next[n].active_power_scale = old[n].active_power_scale;
        next[n].poll_interval_ms = old[n].poll_interval_ms;
        next[n].role = (n == 0U && count > 0U) ? METER_ROLE_GRID : METER_ROLE_UNASSIGNED;
        next[n].generator_index = METER_GENERATOR_INDEX_NONE;
        /* An upgrade never invents a meter model. The old blob carried no such
         * fact, so the migrated one carries none either and an engineer must
         * state it. Guessing EM500 here because the scale looks familiar is
         * exactly the inference this field exists to abolish. */
        next[n].model = METER_MODEL_UNDECLARED;
    }
}

/* Schema 4 and earlier had a single ramp rate pair applied in every source mode.
 * Those rates were tuned for a generator, so they seed the generator profile.
 * The grid profile starts disabled: with no machine to protect there is no
 * reason to rate-limit, and disabling the ramp removes only the rate limit -
 * the export policy and every safety clamp still apply. */
static void upgrade_control_from_v4(control_config_t *next,
                                    const legacy_control_config_v4_t *old)
{
    next->enabled = false;   /* an upgrade never arms automatic control */
    next->grid_import_target_kw = old->grid_import_target_kw;
    next->deadband_kw = old->deadband_kw;
    next->kp = old->kp;
    next->ki = old->ki;
    next->ramp_up_percent_per_second = old->ramp_up_percent_per_second;
    next->ramp_down_percent_per_second = old->ramp_down_percent_per_second;
    next->interval_ms = old->interval_ms;
    next->meter_stale_timeout_ms = old->meter_stale_timeout_ms;

    next->generator_ramp.enabled = true;
    next->generator_ramp.up_percent_per_second = old->ramp_up_percent_per_second;
    next->generator_ramp.down_percent_per_second = old->ramp_down_percent_per_second;

    next->grid_ramp.enabled = false;
    next->grid_ramp.up_percent_per_second = old->ramp_up_percent_per_second;
    next->grid_ramp.down_percent_per_second = old->ramp_down_percent_per_second;
}

static void migrate_v1(const legacy_app_config_v1_t *old, app_config_t *next)
{
    defaults(next);
    if (!old || old->magic != APP_CONFIG_MAGIC || old->version != 1) return;
    strlcpy(next->device_name, old->device_name, sizeof(next->device_name));
    if (old->wifi.ssid[0]) {
        strlcpy(next->wifi.primary.ssid, old->wifi.ssid, sizeof(next->wifi.primary.ssid));
        strlcpy(next->wifi.primary.password, old->wifi.password, sizeof(next->wifi.primary.password));
    }
    next->meter_count = old->meter_count <= APP_MAX_METERS ? old->meter_count : APP_MAX_METERS;
    upgrade_meters_from_v3(next->meters, next->meter_count, old->meters);
    next->inverter_count = old->inverter_count <= APP_MAX_INVERTERS ? old->inverter_count : APP_MAX_INVERTERS;
    memcpy(next->inverters, old->inverters, sizeof(next->inverters));
    upgrade_control_from_v4(&next->control, &old->control);
    next->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
    if (strcmp(next->wifi.fallback.ssid, next->wifi.primary.ssid) == 0) next->wifi.fallback.enabled = false;
}

static void set_active(const app_config_t *c)
{
    portENTER_CRITICAL(&s_lock);
    s_cfg = *c;
    portEXIT_CRITICAL(&s_lock);
}

/* Gives this unit its own recovery-AP password if it does not have one.
 *
 * Generated randomly, per device, on first use -- NOT derived from the MAC or any
 * other published value, because the derivation would itself be in this public
 * repository and a computable password is not a password.
 *
 * It is logged once, at the moment of generation, and never again. That is a
 * deliberate exception to "never log a credential": the operator has to learn it
 * somehow, and the serial console is the same physical-presence channel the
 * one-time Engineering setup code already uses. Reading it costs physical access to
 * the board.
 *
 * Returns true if the configuration was changed and needs saving. */
/* FNV-1a 64 of the recovery-AP password this firmware used to ship with, which was
 * identical on every unit and published in a public repository.
 *
 * Held as a hash rather than the string so the retired credential is not printed in
 * this source again, while a unit already carrying it can still recognise and rotate
 * it. Without this a commissioned unit would keep the published password forever,
 * because it is stored and therefore not empty -- fixing the default alone fixes
 * only units that have never been commissioned. */
#define RETIRED_RECOVERY_AP_PASSWORD_FNV1A64 0x70D9019AAA1F69DDULL

static uint64_t fnv1a64(const char *text)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        hash ^= (uint64_t)*p;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static bool ensure_recovery_ap_secret(app_config_t *c)
{
    bool changed = false;

    /* The recovery access point is the reachability guarantee for a controller
     * that has been relocated to a site whose Wi-Fi it does not know. It is not
     * a last resort and it is not optional, so a stored "disabled" is corrected
     * here rather than honoured. This used to early-return, which meant a unit
     * that had ever been saved with the AP off could never be reached again
     * except by reflashing. */
    if (!c->wifi.fallback_ap_enabled) {
        ESP_LOGW(TAG, "Stored configuration had the recovery access point disabled; "
                      "re-enabling it. It is the only guaranteed way back into a "
                      "relocated controller and cannot be switched off.");
        c->wifi.fallback_ap_enabled = true;
        changed = true;
    }

    /* A passphrase shorter than the WPA2 minimum cannot secure the AP at all, so
     * it counts as absent and is replaced rather than allowed to force the
     * network open. */
    const bool absent = strlen(c->wifi.fallback_ap_password) < DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH;
    const bool retired = !absent &&
                         fnv1a64(c->wifi.fallback_ap_password) ==
                             RETIRED_RECOVERY_AP_PASSWORD_FNV1A64;
    if (retired) {
        ESP_LOGW(TAG, "This unit is using the retired shared recovery-AP password, which "
                      "was published. Rotating it to one unique to this unit.");
    }
    if (!absent && !retired) return changed;

    /* Character set excludes look-alikes (0/O, 1/l/I) because this gets read off a
     * console and typed into a phone. */
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzACDEFGHJKLMNPQRSTUVWXYZ23456789";
    char secret[17];
    uint8_t random_bytes[sizeof(secret) - 1];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(secret) - 1U; ++i) {
        secret[i] = alphabet[random_bytes[i] % (sizeof(alphabet) - 1U)];
    }
    secret[sizeof(secret) - 1U] = '\0';
    strlcpy(c->wifi.fallback_ap_password, secret, sizeof(c->wifi.fallback_ap_password));

    /* The generated value is deliberately NOT printed here. There is exactly one
     * place in this firmware that discloses the recovery passphrase --
     * announce_recovery_ap_on_serial() in network_manager.c, which prints the
     * ACTIVE passphrase to the serial console at every boot and explains why it
     * is allowed to. Printing here as well would be a second disclosure site to
     * audit, and a strictly worse one: it fires only at the moment of
     * generation, so an engineer who missed that one boot could never recover
     * the value. */
    ESP_LOGW(TAG, "Recovery access point '%s' had no usable password. Generated one unique "
                  "to this unit and stored it; it is printed to this console at every boot.",
             c->wifi.fallback_ap_ssid);
    memset(secret, 0, sizeof(secret));
    memset(random_bytes, 0, sizeof(random_bytes));
    return true;
}

/* Brings the saved station profiles into line with what they actually describe.
 *
 * A controller was found in the field reporting
 *     primary 'X' not visible, fallback '(disabled)' not visible
 * while a network it already had credentials for was in range. The secondary
 * profile held a usable SSID but carried enabled = false, so the connection
 * sweep skipped it and the unit sat on its recovery AP instead of rejoining a
 * network it knew.
 *
 * The rule is that "enabled" describes whether a profile is usable, not a
 * separate switch an operator has to remember to set:
 *   - a profile holding an SSID is available to the sweep, so it is enabled;
 *   - a profile holding no SSID cannot be attempted, so it is disabled and the
 *     sweep does not waste radio time on it.
 *
 * Only these two flags are touched. No SSID, passphrase, IP mode or static
 * address is read, written or cleared here, so a commissioned unit keeps every
 * credential it was given.
 *
 * Returns true when something changed, so the caller persists it. */
static bool normalize_station_profiles(app_config_t *c)
{
    app_wifi_sta_profile_t *const profiles[] = {&c->wifi.primary, &c->wifi.fallback};
    static const char *const names[] = {"primary", "secondary"};
    bool changed = false;

    for (size_t n = 0; n < sizeof(profiles) / sizeof(profiles[0]); ++n) {
        const bool usable = profiles[n]->ssid[0] != '\0';
        if (profiles[n]->enabled == usable) continue;
        if (usable) {
            ESP_LOGW(TAG, "Saved %s station '%s' was disabled; enabling it so a known "
                          "network is rejoined before the unit settles for its recovery AP.",
                     names[n], profiles[n]->ssid);
        }
        profiles[n]->enabled = usable;
        changed = true;
    }
    return changed;
}

esp_err_t config_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s); erasing only the NVS partition", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS init failed");

    app_config_t *loaded = calloc(1, sizeof(*loaded));
    if (!loaded) return ESP_ERR_NO_MEM;
    bool have_valid_config = false;
    bool stored_matches = false;
    nvs_handle_t h;

    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t stored_size = 0;
        err = nvs_get_blob(h, KEY, NULL, &stored_size);
        if (err == ESP_OK && stored_size == sizeof(*loaded)) {
            size_t size = sizeof(*loaded);
            err = nvs_get_blob(h, KEY, loaded, &size);
            have_valid_config = err == ESP_OK && valid(loaded);
            stored_matches = have_valid_config;
            if (!have_valid_config) ESP_LOGW(TAG, "Stored configuration rejected by validation");
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v7_t)) {
            /* Schema 7 to 8: the only new fact is
             * inverter_config_t.comms_failsafe_ms -- how long the MACHINE waits
             * before reverting to its own safe output when the controller stops
             * talking to it.
             *
             * It migrates to ZERO, which means NOT STATED, and that is the only
             * honest value. It is a setting on the inverter, seven of eight
             * brands do not document a default, and inventing one would make the
             * controller believe a machine protects itself on a timescale nobody
             * has verified. Zero disables the ordering CHECK; it does not claim
             * the ordering holds.
             *
             * Nothing else changes and no commissioned value is lost, so no site
             * has to be re-commissioned for this upgrade. */
            legacy_app_config_v7_t *legacy = malloc(sizeof(*legacy));
            if (!legacy) {
                nvs_close(h);
                free(loaded);
                return ESP_ERR_NO_MEM;
            }
            size_t legacy_size = sizeof(*legacy);
            err = nvs_get_blob(h, KEY, legacy, &legacy_size);
            if (err == ESP_OK) {
                memset(loaded, 0, sizeof(*loaded));
                loaded->magic = legacy->magic;
                loaded->version = APP_CONFIG_VERSION;
                memcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                loaded->wifi = legacy->wifi;
                loaded->meter_count = legacy->meter_count;
                memcpy(loaded->meters, legacy->meters, sizeof(loaded->meters));
                loaded->inverter_count = legacy->inverter_count;
                for (uint8_t i = 0; i < APP_MAX_INVERTERS; ++i) {
                    const legacy_inverter_config_v7_t *old_inverter = &legacy->inverters[i];
                    inverter_config_t *next = &loaded->inverters[i];
                    next->enabled = old_inverter->enabled;
                    memcpy(next->name, old_inverter->name, sizeof(next->name));
                    next->endpoint = old_inverter->endpoint;
                    next->rated_power_kw = old_inverter->rated_power_kw;
                    next->power_limit_address = old_inverter->power_limit_address;
                    next->power_limit_function = old_inverter->power_limit_function;
                    next->raw_units_per_percent = old_inverter->raw_units_per_percent;
                    next->minimum_percent = old_inverter->minimum_percent;
                    next->maximum_percent = old_inverter->maximum_percent;
                    /* Not stated. See the field comment. */
                    next->comms_failsafe_ms = 0U;
                }
                loaded->control = legacy->control;
                loaded->wifi_provision_id = legacy->wifi_provision_id;
                have_valid_config = valid(loaded);
                if (have_valid_config) {
                    ESP_LOGW(TAG, "migrated schema 7 configuration to schema %u; "
                                  "each inverter's own comms fail-safe is NOT STATED "
                                  "until an engineer enters it",
                             (unsigned)APP_CONFIG_VERSION);
                }
            }
            free(legacy);
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v6_t)) {
            /* Schema 6 to 7: the only new fact is meter_config_t.phase_control_basis,
             * and unlike the model it CAN be defaulted safely, because both values
             * are legitimate configurations rather than one being "unknown".
             *
             * It migrates to LOWEST_PHASE, the stricter of the two: a limit that
             * holds on the worst conductor holds on all three. An upgraded unit
             * therefore gets a tighter guarantee than it had, never a looser one,
             * and where the per-phase registers are not known the runtime selection
             * falls back to the total and says so. Defaulting to TOTAL would have
             * silently given every upgraded site the weaker guarantee.
             *
             * Nothing else changes, so the commissioning gate does not close and no
             * site has to be re-commissioned for this upgrade. */
            legacy_app_config_v6_t *legacy = malloc(sizeof(*legacy));
            if (!legacy) {
                nvs_close(h);
                free(loaded);
                return ESP_ERR_NO_MEM;
            }
            size_t size = sizeof(*legacy);
            if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                legacy->magic == APP_CONFIG_MAGIC && legacy->version == 6) {
                loaded->magic = legacy->magic;
                loaded->version = APP_CONFIG_VERSION;
                strlcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                loaded->wifi = legacy->wifi;
                loaded->meter_count = legacy->meter_count <= APP_MAX_METERS
                                          ? legacy->meter_count : APP_MAX_METERS;
                for (uint8_t n = 0; n < APP_MAX_METERS; ++n) {
                    /* Field by field from the FROZEN layout, never memcpy'd: the
                     * live struct is longer, and a bulk copy sized by it would read
                     * past the end of the stored blob. */
                    loaded->meters[n].enabled = legacy->meters[n].enabled;
                    strlcpy(loaded->meters[n].name, legacy->meters[n].name,
                            sizeof(loaded->meters[n].name));
                    loaded->meters[n].endpoint = legacy->meters[n].endpoint;
                    loaded->meters[n].function_code = legacy->meters[n].function_code;
                    loaded->meters[n].active_power_address = legacy->meters[n].active_power_address;
                    loaded->meters[n].active_power_type = legacy->meters[n].active_power_type;
                    loaded->meters[n].active_power_order = legacy->meters[n].active_power_order;
                    loaded->meters[n].active_power_scale = legacy->meters[n].active_power_scale;
                    loaded->meters[n].poll_interval_ms = legacy->meters[n].poll_interval_ms;
                    loaded->meters[n].role = legacy->meters[n].role;
                    loaded->meters[n].generator_index = legacy->meters[n].generator_index;
                    loaded->meters[n].model = legacy->meters[n].model;
                    loaded->meters[n].phase_control_basis = METER_PHASE_BASIS_LOWEST_PHASE;
                }
                loaded->inverter_count = legacy->inverter_count <= APP_MAX_INVERTERS
                                             ? legacy->inverter_count : APP_MAX_INVERTERS;
                memcpy(loaded->inverters, legacy->inverters, sizeof(loaded->inverters));
                loaded->control = legacy->control;
                loaded->control.enabled = false; /* an upgrade never arms control */
                loaded->wifi_provision_id = legacy->wifi_provision_id;
                have_valid_config = valid(loaded);
                if (have_valid_config) {
                    ESP_LOGW(TAG, "Migrated configuration schema 6 -> 7; every meter "
                                  "defaults to lowest-phase control basis");
                }
            }
            free(legacy);
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v5_t)) {
            /* Schema 5 to 6: the only new fact is meter_config_t.model, and it is
             * not derivable from anything schema 5 stored. Every meter therefore
             * migrates as UNDECLARED, which leaves an upgraded unit reporting "no
             * meter model declared" until an engineer states it. That closes the
             * commissioning gate on a plant that was previously commissioned, and
             * it is the intended cost: the alternative is assuming every already
             * commissioned meter is an EM500 and silently applying EM500 bitmask
             * semantics to whatever is actually wired. Commissioned Wi-Fi
             * credentials, meter endpoints and control tuning are all preserved,
             * so recovering is one form field and not a re-commissioning. */
            legacy_app_config_v5_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                    legacy->magic == APP_CONFIG_MAGIC && legacy->version == 5) {
                    loaded->magic = legacy->magic;
                    loaded->version = APP_CONFIG_VERSION;
                    strlcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                    loaded->wifi = legacy->wifi;
                    loaded->meter_count = legacy->meter_count <= APP_MAX_METERS
                                              ? legacy->meter_count : APP_MAX_METERS;
                    for (uint8_t n = 0; n < APP_MAX_METERS; ++n) {
                        memcpy(&loaded->meters[n], &legacy->meters[n],
                               sizeof(legacy->meters[n]));
                        loaded->meters[n].model = METER_MODEL_UNDECLARED;
                    }
                    loaded->inverter_count = legacy->inverter_count <= APP_MAX_INVERTERS
                                                 ? legacy->inverter_count : APP_MAX_INVERTERS;
                    memcpy(loaded->inverters, legacy->inverters, sizeof(loaded->inverters));
                    /* control_config_t is unchanged between schema 5 and 6, but it
                     * is copied field by field from the FROZEN legacy layout rather
                     * than memcpy'd, so a later growth of the live struct cannot
                     * turn this into an out-of-bounds read. */
                    loaded->control.enabled = false; /* an upgrade never arms control */
                    loaded->control.grid_import_target_kw = legacy->control.grid_import_target_kw;
                    loaded->control.deadband_kw = legacy->control.deadband_kw;
                    loaded->control.kp = legacy->control.kp;
                    loaded->control.ki = legacy->control.ki;
                    loaded->control.ramp_up_percent_per_second =
                        legacy->control.ramp_up_percent_per_second;
                    loaded->control.ramp_down_percent_per_second =
                        legacy->control.ramp_down_percent_per_second;
                    loaded->control.interval_ms = legacy->control.interval_ms;
                    loaded->control.meter_stale_timeout_ms = legacy->control.meter_stale_timeout_ms;
                    loaded->control.grid_ramp.enabled = legacy->control.grid_ramp.enabled;
                    loaded->control.grid_ramp.up_percent_per_second =
                        legacy->control.grid_ramp.up_percent_per_second;
                    loaded->control.grid_ramp.down_percent_per_second =
                        legacy->control.grid_ramp.down_percent_per_second;
                    loaded->control.generator_ramp.enabled = legacy->control.generator_ramp.enabled;
                    loaded->control.generator_ramp.up_percent_per_second =
                        legacy->control.generator_ramp.up_percent_per_second;
                    loaded->control.generator_ramp.down_percent_per_second =
                        legacy->control.generator_ramp.down_percent_per_second;
                    loaded->wifi_provision_id = legacy->wifi_provision_id;
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 5 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 5 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v4_t)) {
            legacy_app_config_v4_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                    legacy->magic == APP_CONFIG_MAGIC && legacy->version == 4) {
                    loaded->magic = legacy->magic;
                    loaded->version = APP_CONFIG_VERSION;
                    strlcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                    loaded->wifi = legacy->wifi;
                    loaded->meter_count = legacy->meter_count <= APP_MAX_METERS
                                              ? legacy->meter_count : APP_MAX_METERS;
                    /* Schema 4 already carries roles, so meters copy straight across.
                     * The copy is sized by the LEGACY struct, so the schema 6
                     * model field is not touched by it and is set explicitly to
                     * UNDECLARED below rather than inheriting stale bytes. */
                    for (uint8_t n = 0; n < APP_MAX_METERS; ++n) {
                        memcpy(&loaded->meters[n], &legacy->meters[n],
                               sizeof(legacy->meters[n]));
                        loaded->meters[n].model = METER_MODEL_UNDECLARED;
                    }
                    loaded->inverter_count = legacy->inverter_count <= APP_MAX_INVERTERS
                                                 ? legacy->inverter_count : APP_MAX_INVERTERS;
                    memcpy(loaded->inverters, legacy->inverters, sizeof(loaded->inverters));
                    upgrade_control_from_v4(&loaded->control, &legacy->control);
                    loaded->wifi_provision_id = legacy->wifi_provision_id;
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 4 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 4 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v3_t)) {
            legacy_app_config_v3_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                    legacy->magic == APP_CONFIG_MAGIC && legacy->version == 3) {
                    loaded->magic = legacy->magic;
                    loaded->version = APP_CONFIG_VERSION;
                    strlcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                    loaded->wifi = legacy->wifi;
                    loaded->meter_count = legacy->meter_count <= APP_MAX_METERS
                                              ? legacy->meter_count : APP_MAX_METERS;
                    upgrade_meters_from_v3(loaded->meters, loaded->meter_count, legacy->meters);
                    loaded->inverter_count = legacy->inverter_count <= APP_MAX_INVERTERS
                                                 ? legacy->inverter_count : APP_MAX_INVERTERS;
                    memcpy(loaded->inverters, legacy->inverters, sizeof(loaded->inverters));
                    upgrade_control_from_v4(&loaded->control, &legacy->control);
                    /* Schema 3 already carries commissioned Wi-Fi and its own
                     * provisioning generation; preserve it so an upgrade cannot
                     * replay build credentials over what an operator set. */
                    loaded->wifi_provision_id = legacy->wifi_provision_id;
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 3 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 3 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v2_t)) {
            legacy_app_config_v2_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK &&
                    legacy->magic == APP_CONFIG_MAGIC && legacy->version == 2) {
                    loaded->magic = legacy->magic;
                    loaded->version = APP_CONFIG_VERSION;
                    strlcpy(loaded->device_name, legacy->device_name, sizeof(loaded->device_name));
                    loaded->wifi = legacy->wifi;
                    loaded->meter_count = legacy->meter_count <= APP_MAX_METERS
                                              ? legacy->meter_count : APP_MAX_METERS;
                    upgrade_meters_from_v3(loaded->meters, loaded->meter_count, legacy->meters);
                    loaded->inverter_count = legacy->inverter_count <= APP_MAX_INVERTERS
                                                 ? legacy->inverter_count : APP_MAX_INVERTERS;
                    memcpy(loaded->inverters, legacy->inverters, sizeof(loaded->inverters));
                    upgrade_control_from_v4(&loaded->control, &legacy->control);
                    /* Schema 2 already contains commissioned Wi-Fi. Treat the
                     * current build provisioning generation as consumed so an
                     * upgrade cannot overwrite those credentials. */
                    loaded->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID;
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 2 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 2 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK && stored_size == sizeof(legacy_app_config_v1_t)) {
            legacy_app_config_v1_t *legacy = malloc(sizeof(*legacy));
            if (legacy) {
                size_t size = sizeof(*legacy);
                if (nvs_get_blob(h, KEY, legacy, &size) == ESP_OK) {
                    migrate_v1(legacy, loaded);
                    have_valid_config = valid(loaded);
                    if (have_valid_config) ESP_LOGI(TAG, "Migrated configuration schema 1 to schema %u", APP_CONFIG_VERSION);
                    else ESP_LOGW(TAG, "Schema 1 migration produced an invalid configuration; discarding it");
                }
                free(legacy);
            }
        } else if (err == ESP_OK) {
            ESP_LOGW(TAG, "Stored configuration blob is %u bytes but no known schema matches; discarding it",
                     (unsigned)stored_size);
        }
        nvs_close(h);
    }

    if (!have_valid_config) {
        defaults(loaded);
        ESP_LOGW(TAG, "No valid stored configuration; safe defaults loaded (primary SSID '%s')",
                 loaded->wifi.primary.ssid);
    } else if (apply_build_provisioning(loaded)) {
        stored_matches = false;
    }

    /* Applies to a freshly defaulted unit AND to a commissioned one carrying the
     * old shared password, so an existing unit in the field stops using the
     * published credential the first time it runs this firmware.
     *
     * Runs after migration and after any build provisioning, so an upgraded unit
     * ends up with an enabled, secured recovery AP without its commissioned
     * station credentials being touched. */
    if (ensure_recovery_ap_secret(loaded)) stored_matches = false;
    if (normalize_station_profiles(loaded)) stored_matches = false;

    set_active(loaded);
    if (!stored_matches) {
        err = config_manager_save(loaded);
        if (err != ESP_OK) ESP_LOGE(TAG, "Configuration persistence failed (%s); continuing with in-memory configuration", esp_err_to_name(err));
    }
    free(loaded);
    return ESP_OK;
}

esp_err_t config_manager_get_snapshot(app_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    *out = s_cfg;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t config_manager_save(const app_config_t *c)
{
    if (!valid(c)) return ESP_ERR_INVALID_ARG;
    /* Persistence gate for the always-on access point. Nothing may be written
     * that would bring the AP up disabled or without a WPA2 passphrase, so no
     * save path - API, import or internal - can turn the controller's only
     * guaranteed way in into an open, unauthenticated network.
     *
     * This is checked here rather than in valid() on purpose: valid() also
     * decides whether a stored blob is kept, and a commissioned unit must never
     * lose its station credentials over a recovery-AP field. */
    if (!c->wifi.fallback_ap_enabled ||
        strlen(c->wifi.fallback_ap_password) < DEVICE_IDENTITY_MIN_PASSPHRASE_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "NVS open failed");
    esp_err_t err = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    app_config_t *verify = malloc(sizeof(*verify));
    if (!verify) return ESP_ERR_NO_MEM;
    size_t verify_size = sizeof(*verify);
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, KEY, verify, &verify_size);
        nvs_close(h);
    }
    bool verified = err == ESP_OK && verify_size == sizeof(*verify) && memcmp(verify, c, sizeof(*c)) == 0;
    free(verify);
    if (!verified) return err == ESP_OK ? ESP_ERR_INVALID_CRC : err;
    set_active(c);
    return ESP_OK;
}

static void endpoint_to_json(cJSON *o, const modbus_endpoint_t *e)
{
    cJSON_AddStringToObject(o, "host", e->host);
    cJSON_AddNumberToObject(o, "port", e->port);
    cJSON_AddNumberToObject(o, "unit_id", e->unit_id);
    cJSON_AddNumberToObject(o, "timeout_ms", e->timeout_ms);
}

static void wifi_profile_to_json(cJSON *parent, const char *name, const app_wifi_sta_profile_t *p)
{
    cJSON *o = cJSON_AddObjectToObject(parent, name);
    cJSON_AddBoolToObject(o, "enabled", p->enabled);
    cJSON_AddStringToObject(o, "ssid", p->ssid);
    cJSON_AddStringToObject(o, "password", p->password[0] ? MASKED_PASSWORD : "");
    cJSON_AddNumberToObject(o, "ip_mode", p->ip_mode);
    cJSON_AddStringToObject(o, "static_ip", p->static_ip);
    cJSON_AddStringToObject(o, "gateway", p->gateway);
    cJSON_AddStringToObject(o, "netmask", p->netmask);
    cJSON_AddStringToObject(o, "dns1", p->dns1);
    cJSON_AddStringToObject(o, "dns2", p->dns2);
}

esp_err_t config_manager_export_json(char **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    app_config_t *c = malloc(sizeof(*c));
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(c);
    if (err != ESP_OK) { free(c); return err; }

    cJSON *r = cJSON_CreateObject();
    if (!r) { free(c); return ESP_ERR_NO_MEM; }
    cJSON_AddNumberToObject(r, "schema", c->version);
    cJSON_AddStringToObject(r, "device_name", c->device_name);
    cJSON *w = cJSON_AddObjectToObject(r, "wifi");
    wifi_profile_to_json(w, "primary", &c->wifi.primary);
    wifi_profile_to_json(w, "fallback", &c->wifi.fallback);
    cJSON_AddBoolToObject(w, "scan_before_connect", c->wifi.scan_before_connect);
    cJSON_AddBoolToObject(w, "fallback_ap_enabled", c->wifi.fallback_ap_enabled);
    cJSON_AddStringToObject(w, "fallback_ap_ssid", c->wifi.fallback_ap_ssid);
    cJSON_AddStringToObject(w, "fallback_ap_password", c->wifi.fallback_ap_password[0] ? MASKED_PASSWORD : "");
    cJSON_AddNumberToObject(w, "max_retries_per_profile", c->wifi.max_retries_per_profile);
    cJSON_AddNumberToObject(w, "reconnect_backoff_ms", c->wifi.reconnect_backoff_ms);

    cJSON *ma = cJSON_AddArrayToObject(r, "meters");
    for (uint8_t n = 0; n < c->meter_count; ++n) {
        meter_config_t *m = &c->meters[n];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", m->enabled);
        cJSON_AddStringToObject(o, "name", m->name);
        endpoint_to_json(o, &m->endpoint);
        cJSON_AddNumberToObject(o, "function", m->function_code);
        cJSON_AddNumberToObject(o, "active_power_address", m->active_power_address);
        cJSON_AddNumberToObject(o, "data_type", m->active_power_type);
        cJSON_AddNumberToObject(o, "word_order", m->active_power_order);
        cJSON_AddNumberToObject(o, "scale", m->active_power_scale);
        cJSON_AddNumberToObject(o, "poll_ms", m->poll_interval_ms);
        cJSON_AddNumberToObject(o, "role", m->role);
        cJSON_AddStringToObject(o, "role_name", meter_role_name(m->role));
        if (m->role == METER_ROLE_GENERATOR) {
            cJSON_AddNumberToObject(o, "generator_index", m->generator_index);
        } else {
            cJSON_AddNullToObject(o, "generator_index");
        }
        cJSON_AddItemToArray(ma, o);
    }

    cJSON *ia = cJSON_AddArrayToObject(r, "inverters");
    for (uint8_t n = 0; n < c->inverter_count; ++n) {
        inverter_config_t *i = &c->inverters[n];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", i->enabled);
        cJSON_AddStringToObject(o, "name", i->name);
        endpoint_to_json(o, &i->endpoint);
        cJSON_AddNumberToObject(o, "rated_kw", i->rated_power_kw);
        cJSON_AddNumberToObject(o, "limit_address", i->power_limit_address);
        cJSON_AddNumberToObject(o, "limit_function", i->power_limit_function);
        cJSON_AddNumberToObject(o, "raw_units_per_percent", i->raw_units_per_percent);
        cJSON_AddNumberToObject(o, "min_percent", i->minimum_percent);
        cJSON_AddNumberToObject(o, "max_percent", i->maximum_percent);
        cJSON_AddItemToArray(ia, o);
    }

    cJSON *cc = cJSON_AddObjectToObject(r, "control");
    cJSON_AddBoolToObject(cc, "enabled", c->control.enabled);
    cJSON_AddNumberToObject(cc, "grid_import_target_kw", c->control.grid_import_target_kw);
    cJSON_AddNumberToObject(cc, "deadband_kw", c->control.deadband_kw);
    cJSON_AddNumberToObject(cc, "kp", c->control.kp);
    cJSON_AddNumberToObject(cc, "ki", c->control.ki);
    cJSON_AddNumberToObject(cc, "ramp_up_pct_s", c->control.ramp_up_percent_per_second);
    cJSON_AddNumberToObject(cc, "ramp_down_pct_s", c->control.ramp_down_percent_per_second);
    for (int which = 0; which < 2; ++which) {
        const ramp_profile_t *ramp = which == 0 ? &c->control.grid_ramp
                                                : &c->control.generator_ramp;
        cJSON *ro = cJSON_AddObjectToObject(cc, which == 0 ? "grid_ramp" : "generator_ramp");
        cJSON_AddBoolToObject(ro, "enabled", ramp->enabled);
        cJSON_AddNumberToObject(ro, "up_pct_s", ramp->up_percent_per_second);
        cJSON_AddNumberToObject(ro, "down_pct_s", ramp->down_percent_per_second);
    }
    /*
     * THE MULTIPLIER THAT IS APPLIED TO A COMMISSIONED RAMP WITHOUT ASKING.
     *
     * The generator ramp-DOWN rate an engineer commissions is not the rate that
     * is always in force. While a generator carries the plant and its loading is
     * below GENERATOR_URGENT_LOADING_FRACTION of the online rating, the control
     * loop multiplies the down rate by GENERATOR_URGENT_RAMP_MULTIPLIER, because
     * an under-loaded engine needs PV pulled off it faster than normal.
     *
     * That is correct behaviour and it was invisible. An engineer set 5 %/s,
     * the plant sometimes ran at 10 %/s, and nothing on any screen said so --
     * which is the same class of defect as showing a commanded setpoint as if it
     * were a measurement: the number on the screen is not the number in force.
     *
     * Published READ-ONLY so the interface can state it. Deliberately taken from
     * the firmware's own constants rather than restated in JavaScript: a second
     * copy of 25% and 2x would drift from generator_fleet_limit.h the first time
     * either was tuned, and then the screen would confidently describe behaviour
     * the controller no longer has.
     */
    cJSON *urgent = cJSON_AddObjectToObject(cc, "generator_urgent_ramp");
    cJSON_AddNumberToObject(urgent, "below_loading_fraction",
                            GENERATOR_URGENT_LOADING_FRACTION);
    cJSON_AddNumberToObject(urgent, "down_rate_multiplier",
                            GENERATOR_URGENT_RAMP_MULTIPLIER);
    cJSON_AddBoolToObject(urgent, "applies_to_down_only", true);
    cJSON_AddBoolToObject(urgent, "configurable", false);

    cJSON_AddNumberToObject(cc, "interval_ms", c->control.interval_ms);
    cJSON_AddNumberToObject(cc, "meter_stale_timeout_ms", c->control.meter_stale_timeout_ms);

    *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    free(c);
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool json_depth_valid(const char *text)
{
    unsigned depth = 0;
    bool in_string = false;
    bool escape = false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (in_string) {
            if (escape) escape = false;
            else if (*p == '\\') escape = true;
            else if (*p == '"') in_string = false;
            continue;
        }
        if (*p == '"') in_string = true;
        else if (*p == '{' || *p == '[') {
            if (++depth > CONFIG_JSON_MAX_DEPTH) return false;
        } else if (*p == '}' || *p == ']') {
            if (depth == 0) return false;
            depth--;
        }
    }
    return !in_string && depth == 0;
}

static void read_string(cJSON *o, const char *key, char *dst, size_t size)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(x) && strlen(x->valuestring) < size) strlcpy(dst, x->valuestring, size);
}

static void read_password(cJSON *o, const char *key, char *dst, size_t size)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(x) && x->valuestring[0] && strcmp(x->valuestring, MASKED_PASSWORD) != 0 && strlen(x->valuestring) < size) {
        strlcpy(dst, x->valuestring, size);
    }
}

static bool read_float(cJSON *o, const char *key, float *value)
{
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!x) return true;
    if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble)) return false;
    *value = (float)x->valuedouble;
    return isfinite(*value);
}

static void parse_wifi_profile(cJSON *o, app_wifi_sta_profile_t *p)
{
    if (!cJSON_IsObject(o)) return;
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, "enabled");
    if (cJSON_IsBool(x)) p->enabled = cJSON_IsTrue(x);
    read_string(o, "ssid", p->ssid, sizeof(p->ssid));
    read_password(o, "password", p->password, sizeof(p->password));
    x = cJSON_GetObjectItemCaseSensitive(o, "ip_mode");
    if (cJSON_IsNumber(x)) p->ip_mode = (app_wifi_ip_mode_t)x->valueint;
    read_string(o, "static_ip", p->static_ip, sizeof(p->static_ip));
    read_string(o, "gateway", p->gateway, sizeof(p->gateway));
    read_string(o, "netmask", p->netmask, sizeof(p->netmask));
    read_string(o, "dns1", p->dns1, sizeof(p->dns1));
    read_string(o, "dns2", p->dns2, sizeof(p->dns2));
}

esp_err_t config_manager_import_json(const char *text)
{
    if (!text || !json_depth_valid(text)) return ESP_ERR_INVALID_ARG;
    cJSON *r = cJSON_Parse(text);
    if (!r || !cJSON_IsObject(r)) { cJSON_Delete(r); return ESP_ERR_INVALID_ARG; }

    app_config_t *c = malloc(sizeof(*c));
    if (!c) { cJSON_Delete(r); return ESP_ERR_NO_MEM; }
    esp_err_t err = config_manager_get_snapshot(c);
    if (err != ESP_OK) { free(c); cJSON_Delete(r); return err; }

    cJSON *w = cJSON_GetObjectItemCaseSensitive(r, "wifi");
    if (cJSON_IsObject(w)) {
        parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(w, "primary"), &c->wifi.primary);
        parse_wifi_profile(cJSON_GetObjectItemCaseSensitive(w, "fallback"), &c->wifi.fallback);
        cJSON *x = cJSON_GetObjectItemCaseSensitive(w, "scan_before_connect");
        if (cJSON_IsBool(x)) c->wifi.scan_before_connect = cJSON_IsTrue(x);
        /* fallback_ap_enabled is deliberately not importable. A configuration
         * file must not be able to switch off the controller's guaranteed way
         * in; read_password() likewise never clears the passphrase, only
         * replaces it. */
        read_string(w, "fallback_ap_ssid", c->wifi.fallback_ap_ssid, sizeof(c->wifi.fallback_ap_ssid));
        read_password(w, "fallback_ap_password", c->wifi.fallback_ap_password, sizeof(c->wifi.fallback_ap_password));
        c->wifi.fallback_ap_enabled = true;
        x = cJSON_GetObjectItemCaseSensitive(w, "max_retries_per_profile");
        if (cJSON_IsNumber(x)) c->wifi.max_retries_per_profile = (uint8_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(w, "reconnect_backoff_ms");
        if (cJSON_IsNumber(x)) c->wifi.reconnect_backoff_ms = (uint32_t)x->valuedouble;
    }

    cJSON *meters = cJSON_GetObjectItemCaseSensitive(r, "meters");
    if (cJSON_IsArray(meters) && cJSON_GetArraySize(meters) > 0) {
        cJSON *o = cJSON_GetArrayItem(meters, 0);
        meter_config_t *m = &c->meters[0];
        read_string(o, "host", m->endpoint.host, sizeof(m->endpoint.host));
        cJSON *x = cJSON_GetObjectItemCaseSensitive(o, "port");
        if (cJSON_IsNumber(x)) m->endpoint.port = (uint16_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(o, "unit_id");
        if (cJSON_IsNumber(x)) m->endpoint.unit_id = (uint8_t)x->valueint;
        x = cJSON_GetObjectItemCaseSensitive(o, "active_power_address");
        if (cJSON_IsNumber(x)) m->active_power_address = (uint16_t)x->valueint;
        if (!read_float(o, "scale", &m->active_power_scale)) err = ESP_ERR_INVALID_ARG;
    }

    cJSON *cc = cJSON_GetObjectItemCaseSensitive(r, "control");
    if (cJSON_IsObject(cc)) {
        /* Generic import may tune values but cannot arm automatic control. */
        c->control.enabled = false;
        if (!read_float(cc, "grid_import_target_kw", &c->control.grid_import_target_kw) ||
            !read_float(cc, "deadband_kw", &c->control.deadband_kw) ||
            !read_float(cc, "kp", &c->control.kp) ||
            !read_float(cc, "ki", &c->control.ki)) err = ESP_ERR_INVALID_ARG;
        cJSON *x = cJSON_GetObjectItemCaseSensitive(cc, "interval_ms");
        if (cJSON_IsNumber(x)) c->control.interval_ms = (uint32_t)x->valuedouble;

        /* Ramp behaviour is commissioning data: which sources are rate-limited,
         * and how fast in each direction. valid() rejects an enabled profile
         * with a zero rate, which would otherwise freeze the command. */
        for (int which = 0; which < 2; ++which) {
            cJSON *ro = cJSON_GetObjectItemCaseSensitive(cc, which == 0 ? "grid_ramp"
                                                                        : "generator_ramp");
            if (!cJSON_IsObject(ro)) continue;
            ramp_profile_t *ramp = which == 0 ? &c->control.grid_ramp
                                              : &c->control.generator_ramp;
            cJSON *e = cJSON_GetObjectItemCaseSensitive(ro, "enabled");
            if (cJSON_IsBool(e)) ramp->enabled = cJSON_IsTrue(e);
            if (!read_float(ro, "up_pct_s", &ramp->up_percent_per_second) ||
                !read_float(ro, "down_pct_s", &ramp->down_percent_per_second)) {
                err = ESP_ERR_INVALID_ARG;
            }
        }
    }

    cJSON_Delete(r);
    if (err == ESP_OK && !valid(c)) err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) err = config_manager_save(c);
    free(c);
    return err;
}

esp_err_t config_manager_restore_defaults(void)
{
    app_config_t *c = malloc(sizeof(*c));
    if (!c) return ESP_ERR_NO_MEM;
    defaults(c);
    /* Restoring defaults must still leave a reachable, secured recovery AP. */
    (void)ensure_recovery_ap_secret(c);
    esp_err_t err = config_manager_save(c);
    free(c);
    return err;
}
