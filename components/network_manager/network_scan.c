#include "network_manager.h"
#include "network_scan_internal.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RAW_SCAN_LIMIT 64

static const char *TAG = "wifi_scan";
static SemaphoreHandle_t s_snapshot_lock;
static network_scan_snapshot_t s_snapshot;
static TaskHandle_t s_manager_task;
static uint32_t s_manager_wake_bit;
static bool s_initialized;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static int compare_rssi_desc(const void *left, const void *right)
{
    const network_scan_ap_t *a = left;
    const network_scan_ap_t *b = right;
    return (int)b->rssi - (int)a->rssi;
}

static void complete_failure(uint32_t generation, esp_err_t error)
{
    if (!s_snapshot_lock) return;
    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    if (s_snapshot.generation == generation &&
        s_snapshot.state == NETWORK_SCAN_RUNNING) {
        s_snapshot.state = NETWORK_SCAN_FAILED;
        s_snapshot.last_error = error;
        s_snapshot.completed_ms = now_ms();
    }
    xSemaphoreGive(s_snapshot_lock);
}

static esp_err_t collect_scan(network_scan_snapshot_t *next)
{
    if (!next) return ESP_ERR_INVALID_ARG;

    wifi_scan_config_t scan_config = {
        .show_hidden = true
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) return err;

    uint16_t record_count = 0;
    err = esp_wifi_scan_get_ap_num(&record_count);
    if (err != ESP_OK || record_count == 0) return err;
    if (record_count > RAW_SCAN_LIMIT) record_count = RAW_SCAN_LIMIT;

    wifi_ap_record_t *records = calloc(record_count, sizeof(*records));
    network_scan_ap_t *candidates = calloc(record_count, sizeof(*candidates));
    if (!records || !candidates) {
        free(records);
        free(candidates);
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&record_count, records);
    if (err != ESP_OK) {
        free(records);
        free(candidates);
        return err;
    }

    network_status_t status = {0};
    network_manager_get_status(&status);
    uint16_t unique_count = 0;

    for (uint16_t index = 0; index < record_count; ++index) {
        const char *ssid = (const char *)records[index].ssid;
        if (!ssid[0]) continue;

        int existing = -1;
        for (uint16_t candidate = 0; candidate < unique_count; ++candidate) {
            if (strcmp(candidates[candidate].ssid, ssid) == 0) {
                existing = (int)candidate;
                break;
            }
        }

        if (existing >= 0 && candidates[existing].rssi >= records[index].rssi) continue;
        network_scan_ap_t *target = existing >= 0
                                        ? &candidates[existing]
                                        : &candidates[unique_count++];
        memset(target, 0, sizeof(*target));
        strlcpy(target->ssid, ssid, sizeof(target->ssid));
        target->rssi = records[index].rssi;
        target->channel = records[index].primary;
        target->auth_mode = (uint8_t)records[index].authmode;
        target->configured_primary = strcmp(ssid, status.ssid) == 0 &&
                                     status.network_ready &&
                                     !status.using_fallback_sta;
        target->configured_fallback = strcmp(ssid, status.ssid) == 0 &&
                                      status.network_ready &&
                                      status.using_fallback_sta;
        target->connected = status.network_ready && strcmp(ssid, status.ssid) == 0;
    }

    qsort(candidates, unique_count, sizeof(*candidates), compare_rssi_desc);
    next->count = unique_count > NETWORK_SCAN_MAX_RESULTS
                      ? NETWORK_SCAN_MAX_RESULTS
                      : unique_count;
    if (next->count > 0) {
        memcpy(next->results, candidates,
               next->count * sizeof(*next->results));
    }

    free(records);
    free(candidates);
    return ESP_OK;
}

esp_err_t network_scan_service_init(TaskHandle_t manager_task,
                                    uint32_t manager_wake_bit)
{
    if (!manager_task || manager_wake_bit == 0U) return ESP_ERR_INVALID_ARG;
    if (s_initialized) return ESP_OK;

    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = NETWORK_SCAN_IDLE;
    s_snapshot_lock = lock;
    s_manager_task = manager_task;
    s_manager_wake_bit = manager_wake_bit;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t network_manager_request_scan(void)
{
    if (!s_initialized || !s_snapshot_lock || !s_manager_task) {
        return ESP_ERR_INVALID_STATE;
    }

    network_status_t status = {0};
    network_manager_get_status(&status);
    if (!status.network_ready && !status.fallback_ap_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status.state == NETWORK_WIFI_SCANNING ||
        status.state == NETWORK_WIFI_CONNECTING_PRIMARY ||
        status.state == NETWORK_WIFI_CONNECTING_FALLBACK) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    if (s_snapshot.state == NETWORK_SCAN_RUNNING) {
        xSemaphoreGive(s_snapshot_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_snapshot.state = NETWORK_SCAN_RUNNING;
    s_snapshot.last_error = ESP_OK;
    s_snapshot.started_ms = now_ms();
    s_snapshot.completed_ms = 0;
    s_snapshot.count = 0;
    s_snapshot.generation++;
    if (s_snapshot.generation == 0) s_snapshot.generation = 1;
    xSemaphoreGive(s_snapshot_lock);

    xTaskNotify(s_manager_task, s_manager_wake_bit, eSetBits);
    return ESP_OK;
}

void network_scan_service_execute(void)
{
    if (!s_initialized || !s_snapshot_lock) return;

    uint32_t generation = 0;
    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    if (s_snapshot.state != NETWORK_SCAN_RUNNING) {
        xSemaphoreGive(s_snapshot_lock);
        return;
    }
    generation = s_snapshot.generation;
    xSemaphoreGive(s_snapshot_lock);

    network_status_t status = {0};
    network_manager_get_status(&status);
    if ((!status.network_ready && !status.fallback_ap_active) ||
        status.state == NETWORK_WIFI_SCANNING ||
        status.state == NETWORK_WIFI_CONNECTING_PRIMARY ||
        status.state == NETWORK_WIFI_CONNECTING_FALLBACK) {
        complete_failure(generation, ESP_ERR_INVALID_STATE);
        return;
    }

    network_scan_snapshot_t *next = calloc(1, sizeof(*next));
    if (!next) {
        complete_failure(generation, ESP_ERR_NO_MEM);
        return;
    }
    next->generation = generation;
    next->started_ms = now_ms();

    esp_err_t err = collect_scan(next);
    next->completed_ms = now_ms();
    next->last_error = err;
    next->state = err == ESP_OK ? NETWORK_SCAN_COMPLETE : NETWORK_SCAN_FAILED;

    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    if (s_snapshot.generation == generation &&
        s_snapshot.state == NETWORK_SCAN_RUNNING) {
        s_snapshot = *next;
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Manager-owned scan generation %u completed with %u visible networks",
                     (unsigned)generation, (unsigned)next->count);
        } else {
            ESP_LOGW(TAG, "Manager-owned scan generation %u failed: %s",
                     (unsigned)generation, esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_snapshot_lock);
    free(next);
}

void network_scan_service_reject(esp_err_t error)
{
    if (!s_initialized || !s_snapshot_lock) return;
    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    uint32_t generation = s_snapshot.generation;
    bool running = s_snapshot.state == NETWORK_SCAN_RUNNING;
    xSemaphoreGive(s_snapshot_lock);
    if (running) complete_failure(generation, error);
}

void network_manager_get_scan_snapshot(network_scan_snapshot_t *out_snapshot)
{
    if (!out_snapshot) return;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (!s_initialized || !s_snapshot_lock) {
        out_snapshot->state = NETWORK_SCAN_IDLE;
        return;
    }

    xSemaphoreTake(s_snapshot_lock, portMAX_DELAY);
    *out_snapshot = s_snapshot;
    xSemaphoreGive(s_snapshot_lock);
}
