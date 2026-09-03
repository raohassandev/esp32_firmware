#include "ota_manager.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "ota_manager";
static ota_manager_status_t s_status;
static bool s_upload_claimed;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void copy_text(char *destination, size_t destination_size,
                      const char *source, size_t source_size)
{
    if (!destination || destination_size == 0U) return;
    destination[0] = '\0';
    if (!source || source_size == 0U) return;
    size_t length = strnlen(source, source_size);
    if (length >= destination_size) length = destination_size - 1U;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool fixed_text_equals(const char *field, size_t field_size,
                              const char *expected)
{
    if (!field || field_size == 0U || !expected) return false;
    const size_t expected_length = strlen(expected);
    if (expected_length >= field_size) return false;
    const size_t field_length = strnlen(field, field_size);
    return field_length == expected_length &&
           memcmp(field, expected, expected_length) == 0;
}

static bool descriptor_matches_candidate(const esp_app_desc_t *written,
                                         const esp_app_desc_t *candidate)
{
    if (!written || !candidate) return false;
    return written->magic_word == ESP_APP_DESC_MAGIC_WORD &&
           written->secure_version == candidate->secure_version &&
           memcmp(written->project_name, candidate->project_name,
                  sizeof(written->project_name)) == 0 &&
           memcmp(written->version, candidate->version,
                  sizeof(written->version)) == 0 &&
           memcmp(written->app_elf_sha256, candidate->app_elf_sha256,
                  sizeof(written->app_elf_sha256)) == 0;
}

static bool image_identity_valid(const esp_image_header_t *image_header,
                                 const esp_app_desc_t *candidate,
                                 const esp_app_desc_t *running)
{
    if (!image_header || !candidate || !running) return false;
    return image_header->magic == ESP_IMAGE_HEADER_MAGIC &&
           image_header->chip_id == (esp_chip_id_t)CONFIG_IDF_FIRMWARE_CHIP_ID &&
           candidate->magic_word == ESP_APP_DESC_MAGIC_WORD &&
           fixed_text_equals(candidate->project_name,
                             sizeof(candidate->project_name),
                             OTA_MANAGER_PRODUCT_ID) &&
           fixed_text_equals(running->project_name,
                             sizeof(running->project_name),
                             OTA_MANAGER_PRODUCT_ID) &&
           candidate->secure_version >= running->secure_version;
}

static bool pending_verify_for(const esp_partition_t *partition)
{
    if (!partition) return false;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return esp_ota_get_state_partition(partition, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

static bool upload_admission_open(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    return running && boot && !pending_verify_for(running) &&
           running->address == boot->address;
#else
    return false;
#endif
}

/* Partition and OTA-state APIs may touch flash metadata. Populate a local
 * snapshot outside the short interrupt-disabled status lock. */
static void fill_partition_status(ota_manager_status_t *status)
{
    if (!status) return;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *description = esp_app_get_description();

    copy_text(status->running_partition, sizeof(status->running_partition),
              running ? running->label : "", running ? sizeof(running->label) : 0U);
    copy_text(status->boot_partition, sizeof(status->boot_partition),
              boot ? boot->label : "", boot ? sizeof(boot->label) : 0U);
    copy_text(status->update_partition, sizeof(status->update_partition),
              update ? update->label : "", update ? sizeof(update->label) : 0U);
    if (description) {
        copy_text(status->running_project, sizeof(status->running_project),
                  description->project_name, sizeof(description->project_name));
        copy_text(status->running_version, sizeof(status->running_version),
                  description->version, sizeof(description->version));
    }
    status->pending_verify = pending_verify_for(running);
    status->update_staged = running && boot && running->address != boot->address;
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    status->rollback_enabled = true;
#else
    status->rollback_enabled = false;
#endif
}

static void set_failure(esp_err_t error)
{
    const uint32_t completed_ms = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_status.state = OTA_MANAGER_FAILED;
    s_status.upload_active = false;
    s_status.last_error = error;
    s_status.completed_ms = completed_ms;
    s_upload_claimed = false;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t ota_manager_init(void)
{
    ota_manager_status_t initial = {0};
    initial.state = OTA_MANAGER_IDLE;
    initial.last_error = ESP_OK;
    fill_partition_status(&initial);

    portENTER_CRITICAL(&s_lock);
    s_status = initial;
    s_upload_claimed = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "OTA slots ready: running=%s boot=%s next=%s rollback=%s pending_verify=%s",
             initial.running_partition,
             initial.boot_partition,
             initial.update_partition,
             initial.rollback_enabled ? "enabled" : "disabled",
             initial.pending_verify ? "yes" : "no");
    return ESP_OK;
}

void ota_manager_get_status(ota_manager_status_t *out_status)
{
    if (!out_status) return;
    ota_manager_status_t snapshot;
    portENTER_CRITICAL(&s_lock);
    snapshot = s_status;
    portEXIT_CRITICAL(&s_lock);
    fill_partition_status(&snapshot);
    *out_status = snapshot;
}

esp_err_t ota_manager_validate_prefix(const uint8_t *prefix,
                                      size_t prefix_length,
                                      size_t complete_image_size,
                                      esp_app_desc_t *out_candidate)
{
    if (!prefix || !out_candidate ||
        prefix_length < OTA_MANAGER_PREFIX_BYTES ||
        complete_image_size < OTA_MANAGER_PREFIX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_image_header_t *image_header = (const esp_image_header_t *)prefix;
    const size_t description_offset =
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    esp_app_desc_t candidate = {0};
    memcpy(&candidate, prefix + description_offset, sizeof(candidate));

    const esp_app_desc_t *running = esp_app_get_description();
    if (!image_identity_valid(image_header, &candidate, running)) {
        portENTER_CRITICAL(&s_lock);
        s_status.image_identity_verified = false;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition || complete_image_size > partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    portENTER_CRITICAL(&s_lock);
    s_status.image_identity_verified = true;
    s_status.image_validated = false;
    s_status.boot_partition_preserved_after_abort = false;
    portEXIT_CRITICAL(&s_lock);

    *out_candidate = candidate;
    return ESP_OK;
}

esp_err_t ota_manager_begin(size_t image_size,
                            const esp_image_header_t *image_header,
                            const esp_app_desc_t *candidate,
                            ota_manager_session_t *out_session)
{
    if (!image_header || !candidate || !out_session ||
        image_size < OTA_MANAGER_PREFIX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_app_desc_t *running_description = esp_app_get_description();
    if (!image_identity_valid(image_header, candidate, running_description)) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    /* Never erase the alternate slot while it is staged as the next boot image,
     * and never accept another image while this running image still needs
     * first-boot validation. Rollback support is mandatory for this feature. */
    if (!upload_admission_open()) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&s_lock);
    if (s_upload_claimed) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_upload_claimed = true;
    portEXIT_CRITICAL(&s_lock);

    memset(out_session, 0, sizeof(*out_session));
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *boot_before = esp_ota_get_boot_partition();
    if (!partition || !boot_before || image_size > partition->size) {
        set_failure(ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Identity and target checks above must complete before this call because
     * esp_ota_begin() may erase the inactive OTA partition. */
    esp_ota_handle_t handle = 0;
    esp_err_t error = esp_ota_begin(partition, image_size, &handle);
    if (error != ESP_OK) {
        set_failure(error);
        return error;
    }

    out_session->handle = handle;
    out_session->partition = partition;
    out_session->boot_partition_before = boot_before;
    out_session->expected_size = image_size;
    out_session->active = true;
    out_session->identity_verified = true;
    out_session->image_validated = false;
    out_session->candidate = *candidate;

    const uint32_t started_ms = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_status.state = OTA_MANAGER_RECEIVING;
    s_status.upload_active = true;
    s_status.update_staged = false;
    s_status.image_identity_verified = true;
    s_status.image_validated = false;
    s_status.boot_partition_preserved_after_abort = false;
    s_status.image_size = image_size;
    s_status.bytes_written = 0U;
    s_status.started_ms = started_ms;
    s_status.completed_ms = 0U;
    s_status.last_error = ESP_OK;
    copy_text(s_status.update_partition, sizeof(s_status.update_partition),
              partition->label, sizeof(partition->label));
    copy_text(s_status.candidate_project, sizeof(s_status.candidate_project),
              candidate->project_name, sizeof(candidate->project_name));
    copy_text(s_status.candidate_version, sizeof(s_status.candidate_version),
              candidate->version, sizeof(candidate->version));
    copy_text(s_status.candidate_idf_version, sizeof(s_status.candidate_idf_version),
              candidate->idf_ver, sizeof(candidate->idf_ver));
    s_status.candidate_secure_version = candidate->secure_version;
    portEXIT_CRITICAL(&s_lock);

    char project[sizeof(s_status.candidate_project)];
    char version[sizeof(s_status.candidate_version)];
    copy_text(project, sizeof(project), candidate->project_name,
              sizeof(candidate->project_name));
    copy_text(version, sizeof(version), candidate->version,
              sizeof(candidate->version));
    ESP_LOGI(TAG, "Starting OTA upload to %s: %u bytes, project=%s version=%s target=%u",
             partition->label, (unsigned)image_size, project, version,
             (unsigned)image_header->chip_id);
    return ESP_OK;
}

esp_err_t ota_manager_write(ota_manager_session_t *session,
                            const void *data,
                            size_t length)
{
    if (!session || !session->active || !session->identity_verified ||
        session->image_validated || !data || length == 0U ||
        session->written > session->expected_size ||
        length > session->expected_size - session->written) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = esp_ota_write(session->handle, data, length);
    if (error != ESP_OK) {
        ota_manager_abort(session, error);
        return error;
    }

    session->written += length;
    portENTER_CRITICAL(&s_lock);
    s_status.bytes_written = session->written;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t ota_manager_finish(ota_manager_session_t *session)
{
    if (!session || !session->active || !session->identity_verified ||
        session->written != session->expected_size) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_lock);
    s_status.state = OTA_MANAGER_VALIDATING;
    portEXIT_CRITICAL(&s_lock);

    /* esp_ota_end() performs ESP-IDF whole-image verification. The boot
     * partition must not change unless this returns ESP_OK. */
    esp_err_t error = esp_ota_end(session->handle);
    session->active = false;
    if (error != ESP_OK) {
        set_failure(error);
        return error;
    }

    esp_app_desc_t written_candidate = {0};
    error = esp_ota_get_partition_description(session->partition,
                                              &written_candidate);
    if (error != ESP_OK ||
        !descriptor_matches_candidate(&written_candidate,
                                      &session->candidate)) {
        if (error == ESP_OK) error = ESP_ERR_OTA_VALIDATE_FAILED;
        set_failure(error);
        return error;
    }

    const esp_partition_t *boot_before_switch = esp_ota_get_boot_partition();
    if (!boot_before_switch || !session->boot_partition_before ||
        boot_before_switch->address != session->boot_partition_before->address) {
        set_failure(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    session->image_validated = true;
    portENTER_CRITICAL(&s_lock);
    s_status.image_validated = true;
    portEXIT_CRITICAL(&s_lock);

    if (!session->image_validated) {
        set_failure(ESP_ERR_OTA_VALIDATE_FAILED);
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }

    /* This is the only boot-selection change in the upload path and it occurs
     * after esp_ota_end() plus descriptor identity verification. */
    error = esp_ota_set_boot_partition(session->partition);
    if (error != ESP_OK) {
        set_failure(error);
        return error;
    }

    const uint32_t completed_ms = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_status.state = OTA_MANAGER_READY_TO_REBOOT;
    s_status.upload_active = false;
    s_status.update_staged = true;
    s_status.bytes_written = session->written;
    s_status.completed_ms = completed_ms;
    s_status.last_error = ESP_OK;
    s_upload_claimed = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "OTA image fully validated and staged in %s; reboot required",
             session->partition->label);
    return ESP_OK;
}

void ota_manager_abort(ota_manager_session_t *session, esp_err_t reason)
{
    const esp_partition_t *boot_before =
        session ? session->boot_partition_before : NULL;
    if (session && session->active) {
        esp_ota_abort(session->handle);
        session->active = false;
    }

    const esp_partition_t *boot_after = esp_ota_get_boot_partition();
    const bool boot_preserved = boot_before && boot_after &&
                                boot_before->address == boot_after->address;
    portENTER_CRITICAL(&s_lock);
    s_status.image_validated = false;
    s_status.boot_partition_preserved_after_abort = boot_preserved;
    portEXIT_CRITICAL(&s_lock);

    if (!boot_preserved) {
        ESP_LOGE(TAG, "Interrupted OTA did not preserve the original boot partition");
        reason = ESP_ERR_INVALID_STATE;
    } else {
        ESP_LOGW(TAG, "OTA upload aborted after %u bytes; boot partition remains %s",
                 session ? (unsigned)session->written : 0U,
                 boot_after->label);
    }
    set_failure(reason == ESP_OK ? ESP_FAIL : reason);
}

bool ota_manager_running_pending_verify(void)
{
    return pending_verify_for(esp_ota_get_running_partition());
}

esp_err_t ota_manager_mark_running_valid(void)
{
    if (!ota_manager_running_pending_verify()) return ESP_OK;
    esp_err_t error = esp_ota_mark_app_valid_cancel_rollback();
    if (error == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_status.pending_verify = false;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "First-boot diagnostics passed; running image marked valid");
    }
    return error;
}

static void boot_validation_task(void *argument)
{
    uint32_t stabilization_ms = (uint32_t)(uintptr_t)argument;
    vTaskDelay(pdMS_TO_TICKS(stabilization_ms));
    esp_err_t error = ota_manager_mark_running_valid();
    portENTER_CRITICAL(&s_lock);
    s_status.validation_scheduled = false;
    if (error != ESP_OK) s_status.last_error = error;
    portEXIT_CRITICAL(&s_lock);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to validate the first OTA boot: %s",
                 esp_err_to_name(error));
    }
    vTaskDelete(NULL);
}

esp_err_t ota_manager_schedule_boot_validation(uint32_t stabilization_ms)
{
    if (!ota_manager_running_pending_verify()) return ESP_OK;
    if (stabilization_ms < 5000U) stabilization_ms = 5000U;
    if (stabilization_ms > 120000U) stabilization_ms = 120000U;

    portENTER_CRITICAL(&s_lock);
    if (s_status.validation_scheduled) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    s_status.validation_scheduled = true;
    portEXIT_CRITICAL(&s_lock);

    BaseType_t created = xTaskCreate(boot_validation_task,
                                     "ota_validate",
                                     4096,
                                     (void *)(uintptr_t)stabilization_ms,
                                     6,
                                     NULL);
    if (created != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_status.validation_scheduled = false;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "First OTA boot will remain rollback-eligible for %u ms",
             (unsigned)stabilization_ms);
    return ESP_OK;
}

esp_err_t ota_manager_rollback_pending_and_reboot(void)
{
    if (!ota_manager_running_pending_verify()) return ESP_ERR_INVALID_STATE;
    ESP_LOGE(TAG, "First-boot diagnostics failed; rolling back to previous image");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

const char *ota_manager_state_name(ota_manager_state_t state)
{
    switch (state) {
    case OTA_MANAGER_IDLE: return "idle";
    case OTA_MANAGER_RECEIVING: return "receiving";
    case OTA_MANAGER_VALIDATING: return "validating";
    case OTA_MANAGER_READY_TO_REBOOT: return "ready_to_reboot";
    case OTA_MANAGER_FAILED: return "failed";
    default: return "unknown";
    }
}
