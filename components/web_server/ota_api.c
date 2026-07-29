#include "ota_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "control_engine.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_manager.h"
#include "sdkconfig.h"

#define OTA_UPLOAD_CHUNK_BYTES 4096U
#define OTA_UPLOAD_DEADLINE_MS 600000U
#define OTA_SAFE_ZERO_TIMEOUT_MS 5000U
#define OTA_SAFE_ZERO_TOLERANCE_KW 0.01f

static const char *TAG = "ota_api";

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root, const char *status)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t error = httpd_resp_sendstr(request, json);
    free(json);
    return error;
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *message, esp_err_t error)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message);
    cJSON_AddNumberToObject(root, "error_code", error);
    cJSON_AddStringToObject(root, "error_name", esp_err_to_name(error));
    return send_json(request, root, status);
}

static cJSON *status_json(void)
{
    ota_manager_status_t status;
    ota_manager_get_status(&status);
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "state", ota_manager_state_name(status.state));
    cJSON_AddBoolToObject(root, "upload_active", status.upload_active);
    cJSON_AddBoolToObject(root, "pending_verify", status.pending_verify);
    cJSON_AddBoolToObject(root, "rollback_enabled", status.rollback_enabled);
    cJSON_AddBoolToObject(root, "validation_scheduled", status.validation_scheduled);
    cJSON_AddBoolToObject(root, "update_staged", status.update_staged);
    cJSON_AddBoolToObject(root, "image_identity_verified",
                         status.image_identity_verified);
    cJSON_AddBoolToObject(root, "image_validated", status.image_validated);
    cJSON_AddBoolToObject(root, "boot_partition_preserved_after_abort",
                         status.boot_partition_preserved_after_abort);
    cJSON_AddNumberToObject(root, "image_size", (double)status.image_size);
    cJSON_AddNumberToObject(root, "bytes_written", (double)status.bytes_written);
    double progress = status.image_size > 0U
                          ? (100.0 * (double)status.bytes_written /
                             (double)status.image_size)
                          : 0.0;
    cJSON_AddNumberToObject(root, "progress_percent", progress);
    cJSON_AddNumberToObject(root, "started_ms", status.started_ms);
    cJSON_AddNumberToObject(root, "completed_ms", status.completed_ms);
    cJSON_AddNumberToObject(root, "last_error", status.last_error);
    cJSON_AddStringToObject(root, "last_error_name",
                            esp_err_to_name(status.last_error));
    cJSON_AddStringToObject(root, "running_partition", status.running_partition);
    cJSON_AddStringToObject(root, "boot_partition", status.boot_partition);
    cJSON_AddStringToObject(root, "update_partition", status.update_partition);
    cJSON_AddStringToObject(root, "running_project", status.running_project);
    cJSON_AddStringToObject(root, "running_version", status.running_version);
    cJSON_AddStringToObject(root, "candidate_project", status.candidate_project);
    cJSON_AddStringToObject(root, "candidate_version", status.candidate_version);
    cJSON_AddStringToObject(root, "candidate_idf_version",
                            status.candidate_idf_version);
    cJSON_AddNumberToObject(root, "candidate_secure_version",
                            status.candidate_secure_version);
    cJSON_AddStringToObject(root, "expected_product_id", OTA_MANAGER_PRODUCT_ID);
    cJSON_AddNumberToObject(root, "expected_target_chip_id",
                            CONFIG_IDF_FIRMWARE_CHIP_ID);
    cJSON_AddNumberToObject(root, "max_image_bytes",
                            update ? (double)update->size : 0.0);
    cJSON_AddBoolToObject(root, "nvs_erase_required", false);
    cJSON_AddBoolToObject(root, "automatic_reboot", false);
    cJSON_AddStringToObject(root, "upload_endpoint", "/api/ota/upload");
    cJSON_AddStringToObject(root, "reboot_endpoint", "/api/ota/reboot");
    return root;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    cJSON *root = status_json();
    return root ? send_json(request, root, NULL) : httpd_resp_send_500(request);
}

static esp_err_t receive_exact(httpd_req_t *request, uint8_t *buffer,
                               size_t length, uint64_t deadline_ms)
{
    size_t received = 0U;
    while (received < length) {
        int result = httpd_req_recv(request, (char *)buffer + received,
                                    length - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            if (now_ms() >= deadline_ms) return ESP_ERR_TIMEOUT;
            continue;
        }
        if (result <= 0) return ESP_FAIL;
        received += (size_t)result;
    }
    return ESP_OK;
}

static esp_err_t wait_for_safe_zero(void)
{
    control_engine_force_disable();
    const uint64_t deadline = now_ms() + OTA_SAFE_ZERO_TIMEOUT_MS;
    while (now_ms() < deadline) {
        control_status_t status = {0};
        control_engine_get_status(&status);
        if (!status.enabled && isfinite(status.applied_pv_kw) &&
            fabsf(status.applied_pv_kw) <= OTA_SAFE_ZERO_TOLERANCE_KW) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_ERR_TIMEOUT;
}

static bool content_type_valid(httpd_req_t *request)
{
    size_t length = httpd_req_get_hdr_value_len(request, "Content-Type");
    if (length == 0U || length >= 64U) return false;
    char value[64] = {0};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", value,
                                    sizeof(value)) != ESP_OK) {
        return false;
    }
    return strncmp(value, "application/octet-stream",
                   strlen("application/octet-stream")) == 0;
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    if (!content_type_valid(request)) {
        return send_error(request, "415 Unsupported Media Type",
                          "Upload the firmware .bin as application/octet-stream",
                          ESP_ERR_INVALID_ARG);
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    const size_t image_size = request->content_len > 0
                                  ? (size_t)request->content_len
                                  : 0U;
    if (!update || image_size < OTA_MANAGER_PREFIX_BYTES ||
        image_size > update->size) {
        return send_error(request, "413 Payload Too Large",
                          "Firmware image is missing, truncated, or larger than the inactive OTA slot",
                          ESP_ERR_INVALID_SIZE);
    }

    const uint64_t deadline = now_ms() + OTA_UPLOAD_DEADLINE_MS;
    uint8_t prefix[OTA_MANAGER_PREFIX_BYTES];
    esp_err_t error = receive_exact(request, prefix, sizeof(prefix), deadline);
    if (error != ESP_OK) {
        return send_error(request, error == ESP_ERR_TIMEOUT
                                       ? "408 Request Timeout"
                                       : "400 Bad Request",
                          "Firmware header could not be received completely", error);
    }

    const esp_image_header_t *image_header =
        (const esp_image_header_t *)prefix;
    esp_app_desc_t candidate = {0};
    error = ota_manager_validate_prefix(prefix, sizeof(prefix), image_size,
                                        &candidate);
    if (error != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Firmware image product ID or ESP target does not match this controller, exceeds the OTA slot, or violates secure-version policy",
                          error);
    }

    error = wait_for_safe_zero();
    if (error != ESP_OK) {
        return send_error(request, "409 Conflict",
                          "OTA refused because the controller could not confirm a safe zero inverter command",
                          error);
    }

    ota_manager_session_t session = {0};
    error = ota_manager_begin(image_size, image_header, &candidate, &session);
    if (error != ESP_OK) {
        return send_error(request, error == ESP_ERR_INVALID_STATE
                                       ? "409 Conflict"
                                       : "500 Internal Server Error",
                          "OTA session could not be started", error);
    }

    error = ota_manager_write(&session, prefix, sizeof(prefix));
    if (error != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "Firmware header could not be written", error);
    }

    uint8_t *buffer = malloc(OTA_UPLOAD_CHUNK_BYTES);
    if (!buffer) {
        ota_manager_abort(&session, ESP_ERR_NO_MEM);
        return send_error(request, "503 Service Unavailable",
                          "Insufficient memory for the OTA streaming buffer",
                          ESP_ERR_NO_MEM);
    }

    size_t remaining = image_size - sizeof(prefix);
    while (remaining > 0U) {
        size_t chunk = remaining < OTA_UPLOAD_CHUNK_BYTES
                           ? remaining
                           : OTA_UPLOAD_CHUNK_BYTES;
        error = receive_exact(request, buffer, chunk, deadline);
        if (error != ESP_OK) break;
        error = ota_manager_write(&session, buffer, chunk);
        if (error != ESP_OK) break;
        remaining -= chunk;
    }
    free(buffer);

    if (error != ESP_OK || remaining != 0U) {
        if (session.active) ota_manager_abort(&session, error == ESP_OK ? ESP_FAIL : error);
        return send_error(request, error == ESP_ERR_TIMEOUT
                                       ? "408 Request Timeout"
                                       : "400 Bad Request",
                          "Firmware upload was interrupted or incomplete; the existing boot partition was preserved",
                          error == ESP_OK ? ESP_FAIL : error);
    }

    error = ota_manager_finish(&session);
    if (error != ESP_OK) {
        if (session.active) ota_manager_abort(&session, error);
        return send_error(request, "400 Bad Request",
                          "ESP-IDF rejected the completed firmware image before boot selection changed",
                          error);
    }

    cJSON *root = status_json();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "accepted", true);
    cJSON_AddBoolToObject(root, "reboot_required", true);
    cJSON_AddStringToObject(root, "message",
                            "Firmware identity and complete image validated before staging. Reboot explicitly to test the new slot with rollback protection.");
    return send_json(request, root, NULL);
}

static void delayed_restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t reboot_handler(httpd_req_t *request)
{
    ota_manager_status_t status;
    ota_manager_get_status(&status);
    if (status.upload_active || !status.update_staged ||
        !status.image_identity_verified || !status.image_validated) {
        return send_error(request, "409 Conflict",
                          "No completely validated OTA image is staged for reboot",
                          ESP_ERR_INVALID_STATE);
    }

    if (xTaskCreate(delayed_restart_task, "ota_reboot", 2048, NULL, 8, NULL) != pdPASS) {
        return send_error(request, "503 Service Unavailable",
                          "Unable to schedule the OTA reboot", ESP_ERR_NO_MEM);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "restarting", true);
    cJSON_AddBoolToObject(root, "rollback_protected", status.rollback_enabled);
    cJSON_AddStringToObject(root, "message",
                            "Controller will restart into the staged image. NVS is preserved.");
    ESP_LOGW(TAG, "Authenticated OTA reboot requested");
    return send_json(request, root, NULL);
}

esp_err_t ota_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t handlers[] = {
        {.uri = "/api/ota/status", .method = HTTP_GET,
         .handler = status_handler},
        {.uri = "/api/ota/upload", .method = HTTP_POST,
         .handler = upload_handler},
        {.uri = "/api/ota/reboot", .method = HTTP_POST,
         .handler = reboot_handler},
    };
    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            TAG, "OTA handler registration failed");
    }
    return ESP_OK;
}
