#include "meter_read_jobs.h"

#include <stdbool.h>
#include <string.h>

#include "config_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_manager.h"

#define METER_READ_JOB_SLOTS 20U
#define METER_READ_JOB_MAX_REGISTERS 125U
#define METER_READ_JOB_FRESH_MS 5000U
#define METER_READ_JOB_TASK_STACK 4096U
#define METER_READ_JOB_TASK_PRIORITY 3U
#define METER_READ_JOB_IDLE_MS 50U

typedef struct {
    bool used;
    bool pending;
    bool in_progress;
    bool has_data;
    uint8_t meter_index;
    uint8_t function_code;
    uint16_t address;
    uint16_t count;
    uint32_t generation;
    uint32_t requested_ms;
    uint32_t updated_ms;
    uint32_t response_ms;
    uint32_t success_count;
    uint32_t error_count;
    esp_err_t last_error;
    uint16_t registers[METER_READ_JOB_MAX_REGISTERS];
} meter_read_job_t;

static const char *TAG = "meter_read_jobs";
static meter_read_job_t s_jobs[METER_READ_JOB_SLOTS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool same_key(const meter_read_job_t *job,
                     uint8_t meter_index, uint8_t function_code,
                     uint16_t address, uint16_t count)
{
    return job->used && job->meter_index == meter_index &&
           job->function_code == function_code && job->address == address &&
           job->count == count;
}

static int find_job(uint8_t meter_index, uint8_t function_code,
                    uint16_t address, uint16_t count)
{
    for (unsigned index = 0; index < METER_READ_JOB_SLOTS; ++index) {
        if (same_key(&s_jobs[index], meter_index, function_code, address, count)) {
            return (int)index;
        }
    }
    return -1;
}

static int allocation_slot(void)
{
    int oldest = -1;
    uint32_t oldest_requested = UINT32_MAX;
    for (unsigned index = 0; index < METER_READ_JOB_SLOTS; ++index) {
        meter_read_job_t *job = &s_jobs[index];
        if (!job->used) return (int)index;
        if (!job->in_progress && job->requested_ms <= oldest_requested) {
            oldest_requested = job->requested_ms;
            oldest = (int)index;
        }
    }
    return oldest;
}

static int claim_pending_job(meter_read_job_t *out_job)
{
    int claimed = -1;
    portENTER_CRITICAL(&s_lock);
    for (unsigned index = 0; index < METER_READ_JOB_SLOTS; ++index) {
        meter_read_job_t *job = &s_jobs[index];
        if (!job->used || !job->pending || job->in_progress) continue;
        job->pending = false;
        job->in_progress = true;
        *out_job = *job;
        claimed = (int)index;
        break;
    }
    portEXIT_CRITICAL(&s_lock);
    return claimed;
}

static void complete_job(int index, const meter_read_job_t *claimed,
                         esp_err_t error, const uint16_t *registers,
                         uint32_t response_ms, uint32_t completed_ms)
{
    if (index < 0 || (unsigned)index >= METER_READ_JOB_SLOTS || !claimed) return;
    portENTER_CRITICAL(&s_lock);
    meter_read_job_t *job = &s_jobs[index];
    bool unchanged = job->used && job->generation == claimed->generation &&
                     same_key(job, claimed->meter_index, claimed->function_code,
                              claimed->address, claimed->count);
    if (unchanged) {
        job->in_progress = false;
        job->last_error = error;
        job->response_ms = response_ms;
        if (error == ESP_OK) {
            memcpy(job->registers, registers,
                   (size_t)job->count * sizeof(job->registers[0]));
            job->has_data = true;
            job->updated_ms = completed_ms;
            job->success_count++;
        } else {
            job->error_count++;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

static void read_job_task(void *argument)
{
    (void)argument;
    for (;;) {
        meter_read_job_t claimed = {0};
        int index = claim_pending_job(&claimed);
        if (index < 0) {
            vTaskDelay(pdMS_TO_TICKS(METER_READ_JOB_IDLE_MS));
            continue;
        }

        uint16_t registers[METER_READ_JOB_MAX_REGISTERS] = {0};
        uint32_t started_ms = now_ms();
        esp_err_t error = meter_manager_read_registers(claimed.meter_index,
                                                       claimed.function_code,
                                                       claimed.address,
                                                       claimed.count,
                                                       registers);
        uint32_t completed_ms = now_ms();
        complete_job(index, &claimed, error, registers,
                     completed_ms - started_ms, completed_ms);
    }
}

esp_err_t meter_read_jobs_init(void)
{
    if (s_task) return ESP_OK;
    portENTER_CRITICAL(&s_lock);
    memset(s_jobs, 0, sizeof(s_jobs));
    portEXIT_CRITICAL(&s_lock);
    BaseType_t created = xTaskCreate(read_job_task, "meter_read_jobs",
                                     METER_READ_JOB_TASK_STACK, NULL,
                                     METER_READ_JOB_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "bounded background read-job queue started");
    return ESP_OK;
}

esp_err_t meter_read_jobs_cached_read(uint8_t meter_index,
                                      uint8_t function_code,
                                      uint16_t address,
                                      uint16_t count,
                                      uint16_t *registers)
{
    if (!registers || meter_index >= meter_manager_get_count() ||
        meter_index >= APP_MAX_METERS ||
        (function_code != 3U && function_code != 4U) || count == 0U ||
        count > METER_READ_JOB_MAX_REGISTERS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t requested_ms = now_ms();
    esp_err_t result = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    int index = find_job(meter_index, function_code, address, count);
    if (index < 0) {
        index = allocation_slot();
        if (index >= 0) {
            meter_read_job_t *job = &s_jobs[index];
            uint32_t generation = job->generation + 1U;
            memset(job, 0, sizeof(*job));
            job->used = true;
            job->pending = true;
            job->meter_index = meter_index;
            job->function_code = function_code;
            job->address = address;
            job->count = count;
            job->generation = generation;
            job->requested_ms = requested_ms;
        } else {
            result = ESP_ERR_NO_MEM;
        }
    } else {
        meter_read_job_t *job = &s_jobs[index];
        job->requested_ms = requested_ms;
        bool fresh = job->has_data &&
                     requested_ms - job->updated_ms <= METER_READ_JOB_FRESH_MS;
        if (fresh) {
            memcpy(registers, job->registers,
                   (size_t)count * sizeof(registers[0]));
            result = ESP_OK;
        } else {
            job->pending = true;
            result = ESP_ERR_INVALID_STATE;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return result;
}
