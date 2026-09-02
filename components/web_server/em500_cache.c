#include "em500_cache.h"

#include <string.h>

#include "config_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "meter_manager.h"

#define EM500_CACHE_TASK_STACK 6144U
#define EM500_CACHE_TASK_PRIORITY 4U
#define EM500_CACHE_IDLE_MS 100U
#define EM500_INSTANTANEOUS_PERIOD_MS 2000U
#define EM500_ENERGY_PERIOD_MS 30000U
#define EM500_SETUP_PERIOD_MS 300000U
#define EM500_INSTANTANEOUS_STALE_MS 6000U
#define EM500_ENERGY_STALE_MS 90000U
#define EM500_SETUP_STALE_MS 900000U

typedef struct {
    bool has_data;
    esp_err_t last_error;
    uint32_t updated_ms;
    uint32_t response_ms;
    uint32_t success_count;
    uint32_t error_count;
} group_runtime_t;

typedef struct {
    portMUX_TYPE lock;
    bool configured;
    bool scan_in_progress;
    uint8_t function_code;
    uint8_t address_base;
    uint8_t requested_scopes;
    uint32_t generation;
    uint32_t requested_ms;
    uint32_t next_instantaneous_ms;
    uint32_t next_energy_ms;
    uint32_t next_setup_ms;
    group_runtime_t instantaneous;
    group_runtime_t source_input;
    group_runtime_t energy;
    group_runtime_t setup;
    uint16_t instantaneous_registers[100];
    uint16_t source_register;
    uint16_t energy_totals[80];
    uint16_t hour_counters[10];
    uint16_t phase_l1[40];
    uint16_t phase_l2[40];
    uint16_t phase_l3[40];
    uint16_t setup_registers[14];
} cache_slot_t;

static const char *TAG = "em500_cache";
static cache_slot_t s_slots[APP_MAX_METERS];
static TaskHandle_t s_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool due(uint32_t timestamp, uint32_t deadline)
{
    return deadline == 0U || (int32_t)(timestamp - deadline) >= 0;
}

static uint16_t pdu_address(uint16_t table_address, uint8_t address_base)
{
    return table_address >= address_base
               ? (uint16_t)(table_address - address_base)
               : table_address;
}

static esp_err_t read_block(uint8_t meter_index, uint8_t function_code,
                            uint8_t address_base, uint16_t table_address,
                            uint16_t count, uint16_t *registers)
{
    return meter_manager_read_registers(meter_index, function_code,
                                        pdu_address(table_address, address_base),
                                        count, registers);
}

static void group_success(group_runtime_t *group, uint32_t timestamp,
                          uint32_t response_ms)
{
    group->has_data = true;
    group->last_error = ESP_OK;
    group->updated_ms = timestamp;
    group->response_ms = response_ms;
    group->success_count++;
}

static void group_failure(group_runtime_t *group, esp_err_t error,
                          uint32_t response_ms)
{
    group->last_error = error;
    group->response_ms = response_ms;
    group->error_count++;
}

static void begin_scan(cache_slot_t *slot)
{
    portENTER_CRITICAL(&slot->lock);
    slot->scan_in_progress = true;
    portEXIT_CRITICAL(&slot->lock);
}

static void end_scan(cache_slot_t *slot)
{
    portENTER_CRITICAL(&slot->lock);
    slot->scan_in_progress = false;
    portEXIT_CRITICAL(&slot->lock);
}

static void acquire_instantaneous(uint8_t index, cache_slot_t *slot,
                                  uint8_t function_code, uint8_t address_base,
                                  uint32_t timestamp)
{
    uint16_t values[100] = {0};
    uint16_t source = 0;
    uint32_t started = now_ms();
    esp_err_t first = read_block(index, function_code, address_base,
                                 0x0002, 78, values);
    esp_err_t second = first == ESP_OK
                           ? read_block(index, function_code, address_base,
                                        0x0050, 22, &values[78])
                           : first;
    uint32_t elapsed = now_ms() - started;

    started = now_ms();
    esp_err_t source_error = read_block(index, function_code, address_base,
                                        EM500_SOURCE_INPUT_TABLE_ADDRESS, 1, &source);
    uint32_t source_elapsed = now_ms() - started;

    portENTER_CRITICAL(&slot->lock);
    if (first == ESP_OK && second == ESP_OK) {
        memcpy(slot->instantaneous_registers, values, sizeof(values));
        group_success(&slot->instantaneous, timestamp, elapsed);
    } else {
        group_failure(&slot->instantaneous,
                      first != ESP_OK ? first : second, elapsed);
    }
    if (source_error == ESP_OK) {
        slot->source_register = source;
        group_success(&slot->source_input, timestamp, source_elapsed);
    } else {
        group_failure(&slot->source_input, source_error, source_elapsed);
    }
    slot->next_instantaneous_ms = timestamp + EM500_INSTANTANEOUS_PERIOD_MS;
    portEXIT_CRITICAL(&slot->lock);
}

static void acquire_energy(uint8_t index, cache_slot_t *slot,
                           uint8_t function_code, uint8_t address_base,
                           uint32_t timestamp)
{
    uint16_t totals[80] = {0};
    uint16_t hours[10] = {0};
    uint16_t l1[40] = {0};
    uint16_t l2[40] = {0};
    uint16_t l3[40] = {0};
    uint32_t started = now_ms();
    esp_err_t error = read_block(index, function_code, address_base,
                                 0x1B20, 80, totals);
    if (error == ESP_OK) error = read_block(index, function_code, address_base,
                                            0x1E00, 10, hours);
    if (error == ESP_OK) error = read_block(index, function_code, address_base,
                                            0x1E20, 40, l1);
    if (error == ESP_OK) error = read_block(index, function_code, address_base,
                                            0x1E48, 40, l2);
    if (error == ESP_OK) error = read_block(index, function_code, address_base,
                                            0x1E70, 40, l3);
    uint32_t elapsed = now_ms() - started;

    portENTER_CRITICAL(&slot->lock);
    if (error == ESP_OK) {
        memcpy(slot->energy_totals, totals, sizeof(totals));
        memcpy(slot->hour_counters, hours, sizeof(hours));
        memcpy(slot->phase_l1, l1, sizeof(l1));
        memcpy(slot->phase_l2, l2, sizeof(l2));
        memcpy(slot->phase_l3, l3, sizeof(l3));
        group_success(&slot->energy, timestamp, elapsed);
    } else {
        group_failure(&slot->energy, error, elapsed);
    }
    slot->next_energy_ms = timestamp + EM500_ENERGY_PERIOD_MS;
    portEXIT_CRITICAL(&slot->lock);
}

static void acquire_setup(uint8_t index, cache_slot_t *slot,
                          uint8_t function_code, uint8_t address_base,
                          uint32_t timestamp)
{
    uint16_t values[14] = {0};
    uint32_t started = now_ms();
    esp_err_t error = read_block(index, function_code, address_base,
                                 0x5000, 14, values);
    uint32_t elapsed = now_ms() - started;

    portENTER_CRITICAL(&slot->lock);
    if (error == ESP_OK) {
        memcpy(slot->setup_registers, values, sizeof(values));
        group_success(&slot->setup, timestamp, elapsed);
    } else {
        group_failure(&slot->setup, error, elapsed);
    }
    slot->next_setup_ms = timestamp + EM500_SETUP_PERIOD_MS;
    portEXIT_CRITICAL(&slot->lock);
}

static void cache_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint8_t meter_count = meter_manager_get_count();
        if (meter_count > APP_MAX_METERS) meter_count = APP_MAX_METERS;
        uint32_t timestamp = now_ms();

        for (uint8_t index = 0; index < meter_count; ++index) {
            cache_slot_t *slot = &s_slots[index];
            portENTER_CRITICAL(&slot->lock);
            bool configured = slot->configured;
            uint8_t function_code = slot->function_code;
            uint8_t address_base = slot->address_base;
            uint8_t scopes = slot->requested_scopes;
            uint32_t next_instantaneous = slot->next_instantaneous_ms;
            uint32_t next_energy = slot->next_energy_ms;
            uint32_t next_setup = slot->next_setup_ms;
            portEXIT_CRITICAL(&slot->lock);
            if (!configured || scopes == 0U) continue;

            bool scan_instantaneous = (scopes & EM500_CACHE_SCOPE_INSTANTANEOUS) &&
                                      due(timestamp, next_instantaneous);
            bool scan_energy = (scopes & EM500_CACHE_SCOPE_ENERGY) &&
                               due(timestamp, next_energy);
            bool scan_setup = (scopes & EM500_CACHE_SCOPE_SETUP) &&
                              due(timestamp, next_setup);
            if (!scan_instantaneous && !scan_energy && !scan_setup) continue;

            begin_scan(slot);
            if (scan_instantaneous) {
                acquire_instantaneous(index, slot, function_code,
                                      address_base, timestamp);
            }
            if (scan_energy) {
                acquire_energy(index, slot, function_code, address_base, timestamp);
            }
            if (scan_setup) {
                acquire_setup(index, slot, function_code, address_base, timestamp);
            }
            end_scan(slot);
        }
        vTaskDelay(pdMS_TO_TICKS(EM500_CACHE_IDLE_MS));
    }
}

static void invalidate_slot(cache_slot_t *slot, uint8_t function_code,
                            uint8_t address_base)
{
    slot->function_code = function_code;
    slot->address_base = address_base;
    slot->generation++;
    slot->next_instantaneous_ms = 0;
    slot->next_energy_ms = 0;
    slot->next_setup_ms = 0;
    memset(&slot->instantaneous, 0, sizeof(slot->instantaneous));
    memset(&slot->source_input, 0, sizeof(slot->source_input));
    memset(&slot->energy, 0, sizeof(slot->energy));
    memset(&slot->setup, 0, sizeof(slot->setup));
}

esp_err_t em500_cache_init(void)
{
    if (s_task) return ESP_OK;
    for (uint8_t index = 0; index < APP_MAX_METERS; ++index) {
        cache_slot_t *slot = &s_slots[index];
        memset(slot, 0, sizeof(*slot));
        slot->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    }
    /* NONCRITICAL_PSRAM_ELIGIBLE. This worker only performs Modbus/TCP
     * acquisition and esp_timer reads: it never calls an NVS, esp_partition or
     * esp_flash API, so its stack is never touched while the flash cache is
     * disabled, and it carries no control deadline. Moving it out of the scarce
     * internal DMA-capable pool returns its stack to the Product Core's
     * safety/control tasks, which stay internal by design. */
    BaseType_t created = xTaskCreateWithCaps(cache_task, "em500_cache",
                                             EM500_CACHE_TASK_STACK, NULL,
                                             EM500_CACHE_TASK_PRIORITY, &s_task,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "background acquisition cache started");
    return ESP_OK;
}

esp_err_t em500_cache_request(uint8_t meter_index,
                              uint8_t function_code,
                              uint8_t address_base,
                              uint8_t scope_mask)
{
    if (meter_index >= meter_manager_get_count() || meter_index >= APP_MAX_METERS ||
        (function_code != 3U && function_code != 4U) || address_base > 1U ||
        scope_mask == 0U || (scope_mask & ~EM500_CACHE_SCOPE_ALL) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t requested_ms = now_ms();
    cache_slot_t *slot = &s_slots[meter_index];
    portENTER_CRITICAL(&slot->lock);
    bool variant_change = slot->configured &&
                          (slot->function_code != function_code ||
                           slot->address_base != address_base);
    if (variant_change && slot->scan_in_progress) {
        portEXIT_CRITICAL(&slot->lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!slot->configured || variant_change) {
        invalidate_slot(slot, function_code, address_base);
    }
    slot->configured = true;
    slot->requested_scopes |= scope_mask;
    slot->requested_ms = requested_ms;
    portEXIT_CRITICAL(&slot->lock);
    return ESP_OK;
}

static em500_cache_group_status_t status_snapshot(const group_runtime_t *group,
                                                   uint32_t timestamp,
                                                   uint32_t stale_after_ms)
{
    em500_cache_group_status_t result = {
        .has_data = group->has_data,
        .last_error = group->last_error,
        .updated_ms = group->updated_ms,
        .response_ms = group->response_ms,
        .success_count = group->success_count,
        .error_count = group->error_count,
    };
    result.age_ms = group->has_data ? timestamp - group->updated_ms : 0U;
    result.stale = !group->has_data || result.age_ms > stale_after_ms;
    return result;
}

bool em500_cache_get_status(uint8_t meter_index,
                            em500_cache_status_t *out_status)
{
    if (!out_status || meter_index >= APP_MAX_METERS ||
        meter_index >= meter_manager_get_count()) return false;
    cache_slot_t *slot = &s_slots[meter_index];
    uint32_t timestamp = now_ms();
    portENTER_CRITICAL(&slot->lock);
    *out_status = (em500_cache_status_t){
        .configured = slot->configured,
        .scan_in_progress = slot->scan_in_progress,
        .function_code = slot->function_code,
        .address_base = slot->address_base,
        .requested_scopes = slot->requested_scopes,
        .generation = slot->generation,
        .requested_ms = slot->requested_ms,
        .instantaneous = status_snapshot(&slot->instantaneous, timestamp,
                                         EM500_INSTANTANEOUS_STALE_MS),
        .source_input = status_snapshot(&slot->source_input, timestamp,
                                        EM500_INSTANTANEOUS_STALE_MS),
        .energy = status_snapshot(&slot->energy, timestamp,
                                  EM500_ENERGY_STALE_MS),
        .setup = status_snapshot(&slot->setup, timestamp,
                                 EM500_SETUP_STALE_MS),
    };
    portEXIT_CRITICAL(&slot->lock);
    return true;
}

static esp_err_t copy_cached(cache_slot_t *slot, const group_runtime_t *group,
                             const uint16_t *source, uint16_t expected_count,
                             uint16_t count, uint16_t *registers)
{
    if (count != expected_count) return ESP_ERR_INVALID_SIZE;
    if (!group->has_data) {
        return group->last_error != ESP_OK ? group->last_error
                                           : ESP_ERR_INVALID_STATE;
    }
    memcpy(registers, source, (size_t)count * sizeof(registers[0]));
    return ESP_OK;
}

esp_err_t em500_cache_read_registers(uint8_t meter_index,
                                     uint8_t function_code,
                                     uint8_t address_base,
                                     uint16_t table_address,
                                     uint16_t count,
                                     uint16_t *registers)
{
    if (!registers || meter_index >= APP_MAX_METERS ||
        meter_index >= meter_manager_get_count()) return ESP_ERR_INVALID_ARG;
    cache_slot_t *slot = &s_slots[meter_index];
    portENTER_CRITICAL(&slot->lock);
    if (!slot->configured || slot->function_code != function_code ||
        slot->address_base != address_base) {
        portEXIT_CRITICAL(&slot->lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
    if (table_address == 0x0002U) {
        result = copy_cached(slot, &slot->instantaneous,
                             slot->instantaneous_registers, 78U, count, registers);
    } else if (table_address == 0x0050U) {
        result = copy_cached(slot, &slot->instantaneous,
                             &slot->instantaneous_registers[78], 22U, count, registers);
    } else if (table_address == EM500_SOURCE_INPUT_TABLE_ADDRESS) {
        result = copy_cached(slot, &slot->source_input,
                             &slot->source_register, 1U, count, registers);
    } else if (table_address == 0x1B20U) {
        result = copy_cached(slot, &slot->energy,
                             slot->energy_totals, 80U, count, registers);
    } else if (table_address == 0x1E00U) {
        result = copy_cached(slot, &slot->energy,
                             slot->hour_counters, 10U, count, registers);
    } else if (table_address == 0x1E20U) {
        result = copy_cached(slot, &slot->energy,
                             slot->phase_l1, 40U, count, registers);
    } else if (table_address == 0x1E48U) {
        result = copy_cached(slot, &slot->energy,
                             slot->phase_l2, 40U, count, registers);
    } else if (table_address == 0x1E70U) {
        result = copy_cached(slot, &slot->energy,
                             slot->phase_l3, 40U, count, registers);
    } else if (table_address == 0x5000U) {
        result = copy_cached(slot, &slot->setup,
                             slot->setup_registers, 14U, count, registers);
    }
    portEXIT_CRITICAL(&slot->lock);
    return result;
}
