#include "engineering_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

#define AUTH_NS "eng_auth"
#define AUTH_SALT_KEY "salt"
#define AUTH_HASH_KEY "hash"
#define AUTH_HASH_ROUNDS 2048
#define AUTH_SESSION_MS (30U * 60U * 1000U)
#define AUTH_LOCKOUT_MS (5U * 60U * 1000U)
#define AUTH_MAX_FAILURES 5U
#define AUTH_BODY_MAX 512
#define AUTH_COOKIE_NAME "AMXENG"
#define AUTH_SHA256_BYTES 32U
#define AUTH_HASH_INPUT_MAX 96U

/* Development convenience only. Set to 0 before any resale/production build.
 * When enabled, opening the web interface creates an authenticated engineering
 * session automatically. The normal unique AMX-XXXXXX and stored-password
 * mechanisms remain intact for production mode. */
#define AUTH_DEVELOPMENT_AUTO_UNLOCK 1

static uint8_t s_salt[16];
static uint8_t s_hash[AUTH_SHA256_BYTES];
static bool s_setup_required;
static char s_session_token[65];
static uint32_t s_session_expires_ms;
static uint32_t s_failed_attempts;
static uint32_t s_lockout_until_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool sha256_join(const uint8_t *first, size_t first_size,
                        const uint8_t *second, size_t second_size,
                        uint8_t out[AUTH_SHA256_BYTES])
{
    if (!first || !second || !out || first_size + second_size > AUTH_HASH_INPUT_MAX) {
        return false;
    }
    uint8_t input[AUTH_HASH_INPUT_MAX];
    memcpy(input, first, first_size);
    memcpy(input + first_size, second, second_size);
    size_t output_size = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256,
                                           input, first_size + second_size,
                                           out, AUTH_SHA256_BYTES,
                                           &output_size);
    memset(input, 0, sizeof(input));
    return status == PSA_SUCCESS && output_size == AUTH_SHA256_BYTES;
}

static bool hash_password(const char *password, const uint8_t salt[16],
                          uint8_t out[AUTH_SHA256_BYTES])
{
    if (!password || !salt || !out) return false;
    size_t password_size = strlen(password);
    if (password_size == 0 || password_size > 64U) return false;

    uint8_t current[AUTH_SHA256_BYTES] = {0};
    if (!sha256_join(salt, 16, (const uint8_t *)password, password_size, current)) {
        return false;
    }
    for (unsigned round = 1; round < AUTH_HASH_ROUNDS; ++round) {
        uint8_t next[AUTH_SHA256_BYTES] = {0};
        if (!sha256_join(current, sizeof(current), salt, 16, next)) {
            memset(current, 0, sizeof(current));
            return false;
        }
        memcpy(current, next, sizeof(current));
        memset(next, 0, sizeof(next));
    }
    memcpy(out, current, AUTH_SHA256_BYTES);
    memset(current, 0, sizeof(current));
    return true;
}

static void temporary_password(char out[16])
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 16, "AMX-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < size; ++i) diff |= left[i] ^ right[i];
    return diff == 0;
}

static bool constant_time_token_equal(const char *left, const char *right)
{
    if (!left || !right || strlen(left) != 64 || strlen(right) != 64) return false;
    return constant_time_equal((const uint8_t *)left, (const uint8_t *)right, 64);
}

static esp_err_t persist_credentials(const char *password)
{
    esp_fill_random(s_salt, sizeof(s_salt));
    if (!hash_password(password, s_salt, s_hash)) return ESP_FAIL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, AUTH_SALT_KEY, s_salt, sizeof(s_salt));
    if (err == ESP_OK) err = nvs_set_blob(handle, AUTH_HASH_KEY, s_hash, sizeof(s_hash));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_setup_required = false;
    return err;
}

static bool verify_password(const char *password)
{
    uint8_t candidate[AUTH_SHA256_BYTES] = {0};
    bool hashed = hash_password(password, s_salt, candidate);
    bool valid = hashed && constant_time_equal(candidate, s_hash, sizeof(candidate));
    memset(candidate, 0, sizeof(candidate));
    return valid;
}

static void new_session(void)
{
    uint8_t raw[32];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); ++i) {
        snprintf(&s_session_token[i * 2], 3, "%02x", raw[i]);
    }
    memset(raw, 0, sizeof(raw));
    s_session_token[64] = '\0';
    s_session_expires_ms = now_ms() + AUTH_SESSION_MS;
}

static void set_session_cookie(httpd_req_t *request, bool enabled)
{
    if (enabled) {
        char cookie[160];
        snprintf(cookie, sizeof(cookie),
                 AUTH_COOKIE_NAME "=%s; Path=/; Max-Age=%u; HttpOnly; SameSite=Strict",
                 s_session_token, (unsigned)(AUTH_SESSION_MS / 1000U));
        httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    } else {
        httpd_resp_set_hdr(request, "Set-Cookie",
                           AUTH_COOKIE_NAME "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    }
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

static esp_err_t receive_json(httpd_req_t *request, cJSON **out)
{
    if (!request || !out || request->content_len <= 0 || request->content_len > AUTH_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *body = malloc((size_t)request->content_len + 1U);
    if (!body) return ESP_ERR_NO_MEM;
    size_t offset = 0;
    while (offset < (size_t)request->content_len) {
        int received = httpd_req_recv(request, body + offset,
                                      request->content_len - offset);
        if (received <= 0) {
            free(body);
            return ESP_FAIL;
        }
        offset += (size_t)received;
    }
    body[offset] = '\0';
    *out = cJSON_Parse(body);
    memset(body, 0, (size_t)request->content_len + 1U);
    free(body);
    return *out ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static bool token_from_cookie(httpd_req_t *request, char token[65])
{
    size_t length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (length == 0 || length > 512) return false;
    char *cookies = malloc(length + 1U);
    if (!cookies) return false;
    bool found = false;
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookies, length + 1U) == ESP_OK) {
        const char *cursor = cookies;
        const size_t name_length = strlen(AUTH_COOKIE_NAME);
        while (*cursor) {
            while (*cursor == ' ' || *cursor == ';') cursor++;
            if (strncmp(cursor, AUTH_COOKIE_NAME "=", name_length + 1U) == 0) {
                cursor += name_length + 1U;
                size_t value_length = strcspn(cursor, "; ");
                if (value_length == 64) {
                    memcpy(token, cursor, 64);
                    token[64] = '\0';
                    found = true;
                }
                break;
            }
            cursor += strcspn(cursor, ";");
        }
    }
    memset(cookies, 0, length + 1U);
    free(cookies);
    return found;
}

static bool request_has_valid_token(httpd_req_t *request)
{
    char token[65] = {0};
    if (token_from_cookie(request, token) &&
        constant_time_token_equal(token, s_session_token)) {
        memset(token, 0, sizeof(token));
        return true;
    }

    size_t length = httpd_req_get_hdr_value_len(request, "X-Engineering-Token");
    if (length != 64) return false;
    if (httpd_req_get_hdr_value_str(request, "X-Engineering-Token",
                                     token, sizeof(token)) != ESP_OK) {
        return false;
    }
    bool valid = constant_time_token_equal(token, s_session_token);
    memset(token, 0, sizeof(token));
    return valid;
}

bool engineering_auth_is_authorized(httpd_req_t *request)
{
    if (!request || !s_session_token[0] ||
        (int32_t)(now_ms() - s_session_expires_ms) >= 0) {
        return false;
    }
    bool ok = request_has_valid_token(request);
    if (ok) s_session_expires_ms = now_ms() + AUTH_SESSION_MS;
    return ok;
}

esp_err_t engineering_auth_require(httpd_req_t *request)
{
    if (engineering_auth_is_authorized(request)) return ESP_OK;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", "Engineering authentication required");
    cJSON_AddBoolToObject(root, "engineering_auth_required", true);
    return send_json(request, "401 Unauthorized", root);
}

static esp_err_t session_get(httpd_req_t *request)
{
    bool authorized = engineering_auth_is_authorized(request);
#if AUTH_DEVELOPMENT_AUTO_UNLOCK
    if (!authorized) {
        new_session();
        set_session_cookie(request, true);
        authorized = true;
    }
#endif
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "authenticated", authorized);
    cJSON_AddBoolToObject(root, "setup_required",
                          AUTH_DEVELOPMENT_AUTO_UNLOCK ? false : s_setup_required);
    cJSON_AddBoolToObject(root, "cookie_session", true);
    cJSON_AddBoolToObject(root, "development_auto_unlock",
                          AUTH_DEVELOPMENT_AUTO_UNLOCK != 0);
    cJSON_AddNumberToObject(root, "session_timeout_minutes", AUTH_SESSION_MS / 60000U);
    cJSON_AddStringToObject(root, "temporary_password_format",
                            "AMX-XXXXXX (device label / serial log)");
    if (authorized) {
        cJSON_AddNumberToObject(root, "expires_in_ms", s_session_expires_ms - now_ms());
    }
    return send_json(request, NULL, root);
}

static esp_err_t login_post(httpd_req_t *request)
{
    uint32_t current = now_ms();
    if ((int32_t)(current - s_lockout_until_ms) < 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", "Too many failed attempts. Try again later.");
        cJSON_AddNumberToObject(root, "retry_after_ms", s_lockout_until_ms - current);
        return send_json(request, "429 Too Many Requests", root);
    }

    cJSON *json = NULL;
    if (receive_json(request, &json) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid login body");
    }
    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    bool valid = cJSON_IsString(password) && password->valuestring &&
                 verify_password(password->valuestring);
    cJSON_Delete(json);
    if (!valid) {
        s_failed_attempts++;
        if (s_failed_attempts >= AUTH_MAX_FAILURES) {
            s_failed_attempts = 0;
            s_lockout_until_ms = current + AUTH_LOCKOUT_MS;
        }
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "error", "Invalid engineering password");
        return send_json(request, "401 Unauthorized", root);
    }

    s_failed_attempts = 0;
    s_lockout_until_ms = 0;
    new_session();
    set_session_cookie(request, true);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "authenticated", true);
    cJSON_AddBoolToObject(root, "cookie_session", true);
    cJSON_AddBoolToObject(root, "password_change_recommended", s_setup_required);
    cJSON_AddNumberToObject(root, "expires_in_ms", AUTH_SESSION_MS);
    return send_json(request, NULL, root);
}

static esp_err_t logout_post(httpd_req_t *request)
{
    memset(s_session_token, 0, sizeof(s_session_token));
    s_session_expires_ms = 0;
    set_session_cookie(request, false);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "authenticated", false);
    return send_json(request, NULL, root);
}

static esp_err_t password_post(httpd_req_t *request)
{
    if (!engineering_auth_is_authorized(request)) {
        return engineering_auth_require(request);
    }
    cJSON *json = NULL;
    if (receive_json(request, &json) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid password body");
    }
    cJSON *current = cJSON_GetObjectItemCaseSensitive(json, "current_password");
    cJSON *next = cJSON_GetObjectItemCaseSensitive(json, "new_password");
    bool valid = cJSON_IsString(current) && cJSON_IsString(next) &&
                 current->valuestring && next->valuestring &&
                 verify_password(current->valuestring) &&
                 strlen(next->valuestring) >= 10 &&
                 strlen(next->valuestring) <= 64;
    esp_err_t save_err = valid ? persist_credentials(next->valuestring)
                               : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (save_err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Current password is wrong or new password is too weak");
    }
    new_session();
    set_session_cookie(request, true);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "changed", true);
    cJSON_AddBoolToObject(root, "cookie_session", true);
    return send_json(request, NULL, root);
}

esp_err_t engineering_auth_init(void)
{
    if (psa_crypto_init() != PSA_SUCCESS) return ESP_FAIL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUTH_NS, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t salt_size = sizeof(s_salt);
        size_t hash_size = sizeof(s_hash);
        err = nvs_get_blob(handle, AUTH_SALT_KEY, s_salt, &salt_size);
        if (err == ESP_OK) {
            err = nvs_get_blob(handle, AUTH_HASH_KEY, s_hash, &hash_size);
        }
        nvs_close(handle);
        if (err == ESP_OK && salt_size == sizeof(s_salt) &&
            hash_size == sizeof(s_hash)) {
            s_setup_required = false;
#if AUTH_DEVELOPMENT_AUTO_UNLOCK
            printf("Engineering development auto-unlock enabled\n");
#endif
            return ESP_OK;
        }
    }

    char password[16];
    temporary_password(password);
    esp_fill_random(s_salt, sizeof(s_salt));
    if (!hash_password(password, s_salt, s_hash)) {
        memset(password, 0, sizeof(password));
        return ESP_FAIL;
    }
    s_setup_required = true;
#if AUTH_DEVELOPMENT_AUTO_UNLOCK
    printf("Engineering development auto-unlock enabled\n");
#endif
    printf("Engineering temporary password: %s\n", password);
    memset(password, 0, sizeof(password));
    return ESP_OK;
}

esp_err_t engineering_auth_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/engineering/session", .method = HTTP_GET, .handler = session_get},
        {.uri = "/api/engineering/login", .method = HTTP_POST, .handler = login_post},
        {.uri = "/api/engineering/logout", .method = HTTP_POST, .handler = logout_post},
        {.uri = "/api/engineering/password", .method = HTTP_POST, .handler = password_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &handlers[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
