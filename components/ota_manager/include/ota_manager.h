#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANAGER_PRODUCT_ID "automatrix_pvdg"
#define OTA_MANAGER_PREFIX_BYTES \
    (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

typedef enum {
    OTA_MANAGER_IDLE = 0,
    OTA_MANAGER_RECEIVING,
    OTA_MANAGER_VALIDATING,
    OTA_MANAGER_READY_TO_REBOOT,
    OTA_MANAGER_FAILED,
} ota_manager_state_t;

typedef struct {
    ota_manager_state_t state;
    bool upload_active;
    bool pending_verify;
    bool rollback_enabled;
    bool update_staged;
    bool validation_scheduled;
    bool image_identity_verified;
    bool image_validated;
    bool boot_partition_preserved_after_abort;
    size_t image_size;
    size_t bytes_written;
    uint32_t started_ms;
    uint32_t completed_ms;
    esp_err_t last_error;
    char running_partition[17];
    char boot_partition[17];
    char update_partition[17];
    char running_project[33];
    char running_version[33];
    char candidate_project[33];
    char candidate_version[33];
    char candidate_idf_version[33];
    uint32_t candidate_secure_version;
} ota_manager_status_t;

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    const esp_partition_t *boot_partition_before;
    size_t expected_size;
    size_t written;
    bool active;
    bool identity_verified;
    bool image_validated;
    esp_app_desc_t candidate;
} ota_manager_session_t;

/* Initializes OTA runtime status. Does not modify NVS or OTA selection data. */
esp_err_t ota_manager_init(void);

/* Returns a lock-protected status snapshot. */
void ota_manager_get_status(ota_manager_status_t *out_status);

/* Validates image magic, exact product identity, ESP target chip ID, secure
 * version, and slot capacity before esp_ota_begin() can erase or write the
 * inactive OTA partition. */
esp_err_t ota_manager_validate_prefix(const uint8_t *prefix,
                                      size_t prefix_length,
                                      size_t complete_image_size,
                                      esp_app_desc_t *out_candidate);

/* Starts one streaming update into the inactive OTA slot. Only one session may
 * exist. The caller must abort or finish every successful begin. */
esp_err_t ota_manager_begin(size_t image_size,
                            const esp_app_desc_t *candidate,
                            ota_manager_session_t *out_session);

esp_err_t ota_manager_write(ota_manager_session_t *session,
                            const void *data,
                            size_t length);

/* Runs ESP-IDF whole-image validation, re-reads and matches the staged
 * descriptor, and only then switches the next boot partition. */
esp_err_t ota_manager_finish(ota_manager_session_t *session);

/* Aborts the active OTA handle, verifies that the original boot partition is
 * still selected, and releases the single-upload lock. */
void ota_manager_abort(ota_manager_session_t *session, esp_err_t reason);

/* Returns true when the running OTA image is awaiting first-boot validation. */
bool ota_manager_running_pending_verify(void);

/* Schedules validation only after the complete application initialized. A
 * pending image remains rollback-eligible throughout the stabilization delay. */
esp_err_t ota_manager_schedule_boot_validation(uint32_t stabilization_ms);

/* Marks a fully initialized first boot valid and cancels rollback. */
esp_err_t ota_manager_mark_running_valid(void);

/* Rolls back and restarts only when the running image is pending verification. */
esp_err_t ota_manager_rollback_pending_and_reboot(void);

const char *ota_manager_state_name(ota_manager_state_t state);

#ifdef __cplusplus
}
#endif
