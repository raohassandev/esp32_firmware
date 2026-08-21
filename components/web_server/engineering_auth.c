#include "engineering_auth.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audit_log.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"
#include "nvs.h"

#define AUTH_NAMESPACE "eng_auth"
#define AUTH_RECORD_KEY "credential"
/* Records the highest applied credential-reset generation, so a recovery build
 * that stays installed resets the password once and not on every boot. */
#define AUTH_RESET_GENERATION_KEY "reset_gen"
#define AUTH_RECORD_MAGIC 0x41555448u
#define AUTH_RECORD_VERSION 1u
#define AUTH_SALT_BYTES 16u
#define AUTH_HASH_BYTES 32u
#define AUTH_SESSION_BYTES 32u
#define AUTH_SESSION_HEX_BYTES (AUTH_SESSION_BYTES * 2u)
/* Session cookie header buffer, owned by the request handler so that it stays
 * valid until httpd has sent the response. */
#define AUTH_COOKIE_HEADER_BYTES 160u
#define AUTH_PBKDF2_ITERATIONS 20000u
#define AUTH_SESSION_TIMEOUT_MS (30ULL * 60ULL * 1000ULL)
#define AUTH_BODY_LIMIT 1024u
#define AUTH_BODY_DEADLINE_MS 3000ULL
#define AUTH_JSON_MAX_DEPTH 6u
#define AUTH_MAX_FAILURES 5u
#define AUTH_LOCKOUT_MS 30000ULL
#define AUTH_MIN_PASSWORD_LENGTH 10u
#define AUTH_MAX_PASSWORD_LENGTH 64u
#define AUTH_COOKIE_NAME "eng_session"

static const char *TAG = "engineering_auth";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t iterations;
    uint8_t salt[AUTH_SALT_BYTES];
    uint8_t hash[AUTH_HASH_BYTES];
} auth_record_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static auth_record_t s_record;
static bool s_password_configured;
static char s_setup_code[16];
static uint8_t s_session_token[AUTH_SESSION_BYTES];
static bool s_session_active;
static uint64_t s_session_expires_ms;
static uint8_t s_failed_attempts;
static uint64_t s_lockout_until_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t index = 0; index < length; ++index) difference |= left[index] ^ right[index];
    return difference == 0;
}

static bool constant_time_string_equal(const char *left, const char *right)
{
    if (!left || !right) return false;
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    size_t length = left_length > right_length ? left_length : right_length;
    uint8_t difference = (uint8_t)(left_length ^ right_length);
    for (size_t index = 0; index < length; ++index) {
        uint8_t a = index < left_length ? (uint8_t)left[index] : 0;
        uint8_t b = index < right_length ? (uint8_t)right[index] : 0;
        difference |= a ^ b;
    }
    return difference == 0;
}

static int hmac_sha256(const uint8_t *key, size_t key_length,
                       const uint8_t *data, size_t data_length,
                       uint8_t output[AUTH_HASH_BYTES])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return -1;
    return mbedtls_md_hmac(info, key, key_length, data, data_length, output);
}

static bool derive_password_hash(const char *password, const uint8_t salt[AUTH_SALT_BYTES],
                                 uint32_t iterations, uint8_t output[AUTH_HASH_BYTES])
{
    if (!password || !salt || !output || iterations < 1000u) return false;
    size_t password_length = strlen(password);
    if (password_length < AUTH_MIN_PASSWORD_LENGTH || password_length > AUTH_MAX_PASSWORD_LENGTH) return false;

    uint8_t initial[AUTH_SALT_BYTES + 4u];
    memcpy(initial, salt, AUTH_SALT_BYTES);
    initial[AUTH_SALT_BYTES] = 0;
    initial[AUTH_SALT_BYTES + 1u] = 0;
    initial[AUTH_SALT_BYTES + 2u] = 0;
    initial[AUTH_SALT_BYTES + 3u] = 1;

    uint8_t u[AUTH_HASH_BYTES];
    uint8_t accumulator[AUTH_HASH_BYTES];
    if (hmac_sha256((const uint8_t *)password, password_length,
                    initial, sizeof(initial), u) != 0) return false;
    memcpy(accumulator, u, sizeof(accumulator));

    for (uint32_t iteration = 1; iteration < iterations; ++iteration) {
        uint8_t next[AUTH_HASH_BYTES];
        if (hmac_sha256((const uint8_t *)password, password_length,
                        u, sizeof(u), next) != 0) {
            memset(u, 0, sizeof(u));
            memset(accumulator, 0, sizeof(accumulator));
            return false;
        }
        memcpy(u, next, sizeof(u));
        for (size_t index = 0; index < sizeof(accumulator); ++index) accumulator[index] ^= u[index];
        memset(next, 0, sizeof(next));
    }

    memcpy(output, accumulator, AUTH_HASH_BYTES);
    memset(initial, 0, sizeof(initial));
    memset(u, 0, sizeof(u));
    memset(accumulator, 0, sizeof(accumulator));
    return true;
}

static bool record_valid(const auth_record_t *record)
{
    return record && record->magic == AUTH_RECORD_MAGIC &&
           record->version == AUTH_RECORD_VERSION &&
           record->iterations >= 1000u && record->iterations <= 1000000u;
}

static esp_err_t load_record(auth_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(*record);
    err = nvs_get_blob(handle, AUTH_RECORD_KEY, record, &size);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    return size == sizeof(*record) && record_valid(record) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t store_password(const char *password, auth_record_t *out_record)
{
    if (!password || !out_record) return ESP_ERR_INVALID_ARG;
    auth_record_t record = {
        .magic = AUTH_RECORD_MAGIC,
        .version = AUTH_RECORD_VERSION,
        .iterations = AUTH_PBKDF2_ITERATIONS,
    };
    esp_fill_random(record.salt, sizeof(record.salt));
    if (!derive_password_hash(password, record.salt, record.iterations, record.hash)) {
        memset(&record, 0, sizeof(record));
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, AUTH_RECORD_KEY, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (err != ESP_OK) {
        memset(&record, 0, sizeof(record));
        return err;
    }
    *out_record = record;
    return ESP_OK;
}

static bool verify_password(const char *password)
{
    auth_record_t record;
    portENTER_CRITICAL(&s_lock);
    record = s_record;
    bool configured = s_password_configured;
    portEXIT_CRITICAL(&s_lock);
    if (!configured || !record_valid(&record)) return false;

    uint8_t candidate[AUTH_HASH_BYTES];
    bool derived = derive_password_hash(password, record.salt, record.iterations, candidate);
    bool matches = derived && constant_time_equal(candidate, record.hash, sizeof(candidate));
    memset(candidate, 0, sizeof(candidate));
    memset(&record, 0, sizeof(record));
    return matches;
}

static void bytes_to_hex(const uint8_t *bytes, size_t length, char *hex)
{
    static const char alphabet[] = "0123456789abcdef";
    for (size_t index = 0; index < length; ++index) {
        hex[index * 2u] = alphabet[bytes[index] >> 4u];
        hex[index * 2u + 1u] = alphabet[bytes[index] & 0x0fu];
    }
    hex[length * 2u] = '\0';
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool hex_to_bytes(const char *hex, uint8_t *bytes, size_t length)
{
    if (!hex || !bytes) return false;
    for (size_t index = 0; index < length; ++index) {
        int high = hex_value(hex[index * 2u]);
        int low = hex_value(hex[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        bytes[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool extract_session_cookie(httpd_req_t *request, uint8_t token[AUTH_SESSION_BYTES])
{
    if (!request || !token) return false;
    size_t length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (length == 0 || length > 512u) return false;
    char *cookie = malloc(length + 1u);
    if (!cookie) return false;
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, length + 1u) != ESP_OK) {
        free(cookie);
        return false;
    }

    const char *cursor = cookie;
    const char *value = NULL;
    const size_t name_length = strlen(AUTH_COOKIE_NAME);
    while ((cursor = strstr(cursor, AUTH_COOKIE_NAME "=")) != NULL) {
        if (cursor == cookie || cursor[-1] == ';' || cursor[-1] == ' ') {
            value = cursor + name_length + 1u;
            break;
        }
        cursor += name_length;
    }
    bool valid = value && strlen(value) >= AUTH_SESSION_HEX_BYTES &&
                 (value[AUTH_SESSION_HEX_BYTES] == '\0' || value[AUTH_SESSION_HEX_BYTES] == ';' ||
                  value[AUTH_SESSION_HEX_BYTES] == ' ') &&
                 hex_to_bytes(value, token, AUTH_SESSION_BYTES);
    free(cookie);
    return valid;
}

static bool session_cookie_valid(httpd_req_t *request, bool extend)
{
    uint8_t supplied[AUTH_SESSION_BYTES];
    if (!extract_session_cookie(request, supplied)) return false;
    uint64_t current = now_ms();

    portENTER_CRITICAL(&s_lock);
    bool valid = s_session_active && current < s_session_expires_ms &&
                 constant_time_equal(supplied, s_session_token, sizeof(supplied));
    if (valid && extend) s_session_expires_ms = current + AUTH_SESSION_TIMEOUT_MS;
    if (s_session_active && current >= s_session_expires_ms) {
        memset(s_session_token, 0, sizeof(s_session_token));
        s_session_active = false;
        s_session_expires_ms = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    memset(supplied, 0, sizeof(supplied));
    return valid;
}

static void create_session(char cookie_value[AUTH_SESSION_HEX_BYTES + 1u])
{
    uint8_t token[AUTH_SESSION_BYTES];
    uint64_t expiry = now_ms() + AUTH_SESSION_TIMEOUT_MS;
    esp_fill_random(token, sizeof(token));
    bytes_to_hex(token, sizeof(token), cookie_value);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_session_token, token, sizeof(token));
    s_session_active = true;
    s_session_expires_ms = expiry;
    portEXIT_CRITICAL(&s_lock);
    memset(token, 0, sizeof(token));
}

static void invalidate_session(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(s_session_token, 0, sizeof(s_session_token));
    s_session_active = false;
    s_session_expires_ms = 0;
    portEXIT_CRITICAL(&s_lock);
}

/* httpd_resp_set_hdr() stores the pointer it is given and does not copy the
 * value, so the buffer must stay alive until the response has been sent. The
 * caller therefore owns it; a local here would be dangling by send time and the
 * client would receive an empty Set-Cookie. */
static void set_session_cookie(httpd_req_t *request, char *header, size_t header_size, const char *token_hex)
{
    snprintf(header, header_size, AUTH_COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800",
             token_hex ? token_hex : "");
    httpd_resp_set_hdr(request, "Set-Cookie", header);
}

static void clear_session_cookie(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Set-Cookie",
                       AUTH_COOKIE_NAME "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

static esp_err_t send_json(httpd_req_t *request, const char *status, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static bool json_depth_valid(const char *text)
{
    if (!text) return false;
    unsigned depth = 0;
    bool in_string = false;
    bool escape = false;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (in_string) {
            if (escape) escape = false;
            else if (*cursor == '\\') escape = true;
            else if (*cursor == '"') in_string = false;
            continue;
        }
        if (*cursor == '"') in_string = true;
        else if (*cursor == '{' || *cursor == '[') {
            if (++depth > AUTH_JSON_MAX_DEPTH) return false;
        } else if (*cursor == '}' || *cursor == ']') {
            if (depth == 0) return false;
            depth--;
        }
    }
    return !in_string && depth == 0;
}

static esp_err_t read_json_body(httpd_req_t *request, char **out_body)
{
    if (!request || !out_body || request->content_len <= 0 || request->content_len > AUTH_BODY_LIMIT) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *body = malloc((size_t)request->content_len + 1u);
    if (!body) return ESP_ERR_NO_MEM;
    size_t offset = 0;
    uint64_t deadline = now_ms() + AUTH_BODY_DEADLINE_MS;
    while (offset < (size_t)request->content_len) {
        if (now_ms() >= deadline) {
            free(body);
            return ESP_ERR_TIMEOUT;
        }
        int received = httpd_req_recv(request, body + offset,
                                      (size_t)request->content_len - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            free(body);
            return ESP_FAIL;
        }
        offset += (size_t)received;
    }
    body[offset] = '\0';
    if (!json_depth_valid(body)) {
        free(body);
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = body;
    return ESP_OK;
}

static cJSON *parse_body(httpd_req_t *request)
{
    char *body = NULL;
    if (read_json_body(request, &body) != ESP_OK) return NULL;
    cJSON *root = cJSON_Parse(body);
    memset(body, 0, strlen(body));
    free(body);
    return root;
}

static const char *required_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool login_locked(uint64_t *remaining_ms)
{
    uint64_t current = now_ms();
    portENTER_CRITICAL(&s_lock);
    bool locked = current < s_lockout_until_ms;
    uint64_t remaining = locked ? s_lockout_until_ms - current : 0;
    if (!locked && s_lockout_until_ms != 0) {
        s_lockout_until_ms = 0;
        s_failed_attempts = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (remaining_ms) *remaining_ms = remaining;
    return locked;
}

/* A failed sign-in is recorded unconditionally, and the record carries nothing
 * but the fact of the failure: no password, no fragment of one, no length, no
 * token, no client-supplied text of any kind. audit_entry_t has no character
 * field, so there is no place for such a value to be added later. This
 * repository is public. */
static void record_login_failure(void)
{
    uint64_t lockout_until = now_ms() + AUTH_LOCKOUT_MS;
    portENTER_CRITICAL(&s_lock);
    bool lockout_engaged = false;
    if (++s_failed_attempts >= AUTH_MAX_FAILURES) {
        s_lockout_until_ms = lockout_until;
        s_failed_attempts = 0;
        lockout_engaged = true;
    }
    portEXIT_CRITICAL(&s_lock);

    /* Deliberately outside the critical section above: audit recording takes
     * its own spinlock, and nesting them would extend the interrupt-disabled
     * window on the authentication path for no benefit. */
    audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_FAILURE);
    if (lockout_engaged) {
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN,
                         AUDIT_OUTCOME_LOCKED_OUT);
    }
}

static void clear_login_failures(void)
{
    portENTER_CRITICAL(&s_lock);
    s_failed_attempts = 0;
    s_lockout_until_ms = 0;
    portEXIT_CRITICAL(&s_lock);
}

engineering_local_auth_result_t engineering_auth_verify_local_credential(
    const char *credential,
    uint32_t *retry_after_ms,
    bool *setup_required)
{
    if (retry_after_ms) *retry_after_ms = 0U;

    portENTER_CRITICAL(&s_lock);
    const bool setup = !s_password_configured;
    char setup_code[sizeof(s_setup_code)];
    strlcpy(setup_code, s_setup_code, sizeof(setup_code));
    portEXIT_CRITICAL(&s_lock);
    if (setup_required) *setup_required = setup;

    uint64_t remaining = 0U;
    if (login_locked(&remaining)) {
        memset(setup_code, 0, sizeof(setup_code));
        if (retry_after_ms) *retry_after_ms = (uint32_t)remaining;
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN,
                         AUDIT_OUTCOME_LOCKED_OUT);
        return ENGINEERING_LOCAL_AUTH_LOCKED;
    }

    if (!credential || strlen(credential) > AUTH_MAX_PASSWORD_LENGTH) {
        memset(setup_code, 0, sizeof(setup_code));
        record_login_failure();
        return ENGINEERING_LOCAL_AUTH_DENIED;
    }

    const bool valid = setup
        ? constant_time_string_equal(credential, setup_code)
        : verify_password(credential);
    memset(setup_code, 0, sizeof(setup_code));
    if (!valid) {
        record_login_failure();
        return ENGINEERING_LOCAL_AUTH_DENIED;
    }

    clear_login_failures();
    audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN,
                     AUDIT_OUTCOME_SUCCESS);
    return ENGINEERING_LOCAL_AUTH_OK;
}

bool engineering_auth_is_authorized(httpd_req_t *request)
{
    portENTER_CRITICAL(&s_lock);
    bool configured = s_password_configured;
    portEXIT_CRITICAL(&s_lock);
    return configured && session_cookie_valid(request, true);
}

esp_err_t engineering_auth_require(httpd_req_t *request)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    bool setup_required = !s_password_configured;
    portEXIT_CRITICAL(&s_lock);
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", setup_required ? "engineering_password_setup_required" : "engineering_authentication_required");
    cJSON_AddStringToObject(root, "message", setup_required
        ? "Use the one-time setup code printed on the controller serial console, then set a permanent Engineering password"
        : "Engineering authentication is required for this operation");
    cJSON_AddBoolToObject(root, "authenticated", false);
    cJSON_AddBoolToObject(root, "setup_required", setup_required);
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Session");
    return send_json(request, "401 Unauthorized", root);
}

static esp_err_t session_get(httpd_req_t *request)
{
    bool session_valid = session_cookie_valid(request, true);
    portENTER_CRITICAL(&s_lock);
    bool setup_required = !s_password_configured;
    portEXIT_CRITICAL(&s_lock);
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", session_valid);
    cJSON_AddBoolToObject(root, "setup_required", setup_required);
    cJSON_AddBoolToObject(root, "cookie_session", true);
    cJSON_AddBoolToObject(root, "development_auto_unlock", false);
    cJSON_AddBoolToObject(root, "temporary_field_bypass", false);
    cJSON_AddBoolToObject(root, "password_change_recommended", setup_required && session_valid);
    cJSON_AddNumberToObject(root, "session_timeout_minutes", 30);
    cJSON_AddStringToObject(root, "security_state", setup_required
        ? "Permanent Engineering password setup required"
        : session_valid ? "Engineering session authenticated" : "Engineering locked");
    return send_json(request, NULL, root);
}

static esp_err_t login_post(httpd_req_t *request)
{
    uint64_t remaining = 0;
    if (login_locked(&remaining)) {
        /* An attempt refused by the lockout is evidence of an ongoing attack
         * and must appear in the trail as its own event, not be swallowed. */
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN,
                         AUDIT_OUTCOME_LOCKED_OUT);
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "engineering_login_temporarily_locked");
        cJSON_AddNumberToObject(root, "retry_after_ms", (double)remaining);
        return send_json(request, "429 Too Many Requests", root);
    }

    cJSON *body = parse_body(request);
    const char *password = body ? required_string(body, "password") : NULL;
    if (!password || strlen(password) > AUTH_MAX_PASSWORD_LENGTH) {
        cJSON_Delete(body);
        record_login_failure();
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "invalid_engineering_credentials");
        return send_json(request, "401 Unauthorized", root);
    }

    portENTER_CRITICAL(&s_lock);
    bool setup_required = !s_password_configured;
    char setup_code[sizeof(s_setup_code)];
    strlcpy(setup_code, s_setup_code, sizeof(setup_code));
    portEXIT_CRITICAL(&s_lock);

    bool valid = setup_required ? constant_time_string_equal(password, setup_code) : verify_password(password);
    cJSON_Delete(body);
    memset(setup_code, 0, sizeof(setup_code));
    if (!valid) {
        record_login_failure();
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "invalid_engineering_credentials");
        return send_json(request, "401 Unauthorized", root);
    }

    clear_login_failures();
    audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGIN, AUDIT_OUTCOME_SUCCESS);
    /* cookie_header is a handler-owned buffer on purpose: httpd_resp_set_hdr()
     * stores the pointer without copying, and a buffer that dies before the
     * response is sent makes login silently impossible. Do not narrow it. */
    char cookie[AUTH_SESSION_HEX_BYTES + 1u];
    char cookie_header[AUTH_COOKIE_HEADER_BYTES];
    create_session(cookie);
    set_session_cookie(request, cookie_header, sizeof(cookie_header), cookie);
    memset(cookie, 0, sizeof(cookie));
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "setup_required", setup_required);
    cJSON_AddBoolToObject(root, "password_change_recommended", setup_required);
    cJSON_AddBoolToObject(root, "temporary_field_bypass", false);
    return send_json(request, NULL, root);
}

static esp_err_t logout_post(httpd_req_t *request)
{
    invalidate_session();
    audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_LOGOUT, AUDIT_OUTCOME_SUCCESS);
    clear_session_cookie(request);
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", false);
    cJSON_AddStringToObject(root, "message", "Engineering session closed");
    return send_json(request, NULL, root);
}

static esp_err_t password_post(httpd_req_t *request)
{
    if (!session_cookie_valid(request, true)) {
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_PASSWORD_CHANGE,
                         AUDIT_OUTCOME_DENIED);
        return engineering_auth_require(request);
    }
    cJSON *body = parse_body(request);
    const char *current_password = body ? required_string(body, "current_password") : NULL;
    const char *new_password = body ? required_string(body, "new_password") : NULL;
    if (!current_password || !new_password || strlen(new_password) < AUTH_MIN_PASSWORD_LENGTH ||
        strlen(new_password) > AUTH_MAX_PASSWORD_LENGTH) {
        cJSON_Delete(body);
        /* The rejection reason is a policy message to the caller only. The audit
         * entry records that a password change failed and nothing about the
         * candidate password - in particular, not whether it was too short. */
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_PASSWORD_CHANGE,
                         AUDIT_OUTCOME_FAILURE);
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "New password must contain 10-64 characters");
        return send_json(request, "400 Bad Request", root);
    }

    portENTER_CRITICAL(&s_lock);
    bool setup_required = !s_password_configured;
    char setup_code[sizeof(s_setup_code)];
    strlcpy(setup_code, s_setup_code, sizeof(setup_code));
    portEXIT_CRITICAL(&s_lock);
    bool current_valid = setup_required
        ? constant_time_string_equal(current_password, setup_code)
        : verify_password(current_password);
    memset(setup_code, 0, sizeof(setup_code));
    if (!current_valid) {
        cJSON_Delete(body);
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_PASSWORD_CHANGE,
                         AUDIT_OUTCOME_FAILURE);
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "Current Engineering password is incorrect");
        return send_json(request, "403 Forbidden", root);
    }

    auth_record_t record;
    esp_err_t err = store_password(new_password, &record);
    cJSON_Delete(body);
    if (err != ESP_OK) {
        audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_PASSWORD_CHANGE,
                         AUDIT_OUTCOME_FAILURE);
        cJSON *root = cJSON_CreateObject();
        if (!root) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(root, "error", "Engineering password could not be persisted");
        return send_json(request, "500 Internal Server Error", root);
    }

    portENTER_CRITICAL(&s_lock);
    s_record = record;
    s_password_configured = true;
    memset(s_setup_code, 0, sizeof(s_setup_code));
    portEXIT_CRITICAL(&s_lock);
    memset(&record, 0, sizeof(record));
    audit_log_record(AUDIT_CATEGORY_AUTHENTICATION, AUDIT_ACTION_PASSWORD_CHANGE,
                     AUDIT_OUTCOME_SUCCESS);

    char cookie[AUTH_SESSION_HEX_BYTES + 1u];
    char cookie_header[AUTH_COOKIE_HEADER_BYTES];
    create_session(cookie);
    set_session_cookie(request, cookie_header, sizeof(cookie_header), cookie);
    memset(cookie, 0, sizeof(cookie));
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "setup_required", false);
    cJSON_AddStringToObject(root, "message", "Engineering password updated");
    return send_json(request, NULL, root);
}

/* One-shot physical recovery for a lost Engineering password.
 *
 * Deletes the stored credential when the build carries a reset generation
 * strictly greater than the one already recorded on the device, then records the
 * new generation so a recovery build left installed cannot wipe the replacement
 * password on every reboot. Start-up then takes the ordinary
 * "no password configured" path: setup is required and a one-time setup code is
 * printed to the serial console.
 *
 * This installs nothing. It removes a credential, so no secret exists in the
 * firmware image, and it is unreachable over the network -- using it costs
 * physical access to reflash the board plus physical access to read its console.
 * That is the same trust boundary the existing one-time setup code already uses.
 *
 * Compiled out entirely at the default of 0, so a normal build does not merely
 * decline to reset a credential -- it contains no code that can. */
#if CONFIG_PVDG_ENGINEERING_CREDENTIAL_RESET_ID > 0
static void apply_credential_reset(void)
{
    nvs_handle_t handle;
    if (nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

    uint32_t applied = 0;
    (void)nvs_get_u32(handle, AUTH_RESET_GENERATION_KEY, &applied);
    if ((uint32_t)CONFIG_PVDG_ENGINEERING_CREDENTIAL_RESET_ID <= applied) {
        nvs_close(handle);
        return;
    }

    /* Erase only the credential key inside this namespace. Wi-Fi credentials,
     * the commissioned configuration and the inverter profile assignments live
     * elsewhere and are not touched. */
    esp_err_t erased = nvs_erase_key(handle, AUTH_RECORD_KEY);
    if (erased == ESP_ERR_NVS_NOT_FOUND) erased = ESP_OK; /* already absent */
    if (erased == ESP_OK) {
        erased = nvs_set_u32(handle, AUTH_RESET_GENERATION_KEY,
                             (uint32_t)CONFIG_PVDG_ENGINEERING_CREDENTIAL_RESET_ID);
    }
    if (erased == ESP_OK) erased = nvs_commit(handle);
    nvs_close(handle);

    if (erased == ESP_OK) {
        ESP_LOGW(TAG, "Engineering credential reset generation %d applied: the stored password "
                      "was deleted. A new password must be set before any engineering endpoint "
                      "can be used. Configuration and Wi-Fi credentials were not touched.",
                 CONFIG_PVDG_ENGINEERING_CREDENTIAL_RESET_ID);
    } else {
        ESP_LOGE(TAG, "Engineering credential reset generation %d failed: %s",
                 CONFIG_PVDG_ENGINEERING_CREDENTIAL_RESET_ID, esp_err_to_name(erased));
    }
}
#else
/* Recovery is not compiled into this build. */
static inline void apply_credential_reset(void) {}
#endif

esp_err_t engineering_auth_init(void)
{
    apply_credential_reset();

    auth_record_t record = {0};
    esp_err_t err = load_record(&record);
    portENTER_CRITICAL(&s_lock);
    memset(&s_record, 0, sizeof(s_record));
    s_password_configured = false;
    s_session_active = false;
    s_session_expires_ms = 0;
    s_failed_attempts = 0;
    s_lockout_until_ms = 0;
    if (err == ESP_OK && record_valid(&record)) {
        s_record = record;
        s_password_configured = true;
        memset(s_setup_code, 0, sizeof(s_setup_code));
    } else {
        uint32_t random_value = 0;
        esp_fill_random(&random_value, sizeof(random_value));
        snprintf(s_setup_code, sizeof(s_setup_code), "SETUP-%06lu",
                 (unsigned long)(100000u + random_value % 900000u));
    }
    bool setup_required = !s_password_configured;
    char setup_code[sizeof(s_setup_code)];
    strlcpy(setup_code, s_setup_code, sizeof(setup_code));
    portEXIT_CRITICAL(&s_lock);
    memset(&record, 0, sizeof(record));

    if (setup_required) {
        ESP_LOGW(TAG, "Engineering password is not configured");
        ESP_LOGW(TAG, "One-time Engineering setup code: %s", setup_code);
        ESP_LOGW(TAG, "Sign in with this serial-console code and immediately set a permanent password");
    } else {
        ESP_LOGI(TAG, "Engineering password authentication enabled");
    }
    memset(setup_code, 0, sizeof(setup_code));
    return ESP_OK;
}

esp_err_t engineering_auth_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t handlers[] = {
        {.uri = "/api/engineering/session", .method = HTTP_GET, .handler = session_get},
        {.uri = "/api/engineering/login", .method = HTTP_POST, .handler = login_post},
        {.uri = "/api/engineering/logout", .method = HTTP_POST, .handler = logout_post},
        {.uri = "/api/engineering/password", .method = HTTP_POST, .handler = password_post},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[index]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
