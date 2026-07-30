#include "alarm_journal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "alarm_journal";

/* The storage partition is declared in partitions.csv at offset 0x620000,
 * length 0x1E0000 (1.9 MB), type data / subtype spiffs, and is otherwise
 * unused. It is deliberately the target: nvs is 24 kB and already carries the
 * configuration and the Engineering credential, and a high-rate history has no
 * business sharing a partition with the thing that authenticates the operator.
 *
 * The mount never formats an existing filesystem. It does provision an
 * unformatted partition once, which is a different act and is bounded as
 * narrowly as it can be:
 *
 *   - It runs only after a mount attempt has already failed, so a partition
 *     carrying a filesystem is never reformatted.
 *   - It is addressed by the "storage" label, and the partition found is
 *     checked to be type data / subtype spiffs before anything is written. nvs
 *     is subtype nvs and the application images are type app, so neither the
 *     configuration, the Wi-Fi credentials, the Engineering credential nor
 *     either OTA slot is reachable from here even if the label were wrong.
 *   - It is a partition-scoped format, never a chip erase.
 *   - It happens once. A mount that succeeds never formats, so a commissioned
 *     journal is not wiped on the next boot.
 *
 * This partition is not user data: nothing in the firmware has ever written to
 * it and the journal is its only consumer, so provisioning it destroys nothing.
 * If provisioning fails the journal still reports itself unavailable with the
 * reason and the rest of the controller carries on unchanged. */
#define ALARM_JOURNAL_PARTITION "storage"
/* Overridable only so the host contract test can exercise this file against a
 * temporary directory. The firmware never defines these. */
#ifndef ALARM_JOURNAL_BASE_PATH
#define ALARM_JOURNAL_BASE_PATH "/pvdg"
#endif
#ifndef ALARM_JOURNAL_FILE
#define ALARM_JOURNAL_FILE ALARM_JOURNAL_BASE_PATH "/alarm.jnl"
#endif

/* On-flash record, little-endian, encoded byte by byte:
 *   0      magic 'A'
 *   1      format version
 *   2      alarm code
 *   3      transition
 *   4..7   sequence
 *   8..11  uptime milliseconds
 *   12..13 detail
 *   14..15 CRC-16/CCITT over bytes 0..13
 * The magic and version cost two bytes and buy the ability to tell "never
 * written" and "written by a different firmware" apart from "corrupt". */
#define ALARM_JOURNAL_MAGIC 0x41U
#define ALARM_JOURNAL_VERSION 0x01U
#define ALARM_JOURNAL_CODE_MAX 31U

/* 32 slots = 512 bytes per read. The read path runs on the HTTP server task,
 * whose stack is 8 kB, so the scan window is deliberately small: a journal this
 * large must be walked in pieces on a controller whose minimum free internal
 * heap has been measured close to its own critical threshold. */
#define ALARM_JOURNAL_BLOCK_SLOTS 32U

#define ALARM_JOURNAL_LOCK_MS 2000U

static SemaphoreHandle_t s_lock;
static FILE *s_file;
static bool s_ready;
static const char *s_status = "not initialised";
static uint32_t s_head;            /* next slot to write */
static uint32_t s_stored;          /* valid records currently readable */
static uint32_t s_next_sequence = 1U;
static uint32_t s_invalid_skipped;
static uint32_t s_write_failures;
static bool s_provision_attempted;

/* Formats the storage partition, once, only after a mount has already failed.
 *
 * The label is checked against the partition table before anything is written:
 * the target must exist, be type DATA with subtype SPIFFS, and sit outside the
 * NVS and application partitions by construction of those subtypes. If the
 * lookup does not produce exactly that, nothing is formatted and the original
 * mount error is returned unchanged -- the journal then reports itself
 * unavailable, which is the correct outcome for an unexpected partition table.
 *
 * `mount_error` is passed in and returned on refusal so that the caller reports
 * why the mount failed rather than why provisioning was declined. */
static esp_err_t provision_partition_once(esp_err_t mount_error)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, ALARM_JOURNAL_PARTITION);
    if (!partition) {
        ESP_LOGW(TAG, "no data/spiffs partition labelled '%s'; not provisioning",
                 ALARM_JOURNAL_PARTITION);
        return mount_error;
    }
    /* Redundant given the typed lookup above, and kept deliberately: it is the
     * check a reader needs to see to believe that nvs and the OTA slots are out
     * of reach, and it costs nothing. */
    if (partition->type != ESP_PARTITION_TYPE_DATA ||
        partition->subtype != ESP_PARTITION_SUBTYPE_DATA_SPIFFS) {
        ESP_LOGE(TAG, "partition '%s' is not data/spiffs; refusing to format",
                 ALARM_JOURNAL_PARTITION);
        return mount_error;
    }

    ESP_LOGW(TAG, "storage partition at 0x%08" PRIx32 " (%" PRIu32 " bytes) has no filesystem; "
                  "provisioning it once for the alarm journal. Configuration, Wi-Fi credentials "
                  "and both application images are in other partitions and are untouched.",
             (uint32_t)partition->address, (uint32_t)partition->size);

    const esp_err_t formatted = esp_spiffs_format(ALARM_JOURNAL_PARTITION);
    if (formatted != ESP_OK) {
        ESP_LOGE(TAG, "provisioning the storage partition failed: %s", esp_err_to_name(formatted));
        return mount_error;
    }
    ESP_LOGI(TAG, "storage partition provisioned; the alarm journal is now durable");
    return ESP_OK;
}

static bool journal_take(void)
{
    if (!s_lock) return false;
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(ALARM_JOURNAL_LOCK_MS)) == pdTRUE;
}

static void journal_give(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* CRC-16/CCITT-FALSE. Deliberately table-free: 16 bytes per record makes the
 * bitwise loop irrelevant next to the flash write, and a 512-byte table on a
 * memory-constrained controller is not worth the microseconds. */
static uint16_t journal_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < length; ++index) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[index] << 8));
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (uint16_t)((crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                             : (uint16_t)(crc << 1));
        }
    }
    return crc;
}

static void journal_encode(uint8_t *bytes, const alarm_journal_entry_t *entry,
                           uint32_t sequence)
{
    bytes[0] = ALARM_JOURNAL_MAGIC;
    bytes[1] = ALARM_JOURNAL_VERSION;
    bytes[2] = entry->code;
    bytes[3] = entry->transition;
    bytes[4] = (uint8_t)(sequence & 0xFFU);
    bytes[5] = (uint8_t)((sequence >> 8) & 0xFFU);
    bytes[6] = (uint8_t)((sequence >> 16) & 0xFFU);
    bytes[7] = (uint8_t)((sequence >> 24) & 0xFFU);
    bytes[8] = (uint8_t)(entry->uptime_ms & 0xFFU);
    bytes[9] = (uint8_t)((entry->uptime_ms >> 8) & 0xFFU);
    bytes[10] = (uint8_t)((entry->uptime_ms >> 16) & 0xFFU);
    bytes[11] = (uint8_t)((entry->uptime_ms >> 24) & 0xFFU);
    bytes[12] = (uint8_t)(entry->detail & 0xFFU);
    bytes[13] = (uint8_t)((entry->detail >> 8) & 0xFFU);
    const uint16_t crc = journal_crc16(bytes, 14U);
    bytes[14] = (uint8_t)(crc & 0xFFU);
    bytes[15] = (uint8_t)((crc >> 8) & 0xFFU);
}

/* Nothing read from flash is trusted before it has passed every one of these.
 * A blank slot (0xFF) and a half-written slot both fail here, which is the
 * point: the journal degrades by losing records, never by handing an operator
 * a fabricated one and never by taking the boot down with it. */
static bool journal_decode(const uint8_t *bytes, alarm_journal_entry_t *entry)
{
    if (bytes[0] != ALARM_JOURNAL_MAGIC) return false;
    if (bytes[1] != ALARM_JOURNAL_VERSION) return false;
    if (bytes[2] > ALARM_JOURNAL_CODE_MAX) return false;
    if (bytes[3] > ALARM_JOURNAL_TRANSITION_MAX) return false;
    const uint16_t stored_crc = (uint16_t)((uint16_t)bytes[14] | ((uint16_t)bytes[15] << 8));
    if (stored_crc != journal_crc16(bytes, 14U)) return false;
    entry->code = bytes[2];
    entry->transition = bytes[3];
    entry->sequence = (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8) |
                      ((uint32_t)bytes[6] << 16) | ((uint32_t)bytes[7] << 24);
    entry->uptime_ms = (uint32_t)bytes[8] | ((uint32_t)bytes[9] << 8) |
                       ((uint32_t)bytes[10] << 16) | ((uint32_t)bytes[11] << 24);
    entry->detail = (uint16_t)((uint16_t)bytes[12] | ((uint16_t)bytes[13] << 8));
    /* Sequence 0 is never issued, so it marks a slot that was never a record. */
    if (entry->sequence == 0U) return false;
    return true;
}

static bool journal_read_block(uint32_t slot, uint32_t slots, uint8_t *buffer)
{
    memset(buffer, 0, (size_t)slots * ALARM_JOURNAL_RECORD_BYTES);
    if (fseek(s_file, (long)slot * (long)ALARM_JOURNAL_RECORD_BYTES, SEEK_SET) != 0) {
        return false;
    }
    /* A short read is expected while the ring is still growing: the slots past
     * the end of the file simply do not exist yet, and because the buffer was
     * zeroed above, those slots fail validation exactly as an unwritten slot
     * should. That case is not an error and must not be reported as one.
     *
     * A read ERROR is a different thing entirely and was previously
     * indistinguishable from it. The return value was discarded with a (void)
     * cast, so a failing filesystem returned a block of zeroes that the caller
     * accepted as "these slots were never written" -- silently converting an I/O
     * fault into missing history, which is the one outcome a journal exists to
     * prevent. ferror() separates them: end-of-file is growth, anything else is a
     * failure the caller must see.
     *
     * (The (void) cast did not even suppress the warning it was written for:
     * glibc marks fread warn_unused_result, and casting to void does not silence
     * that attribute, so this broke the -Werror build on Linux while compiling
     * clean on MinGW.) */
    const size_t wanted = (size_t)slots * ALARM_JOURNAL_RECORD_BYTES;
    const size_t got = fread(buffer, 1U, wanted, s_file);
    if (got < wanted && ferror(s_file)) {
        clearerr(s_file);
        return false;
    }
    return true;
}

/* Recovers the write position without a header block. There is no superblock to
 * lose: the newest record is whichever valid record carries the highest
 * sequence, and the next write goes after it. A torn header would otherwise be
 * a single point of failure for the whole history. */
static void journal_scan(void)
{
    uint8_t buffer[ALARM_JOURNAL_BLOCK_SLOTS * ALARM_JOURNAL_RECORD_BYTES];
    uint32_t newest_sequence = 0U;
    uint32_t newest_slot = 0U;
    uint32_t valid = 0U;
    uint32_t invalid = 0U;
    bool found = false;

    for (uint32_t slot = 0U; slot < ALARM_JOURNAL_CAPACITY; slot += ALARM_JOURNAL_BLOCK_SLOTS) {
        uint32_t slots = ALARM_JOURNAL_CAPACITY - slot;
        if (slots > ALARM_JOURNAL_BLOCK_SLOTS) slots = ALARM_JOURNAL_BLOCK_SLOTS;
        if (!journal_read_block(slot, slots, buffer)) break;
        for (uint32_t index = 0U; index < slots; ++index) {
            alarm_journal_entry_t entry;
            const uint8_t *record = &buffer[index * ALARM_JOURNAL_RECORD_BYTES];
            bool blank = true;
            for (uint32_t byte = 0U; byte < ALARM_JOURNAL_RECORD_BYTES; ++byte) {
                if (record[byte] != 0x00U && record[byte] != 0xFFU) {
                    blank = false;
                    break;
                }
            }
            if (journal_decode(record, &entry)) {
                valid++;
                /* Signed difference so a sequence that has wrapped past 2^32
                 * still orders correctly rather than resetting the journal. */
                if (!found || (int32_t)(entry.sequence - newest_sequence) > 0) {
                    newest_sequence = entry.sequence;
                    newest_slot = slot + index;
                    found = true;
                }
            } else if (!blank) {
                invalid++;
            }
        }
    }

    s_stored = valid;
    s_invalid_skipped = invalid;
    if (found) {
        s_head = (newest_slot + 1U) % ALARM_JOURNAL_CAPACITY;
        s_next_sequence = newest_sequence + 1U;
        if (s_next_sequence == 0U) s_next_sequence = 1U;
    } else {
        /* Nothing readable. Start fresh in place: write from slot 0 onwards and
         * let each write replace one slot. No format, no truncate, no erase -
         * whatever else is on that partition is none of this module's business.
         */
        s_head = 0U;
        s_next_sequence = 1U;
    }
}

void alarm_journal_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) s_status = "journal lock unavailable";
    else if (!s_ready) s_status = "not opened";
}

void alarm_journal_open(void)
{
    if (!journal_take()) return;
    if (s_ready) {
        journal_give();
        return;
    }

    if (!esp_spiffs_mounted(ALARM_JOURNAL_PARTITION)) {
        const esp_vfs_spiffs_conf_t conf = {
            .base_path = ALARM_JOURNAL_BASE_PATH,
            .partition_label = ALARM_JOURNAL_PARTITION,
            .max_files = 2,
            /* Never true: an existing filesystem must not be reformatted just
             * because a mount failed for some other reason. Provisioning an
             * unformatted partition is handled explicitly below, so that the
             * one case where formatting is correct is the only case that can
             * reach a format call. */
            .format_if_mount_failed = false,
        };
        esp_err_t mounted = esp_vfs_spiffs_register(&conf);
        if (mounted != ESP_OK && !s_provision_attempted) {
            /* First boot on a partition the firmware has never used. Provision
             * it once, having first confirmed that the partition really is the
             * unused spiffs data area and not something that matters. */
            s_provision_attempted = true;
            mounted = provision_partition_once(mounted);
            if (mounted == ESP_OK) mounted = esp_vfs_spiffs_register(&conf);
        }
        if (mounted != ESP_OK) {
            s_status = "storage partition not mountable; journal unavailable";
            ESP_LOGW(TAG, "%s: %s", s_status, esp_err_to_name(mounted));
            journal_give();
            return;
        }
    }

    /* r+b keeps an existing journal intact. w+b is used only when the file does
     * not exist at all, so an unreadable journal is never truncated on the way
     * to being "fixed". */
    s_file = fopen(ALARM_JOURNAL_FILE, "r+b");
    if (!s_file) s_file = fopen(ALARM_JOURNAL_FILE, "w+b");
    if (!s_file) {
        s_status = "journal file could not be opened; journal unavailable";
        ESP_LOGW(TAG, "%s", s_status);
        journal_give();
        return;
    }

    journal_scan();
    s_ready = true;
    s_status = "ready";
    journal_give();
    ESP_LOGI(TAG, "alarm journal ready: %u stored, %u unreadable skipped, next sequence %u",
             (unsigned)s_stored, (unsigned)s_invalid_skipped, (unsigned)s_next_sequence);
}

bool alarm_journal_ready(void)
{
    return s_ready;
}

const char *alarm_journal_status(void)
{
    return s_status;
}

uint32_t alarm_journal_stored(void)
{
    return s_stored;
}

uint32_t alarm_journal_next_sequence(void)
{
    return s_next_sequence;
}

uint32_t alarm_journal_invalid_skipped(void)
{
    return s_invalid_skipped;
}

uint32_t alarm_journal_write_failures(void)
{
    return s_write_failures;
}

bool alarm_journal_append(const alarm_journal_entry_t *entry)
{
    if (!entry || !s_ready) return false;
    if (!journal_take()) return false;

    uint8_t bytes[ALARM_JOURNAL_RECORD_BYTES];
    journal_encode(bytes, entry, s_next_sequence);

    bool written = false;
    if (fseek(s_file, (long)s_head * (long)ALARM_JOURNAL_RECORD_BYTES, SEEK_SET) == 0 &&
        fwrite(bytes, 1U, sizeof(bytes), s_file) == sizeof(bytes)) {
        /* Flushed and synced on every record. A journal that is still in a
         * buffer when the controller resets is exactly the history that was
         * missing before this module existed. */
        (void)fflush(s_file);
        (void)fsync(fileno(s_file));
        s_next_sequence++;
        if (s_next_sequence == 0U) s_next_sequence = 1U;
        /* Wrap oldest-first: the ring keeps recording rather than refusing new
         * records once full. Recent history is what an incident needs. */
        s_head = (s_head + 1U) % ALARM_JOURNAL_CAPACITY;
        if (s_stored < ALARM_JOURNAL_CAPACITY) s_stored++;
        written = true;
    } else {
        s_write_failures++;
    }

    journal_give();
    return written;
}

size_t alarm_journal_read_page(uint32_t offset, size_t limit,
                               alarm_journal_entry_t *out, bool *out_more)
{
    if (out_more) *out_more = false;
    if (!out || limit == 0U || !s_ready) return 0U;
    if (!journal_take()) return 0U;

    uint8_t buffer[ALARM_JOURNAL_BLOCK_SLOTS * ALARM_JOURNAL_RECORD_BYTES];
    uint32_t remaining = ALARM_JOURNAL_CAPACITY;
    uint32_t end = s_head;              /* exclusive upper bound, newest side */
    uint32_t skipped = 0U;
    size_t produced = 0U;
    bool more = false;

    /* Walks backwards from the newest record. Chunks never straddle the ring
     * wrap, so every read is one contiguous window of the file. */
    while (remaining > 0U) {
        if (end == 0U) end = ALARM_JOURNAL_CAPACITY;
        uint32_t chunk = ALARM_JOURNAL_BLOCK_SLOTS;
        if (chunk > remaining) chunk = remaining;
        if (chunk > end) chunk = end;
        const uint32_t start = end - chunk;
        if (!journal_read_block(start, chunk, buffer)) break;

        for (uint32_t step = chunk; step > 0U; --step) {
            const uint8_t *record = &buffer[(step - 1U) * ALARM_JOURNAL_RECORD_BYTES];
            alarm_journal_entry_t entry;
            if (!journal_decode(record, &entry)) continue;   /* corrupt or blank */
            if (skipped < offset) {
                skipped++;
                continue;
            }
            if (produced >= limit) {
                more = true;
                break;
            }
            out[produced++] = entry;
        }

        if (more) break;
        remaining -= chunk;
        end = start;
        /* Every record the journal holds has already been seen, so there is
         * nothing left to find: stop rather than walking the rest of the ring. */
        if ((uint32_t)produced + skipped >= s_stored) break;
    }

    journal_give();
    if (out_more) *out_more = more;
    return produced;
}

const char *alarm_journal_transition_name(uint8_t transition)
{
    switch (transition) {
    case ALARM_JOURNAL_RAISED:        return "raised";
    case ALARM_JOURNAL_ACKNOWLEDGED:  return "acknowledged";
    case ALARM_JOURNAL_CLEARED:       return "cleared";
    case ALARM_JOURNAL_SHELVED:       return "shelved";
    case ALARM_JOURNAL_UNSHELVED:     return "unshelved";
    case ALARM_JOURNAL_SHELF_EXPIRED: return "shelf_expired";
    /* A9. Named apart from shelving so the audit trail can still answer who
     * decided, months later. */
    case ALARM_JOURNAL_DESIGN_SUPPRESSED:   return "design_suppressed";
    case ALARM_JOURNAL_DESIGN_RELEASED:     return "design_released";
    case ALARM_JOURNAL_OUT_OF_SERVICE:      return "out_of_service";
    case ALARM_JOURNAL_RETURNED_TO_SERVICE: return "returned_to_service";
    default:                          return "unknown";
    }
}
