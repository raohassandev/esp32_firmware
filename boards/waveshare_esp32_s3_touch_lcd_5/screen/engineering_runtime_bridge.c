#include "engineering_runtime_bridge.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "auth_hmac_compat.h"
#include "engineering_sections.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"
#include "nvs.h"

#define AUTH_NAMESPACE "eng_auth"
#define AUTH_RECORD_KEY "credential"
#define AUTH_RECORD_MAGIC 0x41555448u
#define AUTH_RECORD_VERSION 1u
#define AUTH_SALT_BYTES 16u
#define AUTH_HASH_BYTES 32u
#define AUTH_MIN_PASSWORD_LENGTH 10u
#define AUTH_MAX_PASSWORD_LENGTH 64u
#define AUTH_SESSION_TIMEOUT_MS (30ULL * 60ULL * 1000ULL)
#define AUTH_MAX_FAILURES 5u
#define AUTH_LOCKOUT_MS 30000ULL

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t iterations;
    uint8_t salt[AUTH_SALT_BYTES];
    uint8_t hash[AUTH_HASH_BYTES];
} local_auth_record_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_authenticated;
static uint64_t s_expires_ms;
static uint8_t s_failed_attempts;
static uint64_t s_lockout_until_ms;
static char s_message[160] = "Engineering session: locked";

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static bool record_valid(const local_auth_record_t *record)
{
    return record && record->magic == AUTH_RECORD_MAGIC &&
           record->version == AUTH_RECORD_VERSION &&
           record->iterations >= 1000U && record->iterations <= 1000000U;
}

static esp_err_t load_record(local_auth_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    memset(record, 0, sizeof(*record));
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(*record);
    err = nvs_get_blob(handle, AUTH_RECORD_KEY, record, &size);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    return size == sizeof(*record) && record_valid(record) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    if (!left || !right) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0U;
}

static int hmac_sha256(const uint8_t *key, size_t key_length,
                       const uint8_t *data, size_t data_length,
                       uint8_t output[AUTH_HASH_BYTES])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return -1;
    return mbedtls_md_hmac(info, key, key_length, data, data_length, output);
}

static bool derive_password_hash(const char *password,
                                 const uint8_t salt[AUTH_SALT_BYTES],
                                 uint32_t iterations,
                                 uint8_t output[AUTH_HASH_BYTES])
{
    if (!password || !salt || !output || iterations < 1000U) return false;
    const size_t password_length = strlen(password);
    if (password_length < AUTH_MIN_PASSWORD_LENGTH ||
        password_length > AUTH_MAX_PASSWORD_LENGTH) return false;

    uint8_t initial[AUTH_SALT_BYTES + 4U];
    memcpy(initial, salt, AUTH_SALT_BYTES);
    initial[AUTH_SALT_BYTES] = 0U;
    initial[AUTH_SALT_BYTES + 1U] = 0U;
    initial[AUTH_SALT_BYTES + 2U] = 0U;
    initial[AUTH_SALT_BYTES + 3U] = 1U;

    uint8_t u[AUTH_HASH_BYTES];
    uint8_t accumulator[AUTH_HASH_BYTES];
    if (hmac_sha256((const uint8_t *)password, password_length,
                    initial, sizeof(initial), u) != 0) {
        memset(initial, 0, sizeof(initial));
        return false;
    }
    memcpy(accumulator, u, sizeof(accumulator));

    for (uint32_t iteration = 1U; iteration < iterations; ++iteration) {
        uint8_t next[AUTH_HASH_BYTES];
        if (hmac_sha256((const uint8_t *)password, password_length,
                        u, sizeof(u), next) != 0) {
            memset(initial, 0, sizeof(initial));
            memset(u, 0, sizeof(u));
            memset(accumulator, 0, sizeof(accumulator));
            return false;
        }
        memcpy(u, next, sizeof(u));
        for (size_t i = 0U; i < sizeof(accumulator); ++i) accumulator[i] ^= u[i];
        memset(next, 0, sizeof(next));
    }

    memcpy(output, accumulator, AUTH_HASH_BYTES);
    memset(initial, 0, sizeof(initial));
    memset(u, 0, sizeof(u));
    memset(accumulator, 0, sizeof(accumulator));
    return true;
}

static bool verify_password(const char *password)
{
    local_auth_record_t record;
    if (load_record(&record) != ESP_OK) return false;
    uint8_t candidate[AUTH_HASH_BYTES];
    const bool derived = derive_password_hash(password, record.salt,
                                              record.iterations, candidate);
    const bool matches = derived &&
                         constant_time_equal(candidate, record.hash, sizeof(candidate));
    memset(candidate, 0, sizeof(candidate));
    memset(&record, 0, sizeof(record));
    return matches;
}

bool engineering_runtime_bridge_password_configured(void)
{
    local_auth_record_t record;
    const bool configured = load_record(&record) == ESP_OK;
    memset(&record, 0, sizeof(record));
    return configured;
}

static void expire_if_needed_locked(uint64_t current)
{
    if (s_authenticated && current >= s_expires_ms) {
        s_authenticated = false;
        s_expires_ms = 0U;
        snprintf(s_message, sizeof(s_message), "Engineering session expired");
    }
    if (s_lockout_until_ms != 0U && current >= s_lockout_until_ms) {
        s_lockout_until_ms = 0U;
        s_failed_attempts = 0U;
    }
}

bool engineering_runtime_bridge_is_authorized(void)
{
    const uint64_t current = now_ms();
    bool authorized = false;
    portENTER_CRITICAL(&s_lock);
    expire_if_needed_locked(current);
    authorized = s_authenticated && current < s_expires_ms;
    portEXIT_CRITICAL(&s_lock);
    return authorized && engineering_runtime_bridge_password_configured();
}

void engineering_runtime_bridge_submit_password(const char *password, void *user)
{
    (void)user;
    const uint64_t current = now_ms();

    portENTER_CRITICAL(&s_lock);
    expire_if_needed_locked(current);
    const bool locked = current < s_lockout_until_ms;
    portEXIT_CRITICAL(&s_lock);
    if (locked) return;

    if (!engineering_runtime_bridge_password_configured()) {
        portENTER_CRITICAL(&s_lock);
        s_authenticated = false;
        s_expires_ms = 0U;
        snprintf(s_message, sizeof(s_message),
                 "Permanent Engineering password is not configured. Use the serial setup code in the web Engineering page first.");
        portEXIT_CRITICAL(&s_lock);
        return;
    }

    const bool valid = verify_password(password ? password : "");
    portENTER_CRITICAL(&s_lock);
    if (valid) {
        s_authenticated = true;
        s_expires_ms = current + AUTH_SESSION_TIMEOUT_MS;
        s_failed_attempts = 0U;
        s_lockout_until_ms = 0U;
        snprintf(s_message, sizeof(s_message), "Engineering session authenticated");
    } else {
        s_authenticated = false;
        s_expires_ms = 0U;
        if (++s_failed_attempts >= AUTH_MAX_FAILURES) {
            s_failed_attempts = 0U;
            s_lockout_until_ms = current + AUTH_LOCKOUT_MS;
            snprintf(s_message, sizeof(s_message),
                     "Engineering login locked for 30 seconds");
        } else {
            snprintf(s_message, sizeof(s_message), "Invalid Engineering password");
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

void engineering_runtime_bridge_logout(void *user)
{
    (void)user;
    portENTER_CRITICAL(&s_lock);
    s_authenticated = false;
    s_expires_ms = 0U;
    snprintf(s_message, sizeof(s_message), "Engineering session closed");
    portEXIT_CRITICAL(&s_lock);
    engineering_sections_hide();
}

void engineering_runtime_bridge_refresh(pvdg_ui_engineering_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));

    const bool configured = engineering_runtime_bridge_password_configured();
    const uint64_t current = now_ms();
    portENTER_CRITICAL(&s_lock);
    expire_if_needed_locked(current);
    state->authenticated = configured && s_authenticated && current < s_expires_ms;
    state->setup_required = !configured;
    state->locked_out = current < s_lockout_until_ms;
    state->lockout_remaining_seconds = state->locked_out
        ? (uint32_t)((s_lockout_until_ms - current + 999ULL) / 1000ULL) : 0U;
    state->session_remaining_seconds = state->authenticated
        ? (uint32_t)((s_expires_ms - current + 999ULL) / 1000ULL) : 0U;
    state->session_timeout_minutes = 30U;
    state->write_contract_verified = state->authenticated && !state->setup_required;
    state->password_change_recommended = false;
    state->security_state = state->setup_required ? "Password setup required" :
                            state->authenticated ? "Authenticated" : "Protected";
    state->message = s_message;
    portEXIT_CRITICAL(&s_lock);
}

/* Optional runtime-UI host hooks. */
void pvdg_ui_engineering_host_refresh(pvdg_ui_engineering_state_t *state)
{
    engineering_runtime_bridge_refresh(state);
}

void pvdg_ui_engineering_host_submit_password(const char *password, void *user)
{
    engineering_runtime_bridge_submit_password(password, user);
}

void pvdg_ui_engineering_host_logout(void *user)
{
    engineering_runtime_bridge_logout(user);
}

void pvdg_ui_engineering_host_open_section(pvdg_ui_engineering_section_t section,
                                           void *user)
{
    (void)user;
    if (section != PVDG_UI_ENGINEERING_DIAGNOSTICS &&
        !engineering_runtime_bridge_is_authorized()) {
        return;
    }
    engineering_sections_open(section);
}
