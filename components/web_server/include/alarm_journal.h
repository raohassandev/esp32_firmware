#pragma once

/* Persistent alarm journal (gap A2 of docs/ALARM_MANAGEMENT_RESEARCH.md).
 *
 * The operator event ring in operational_api.c holds 96 records in RAM. That is
 * a buffer, not a history: it does not survive a reboot, so after an incident
 * there is nothing to hand to whoever asks what the plant did. This module is
 * the durable half - an append-only record of alarm lifecycle transitions that
 * outlives a restart.
 *
 * Three properties matter more than capacity here:
 *
 *  - It must never block the control path. Nothing in this module may be called
 *    from inside a critical section; callers stage records under their own lock
 *    and flush them afterwards.
 *  - It must survive corruption. Every record carries its own magic, version and
 *    CRC, and is validated on read. A record that does not parse is skipped and
 *    counted, never trusted and never fatal.
 *  - It must never damage a commissioned controller. The module mounts the
 *    existing storage partition read/write and will not format it, will not
 *    erase it, and will not truncate a file it did not understand. If the
 *    journal cannot be opened, the journal is simply unavailable and says so.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One record per lifecycle transition. These values are written to flash, so
 * they are part of the on-disk format: append new ones, never renumber. */
#define ALARM_JOURNAL_RAISED 0U
#define ALARM_JOURNAL_ACKNOWLEDGED 1U
#define ALARM_JOURNAL_CLEARED 2U
#define ALARM_JOURNAL_SHELVED 3U
#define ALARM_JOURNAL_UNSHELVED 4U
#define ALARM_JOURNAL_SHELF_EXPIRED 5U
#define ALARM_JOURNAL_TRANSITION_MAX 5U

/* 16 bytes on flash per record, encoded explicitly rather than by struct
 * layout so the format cannot drift with a compiler or an endianness change. */
#define ALARM_JOURNAL_RECORD_BYTES 16U

/* 16384 records = 256 kB inside the 1.9 MB storage partition. The remainder is
 * left free on purpose: SPIFFS needs spare blocks to garbage-collect, and a
 * filesystem run to capacity is a filesystem that fails when it is most needed.
 * At the handful of alarm transitions a healthy PV-DG site produces in a day
 * this is years of history, and the ring wraps oldest-first rather than
 * refusing to record anything new. */
#define ALARM_JOURNAL_CAPACITY 16384U

typedef struct {
    uint32_t sequence;   /* monotonic across reboots; recovered from flash */
    uint32_t uptime_ms;  /* milliseconds since controller start, NOT a date */
    uint16_t detail;     /* transition-specific: shelf duration in seconds */
    uint8_t code;        /* operational_event_code_t of the alarm */
    uint8_t transition;  /* ALARM_JOURNAL_* above */
} alarm_journal_entry_t;

/* Cheap: creates the lock only. Safe to call before the filesystem exists. */
void alarm_journal_init(void);

/* Mounts the storage partition, opens the journal and recovers the write
 * position by scanning it. Blocking, so it is called once from the operational
 * task rather than from HTTP registration. Never formats and never truncates. */
void alarm_journal_open(void);

bool alarm_journal_ready(void);
const char *alarm_journal_status(void);

uint32_t alarm_journal_stored(void);
uint32_t alarm_journal_next_sequence(void);
uint32_t alarm_journal_invalid_skipped(void);
uint32_t alarm_journal_write_failures(void);

/* Appends one record. Must be called with no lock held. */
bool alarm_journal_append(const alarm_journal_entry_t *entry);

/* Paged, newest first. Reads only the window asked for: the journal is far
 * larger than this controller's free heap and must never be serialized whole.
 * Returns the number of entries written to `out`; sets *out_more when older
 * valid records remain beyond the window. */
size_t alarm_journal_read_page(uint32_t offset, size_t limit,
                               alarm_journal_entry_t *out, bool *out_more);

const char *alarm_journal_transition_name(uint8_t transition);

#ifdef __cplusplus
}
#endif
