"""Alarm journal and shelving contract: ISA-18.2 gaps A2 and A3.

Two halves.

The first is a source contract over components/web_server/operational_api.c and
components/web_server/alarm_journal.c. It cannot prove the firmware behaves on
hardware. It proves that the mechanisms the standards require are present, that
the properties which make them safe are structural rather than incidental, and
that the failure modes this module has already paid for are not reintroduced:
writing flash with interrupts disabled, serialising a history far larger than
the free heap in one response, suppressing an alarm without an audit trail, and
erasing a commissioned controller's flash.

The second half actually compiles alarm_journal.c with gcc against small stubs
and exercises it: append, reopen, paging, oldest-first wrap, and deliberate
corruption. Record validation is the one thing here that cannot be proved by
reading the source, because the whole question is what happens to bytes nobody
intended to write.
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API_PATH = ROOT / "components/web_server/operational_api.c"
JOURNAL_PATH = ROOT / "components/web_server/alarm_journal.c"
JOURNAL_HEADER = ROOT / "components/web_server/include/alarm_journal.h"
API = API_PATH.read_text(encoding="utf-8")
JOURNAL = JOURNAL_PATH.read_text(encoding="utf-8")
HEADER = JOURNAL_HEADER.read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")
PARTITIONS = (ROOT / "partitions.csv").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    match = re.search(r"^[\w \*]*\b%s\s*\([^;]*?\)\s*\{" % re.escape(name), source, re.M | re.S)
    require(match is not None, f"function {name} not found")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"function {name} is not brace balanced")


def strip_comments(source: str) -> str:
    """Comments explain; code decides. Only code is checked for behaviour."""
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", source, flags=re.S))


def critical_spans(source: str) -> list:
    spans = []
    cursor = 0
    while True:
        start = source.find("portENTER_CRITICAL", cursor)
        if start < 0:
            return spans
        end = source.find("portEXIT_CRITICAL", start)
        require(end > start, "unbalanced critical section")
        spans.append(source[start:end])
        cursor = end + 1


# ---------------------------------------------------------------------------
# A2: the journal is where the decision said it would be
# ---------------------------------------------------------------------------

partition = [line for line in PARTITIONS.splitlines() if line.strip().startswith("storage")]
require(len(partition) == 1, "partitions.csv no longer declares a single storage partition")
fields = [field.strip() for field in partition[0].split(",")]
require(fields[1] == "data", "the storage partition must be a data partition")
require(fields[3].lower() == "0x620000",
        f"storage partition offset moved to {fields[3]}; the journal targets 0x620000")
require(fields[4].lower() == "0x1e0000",
        f"storage partition length changed to {fields[4]}; the journal assumes 0x1E0000")
require('ALARM_JOURNAL_PARTITION "storage"' in JOURNAL,
        "the journal must use the existing storage partition, not a new one")
require("nvs_" not in JOURNAL,
        "the journal must not share the partition that holds the Engineering credential")

# ---------------------------------------------------------------------------
# NEVER erase or reformat. A commissioned controller is in the field.
# ---------------------------------------------------------------------------

FORBIDDEN_DESTRUCTION = [
    "esp_partition_erase_range",
    "esp_partition_erase",
    "esp_flash_erase_region",
    "esp_flash_erase_chip",
    "spi_flash_erase_sector",
    "spi_flash_erase_range",
    "esp_vfs_fat_spiflash_format",
]

# esp_spiffs_format is permitted in exactly one place, for exactly one purpose:
# provisioning the storage partition the first time, because nothing in this
# firmware has ever written to it and the journal cannot exist without a
# filesystem. It stays banned everywhere else, and the guards that keep it
# narrow are asserted below rather than trusted.
FORMAT_ALLOWED_IN = "components/web_server/alarm_journal.c"
formatters = sorted(
    path.relative_to(ROOT).as_posix() for path in
    list((ROOT / "components").rglob("*.c")) + list((ROOT / "main").rglob("*.c"))
    if "managed_components" not in str(path)
    and "esp_spiffs_format" in path.read_text(encoding="utf-8", errors="ignore")
)
require(formatters == [FORMAT_ALLOWED_IN],
        f"esp_spiffs_format appeared outside the journal provisioning path: {formatters}")

# The mount itself must never be allowed to format, so that only the explicitly
# guarded provisioning path can reach a format call.
require("format_if_mount_failed = false" in JOURNAL,
        "the journal mount must never format: provisioning is explicit and guarded")
# Provisioning happens at most once, so a mountable journal is never wiped.
require("s_provision_attempted" in JOURNAL,
        "provisioning must be latched so it cannot repeat on every open")
# The partition is verified to be the unused spiffs data area before any write,
# which is what keeps nvs and the two application images out of reach.
require("ESP_PARTITION_TYPE_DATA" in JOURNAL and "ESP_PARTITION_SUBTYPE_DATA_SPIFFS" in JOURNAL,
        "provisioning must confirm the target is a data/spiffs partition before formatting")
require("refusing to format" in JOURNAL,
        "provisioning must refuse, not proceed, when the partition is not what it expects")
# Provisioning may only run after a mount has already failed. Asserted on the
# guard at the call site, not on where the function happens to be defined:
# source order is not execution order, and a position-based assertion would
# break the moment someone moved the helper without changing any behaviour.
call = re.search(r"if\s*\(([^)]*)\)\s*\{\s*[^}]*provision_partition_once", JOURNAL, re.DOTALL)
require(call is not None, "provision_partition_once must be called under an explicit guard")
if call is not None:
    guard = call.group(1)
    require("mounted != ESP_OK" in guard,
            f"provisioning must be guarded by a failed mount, but the guard is: {guard.strip()}")
    require("s_provision_attempted" in guard,
            f"provisioning must be guarded by the once-only latch, but the guard is: {guard.strip()}")
# The only format call in the file must live inside that helper, so it cannot be
# reached by any path that skips the guard.
helper = JOURNAL[JOURNAL.index("static esp_err_t provision_partition_once"):]
helper = helper[:helper.index("\nstatic bool journal_take")]
require(helper.count("esp_spiffs_format") == JOURNAL.count("esp_spiffs_format"),
        "every esp_spiffs_format call must sit inside the guarded provisioning helper")
# Additionally forbidden anywhere the journal work reaches: destroying the file
# is the same mistake as destroying the partition, one level up.
FORBIDDEN_IN_WEB_SERVER = FORBIDDEN_DESTRUCTION + [
    "nvs_flash_erase", "ftruncate", "remove(", "unlink(",
]

firmware_sources = [
    path for path in
    list((ROOT / "components").rglob("*.c")) + list((ROOT / "components").rglob("*.h")) +
    list((ROOT / "main").rglob("*.c")) + list((ROOT / "main").rglob("*.h"))
    if "managed_components" not in str(path)
]
require(firmware_sources, "no firmware sources were scanned")
for path in firmware_sources:
    text = path.read_text(encoding="utf-8", errors="ignore")
    forbidden = (FORBIDDEN_IN_WEB_SERVER if "web_server" in str(path) else FORBIDDEN_DESTRUCTION)
    for token in forbidden:
        require(token not in text,
                f"{path.relative_to(ROOT)} calls {token}: a commissioned controller's flash "
                "must never be erased, formatted or truncated to make a diagnostic work")

# One erase exists in this firmware and it predates this work: the standard NVS
# recovery in config_manager, which runs only when NVS itself is unusable and
# touches nothing else. It is pinned here so a second erase cannot appear
# somewhere quieter and claim the same excuse.
erasers = [path.relative_to(ROOT).as_posix() for path in firmware_sources
           if "nvs_flash_erase" in path.read_text(encoding="utf-8", errors="ignore")]
require(erasers == ["components/config_manager/config_manager.c"],
        f"flash erase appeared outside the pre-existing NVS recovery path: {erasers}")

require("format_if_mount_failed = false" in JOURNAL,
        "the journal must refuse to format the storage partition; an unformatted "
        "partition is a missing feature, a formatted one is lost commissioning")
require("format_if_mount_failed = true" not in JOURNAL,
        "formatting on mount failure would erase a field controller's storage")
require('fopen(ALARM_JOURNAL_FILE, "r+b")' in JOURNAL,
        "an existing journal must be opened without truncating it")
require('"w+b"' in JOURNAL and JOURNAL.index('"r+b"') < JOURNAL.index('"w+b"'),
        "the truncating open mode must only be reached after r+b failed, i.e. when "
        "the file does not exist at all")

# "Start fresh" on an unreadable journal means resuming at slot 0, not wiping.
scan = function_body(JOURNAL, "journal_scan")
require("s_head = 0U" in scan and "s_next_sequence = 1U" in scan,
        "an unreadable journal must restart in place at slot 0")
for token in FORBIDDEN_DESTRUCTION:
    require(token not in scan, f"the recovery path must not call {token}")

# ---------------------------------------------------------------------------
# A2: no journal write inside a critical section
# ---------------------------------------------------------------------------

require("portENTER_CRITICAL" not in JOURNAL,
        "alarm_journal.c must not take the alarm spinlock: it does file I/O")

api_spans = critical_spans(API)
require(api_spans, "the alarm module no longer takes its lock")
for span in api_spans:
    for forbidden in ["alarm_journal_append", "alarm_journal_read_page", "alarm_journal_open",
                      "journal_flush", "fopen", "fwrite", "fsync", "fflush",
                      "cJSON_", "malloc(", "calloc(", "free(", "ESP_LOG"]:
        require(forbidden not in span,
                f"{forbidden} inside a critical section: interrupts are disabled there and "
                "the control loop must never wait on flash")

# Transitions are collected under the lock and written outside it.
stage = function_body(API, "journal_stage_locked")
for forbidden in ["alarm_journal_append", "fwrite", "malloc(", "calloc(", "ESP_LOG"]:
    require(forbidden not in stage, f"the staging step must not {forbidden}")
require("s_stage_dropped++" in stage,
        "a staging overflow must be counted, not silently swallowed")

flush = function_body(API, "journal_flush")
require("alarm_journal_append" in flush, "journal_flush must be the thing that writes")
require(flush.index("portEXIT_CRITICAL") < flush.index("alarm_journal_append"),
        "the flash write must happen after the critical section is left, not inside it")
require("s_stage_dropped" in API and '"staging_dropped"' in API,
        "records lost to staging overflow must be reported to the operator")

# Every lifecycle transition reaches the journal, and the flush is driven from
# the sampling task as well as from the operator's own actions.
task = function_body(API, "operational_task")
require("journal_flush()" in task, "staged journal records are never drained by the task")
require("alarm_journal_open()" in task,
        "the journal must be mounted off the HTTP registration path")

commit = function_body(API, "commit_alarm_locked")
require("ALARM_JOURNAL_RAISED" in commit and "ALARM_JOURNAL_CLEARED" in commit,
        "raise and clear transitions are not journalled")
ack = function_body(API, "alarms_ack_post")
require("ALARM_JOURNAL_ACKNOWLEDGED" in ack, "acknowledgement is not journalled")
require("journal_flush()" in ack, "an acknowledgement must reach flash before the reply")

require("uint32_t sequence" in HEADER and "uint32_t uptime_ms" in HEADER and
        "uint8_t code" in HEADER and "uint8_t transition" in HEADER,
        "a journal record must carry sequence, timestamp, alarm code and transition")

# Corruption is survivable: every record is validated on read.
decode = function_body(JOURNAL, "journal_decode")
require("ALARM_JOURNAL_MAGIC" in decode and "ALARM_JOURNAL_VERSION" in decode and
        "journal_crc16" in decode,
        "stored records must be validated on magic, version and CRC before being trusted")
require("return false" in decode, "an unparseable record must be rejected, not repaired")
read_page = function_body(JOURNAL, "alarm_journal_read_page")
require("if (!journal_decode(record, &entry)) continue;" in read_page,
        "a record that does not parse must be skipped, never returned and never fatal")
require('"unreadable_skipped"' in API,
        "the count of unreadable records must be visible, not hidden")

# Timestamps are uptime, and the payload says so rather than implying a date.
require('"time_base", "uptime_ms"' in API,
        "the journal payload must declare that its times are uptime-relative")
require("no real-time clock" in API,
        "the payload must state the controller has no wall clock rather than imply a date")

# ---------------------------------------------------------------------------
# A2: paged reads. 1.9 MB must never become one response.
# ---------------------------------------------------------------------------

journal_get = function_body(API, "alarms_journal_get")
require('"offset"' in journal_get and '"limit"' in journal_get,
        "the journal endpoint is not paged")
require("ALARM_JOURNAL_PAGE_MAX" in journal_get,
        "the page size must be bounded, or a caller can ask for the whole journal")
page_max = int(re.search(r"#define\s+ALARM_JOURNAL_PAGE_MAX\s+(\d+)", API).group(1))
require(0 < page_max <= 200,
        f"ALARM_JOURNAL_PAGE_MAX={page_max} is too large for a controller whose minimum "
        "free internal heap is around 130 kB")
require("alarm_journal_read_page(" in journal_get,
        "the endpoint must read a window, not the whole journal")
require('"has_more"' in journal_get and '"next_offset"' in journal_get,
        "a paged endpoint must say whether older records remain")
capacity = int(re.search(r"#define\s+ALARM_JOURNAL_CAPACITY\s+(\d+)", HEADER).group(1))
require(capacity > 96,
        f"the journal holds {capacity} records: it must be materially larger than the "
        "96-entry RAM ring it exists to outlive")
require("ALARM_JOURNAL_BLOCK_SLOTS" in JOURNAL,
        "the journal must be walked in bounded blocks, not read whole into RAM")

# ---------------------------------------------------------------------------
# A3: shelving requires an authenticated engineering session
# ---------------------------------------------------------------------------

for endpoint in ['"/api/operator/alarms/shelve"', '"/api/operator/alarms/unshelve"',
                 '"/api/operator/alarms/journal"']:
    require(endpoint in API, f"missing endpoint {endpoint}")

shelve = function_body(API, "alarms_shelve_post")
unshelve = function_body(API, "alarms_unshelve_post")
for name, body in [("shelve", shelve), ("unshelve", unshelve)]:
    require("engineering_auth_is_authorized(request)" in body,
            f"{name} must require an authenticated engineering session; this file sits "
            "outside the authorization gateway so the check has to be explicit")
    require('"401 Unauthorized"' in body,
            f"{name} must fail closed with 401 when unauthenticated")
    auth_index = body.index("engineering_auth_is_authorized")
    require(auth_index < body.index("http_json_parse_bounded"),
            f"{name} must check authorisation before reading the request body")
    require(auth_index < body.index("portENTER_CRITICAL"),
            f"{name} must check authorisation before mutating suppression state")

# ---------------------------------------------------------------------------
# A3: a shelf is bounded and cannot be indefinite
# ---------------------------------------------------------------------------

shelf_min = int(re.search(r"#define\s+ALARM_SHELF_MIN_MS\s+(\d+)", API).group(1))
shelf_max = int(re.search(r"#define\s+ALARM_SHELF_MAX_MS\s+(\d+)", API).group(1))
require(shelf_min > 0, "a shelf with no minimum is a mis-click, not a decision")
require(shelf_max > shelf_min, "the shelf maximum must exceed its minimum")
require(shelf_max <= 24 * 3600 * 1000,
        f"ALARM_SHELF_MAX_MS={shelf_max} exceeds a day: a shelf that outlives the shift "
        "that created it is a disabled alarm under another name")
require("ALARM_SHELF_MIN_MS" in shelve and "ALARM_SHELF_MAX_MS" in shelve,
        "the shelve endpoint does not enforce the published bounds")
require("have_duration" in shelve,
        "the shelf expiry must be required, not inferred")
require('"shelf_duration_out_of_range"' in shelve,
        "a shelf request outside the bounds must be rejected explicitly")
require("400 Bad Request" in shelve,
        "an unbounded or missing expiry must be a client error, not a silent default")
require(not re.search(r"ALARM_SHELF_DEFAULT|shelf.*=\s*0U?\s*;\s*/\*\s*indefinite", shelve),
        "there must be no default or indefinite shelf duration")
require('"shelf_minimum_ms"' in API and '"shelf_maximum_ms"' in API and
        '"shelf_expiry_required", true' in API,
        "the shelf bounds must be published so suppression policy is auditable")

# ---------------------------------------------------------------------------
# A3: expiry auto-unshelves, without an operator being present
# ---------------------------------------------------------------------------

expire = function_body(API, "service_shelf_locked")
require("alarm->shelved = false" in expire, "an expired shelf must clear itself")
require("shelf_expires_ms" in expire, "shelf expiry is not time based")
require("(int32_t)(timestamp - alarm->shelf_expires_ms)" in expire,
        "shelf expiry must use a wrap-safe signed comparison; uptime milliseconds roll "
        "over at ~49.7 days and a shelf must not become permanent because of it")
require("ALARM_JOURNAL_SHELF_EXPIRED" in expire,
        "an automatic unshelve must be journalled: 'the suppression ended and nobody was "
        "told' is the hole audited shelving exists to close")
service_all = function_body(API, "service_alarms_locked")
require("service_shelf_locked" in service_all,
        "shelf expiry must run on the observation tick, not only when someone looks")
alarms_get = function_body(API, "alarms_get")
require("service_shelf_locked" in alarms_get,
        "an expired shelf must never be observable as still in force when the list is read")

# ---------------------------------------------------------------------------
# A3: shelve and unshelve are journalled
# ---------------------------------------------------------------------------

require("ALARM_JOURNAL_SHELVED" in shelve and "journal_flush()" in shelve,
        "a shelve must be journalled and written before the operator is told it took effect")
require("ALARM_JOURNAL_UNSHELVED" in unshelve and "journal_flush()" in unshelve,
        "an unshelve must be journalled")
for token in ["ALARM_JOURNAL_SHELVED", "ALARM_JOURNAL_UNSHELVED", "ALARM_JOURNAL_SHELF_EXPIRED",
              "ALARM_JOURNAL_RAISED", "ALARM_JOURNAL_ACKNOWLEDGED", "ALARM_JOURNAL_CLEARED"]:
    require(token in HEADER, f"the journal format does not define {token}")

# ---------------------------------------------------------------------------
# A3: shelved alarms stay visible but leave the triage counts
# ---------------------------------------------------------------------------

require('cJSON_AddBoolToObject(item, "shelved"' in alarms_get,
        "a shelved alarm must report its shelf state on its own row")
require('"shelf_remaining_ms"' in alarms_get,
        "a shelved alarm must report how long the shelf has left to run")
require('"suppression"' in alarms_get,
        "the ISA-18.2 suppression state must be named, not reduced to a boolean")
require("suppressed-by-design" in API and "out-of-service" in API,
        "the other two suppression states must be named rather than quietly "
        "collapsed into shelving")

# The row is still emitted for a shelved alarm: prominence is the only thing given up.
skips = re.findall(r"^\s*(?:if \()?(.*?)\)? continue;", alarms_get, re.M)
require(len(skips) == 3,
        f"the alarm listing gained or lost a skip ({skips}): a shelved alarm must remain "
        "visible and inspectable, never hidden")
for skip in skips:
    require("shelved" not in skip and "shelf" not in skip and "suppression" not in skip,
            f"shelving must not gate whether an alarm row is produced: {skip}")
require(alarms_get.index('cJSON_AddBoolToObject(item, "shelved"') <
        alarms_get.index("cJSON_AddItemToArray(items, item)"),
        "the shelf state must be attached to the row, not decide whether it exists")

# ...but it does leave the counts an operator triages from.
#
# A9 added two more suppression states, so the counting gates moved from the
# shelved flag to the effective suppression state that shelving is now one of.
# The guarantee being asserted is unchanged and is checked in two parts: the gate
# is a suppression gate, and shelving is one of the things that closes it. A
# literal "!a->shelved" would have passed while a refactor quietly dropped
# shelving out of the derivation, which is the failure this pair catches.
require("if (a->present && !suppressed) {" in alarms_get,
        "a suppressed alarm must leave the active count")
require("if (!suppressed) {" in alarms_get,
        "a suppressed alarm must leave the outstanding count")
require(re.search(r"suppressed\s*=\s*alarm_suppression_hidden_from_triage", alarms_get),
        "the triage gate must be derived from the ISA-18.2 suppression state")
require(".shelved = a->shelved" in alarms_get,
        "shelving must still be one of the states that removes an alarm from the "
        "triage counts; the gate is derived, so shelving has to be fed into it")
require('"shelved"' in alarms_get and '"shelved_active"' in alarms_get,
        "shelved work must still be reported as a separate figure, never hidden entirely")

# Shelving must not suppress detection. Nothing in the detection path may consult it.
for name in ["update_alarm_locked", "commit_alarm_locked", "service_alarm_locked",
             "detect_events", "collect_sample"]:
    body = strip_comments(function_body(API, name))
    require("shelved" not in body and "shelf" not in body,
            f"{name} consults the shelf state: shelving must suppress prominence, never "
            "condition detection, occurrence counting or the record")

# Acknowledgement is untouched by shelving.
ack_code = strip_comments(ack)
require("shelved" not in ack_code and "shelf" not in ack_code,
        "acknowledgement must not be blocked or altered by shelving")

# ---------------------------------------------------------------------------
# Behaviour already verified on hardware must not regress
# ---------------------------------------------------------------------------

require("alarm->acknowledged = false" in commit,
        "a recurrence must still clear a previous acknowledgement")
require("alarm->acknowledged" not in commit.split("} else {")[1],
        "clearing a condition must still not touch the acknowledged flag: RTN "
        "Unacknowledged retention depends on it")
for token in ['"rtn_unacknowledged"', '"unacknowledged"', '"acknowledged"', '"normal"',
              '"ISA-18.2"', '"caused_by"', '"role"', '"stale"']:
    require(token in API, f"previously verified alarm reporting lost {token}")
detect = function_body(API, "detect_events")
boot_scan = detect[:detect.index("s_observed = *next;")]
require(boot_scan.count("append_event_ex(") == 4 and boot_scan.count(", true);") == 4,
        "boot-present conditions must still be raised once and bypass the on-delay")

# ---------------------------------------------------------------------------
# Build and CI registration
# ---------------------------------------------------------------------------

require('"alarm_journal.c"' in CMAKE, "the journal is not compiled")
require(re.search(r"REQUIRES[^\n]*\bspiffs\b", CMAKE),
        "the journal's filesystem dependency is not declared")
require("tests/alarm_journal_shelving_source_contract.py" in WORKFLOW,
        "this contract is not registered in the CI workflow")


# ---------------------------------------------------------------------------
# Executable half: compile alarm_journal.c and exercise it
# ---------------------------------------------------------------------------

STUBS = {
    # Force-included into alarm_journal.c so the fsync rename has a prototype on
    # BOTH platforms: glibc declares fsync in <unistd.h> (so the old
    # -Dfsync(fd)=0 corrupted that declaration), while MinGW does not declare it
    # at all (so a bare rename left it implicit). Declaring it ourselves is the
    # only form that is correct on both.
    "pvdg_test_fsync.h": """
#pragma once
int pvdg_test_fsync(int fd);
""",
    "esp_err.h": """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
static inline const char *esp_err_to_name(esp_err_t error) { (void)error; return "stub"; }
""",
    "esp_log.h": """
#pragma once
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
""",
    "esp_spiffs.h": """
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct {
    const char *base_path;
    const char *partition_label;
    size_t max_files;
    bool format_if_mount_failed;
} esp_vfs_spiffs_conf_t;
static inline bool esp_spiffs_mounted(const char *label) { (void)label; return true; }
static inline esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf)
{ (void)conf; return ESP_OK; }
static inline esp_err_t esp_spiffs_format(const char *label) { (void)label; return ESP_OK; }
""",
    # The provisioning guard reads the partition's type and subtype, so the stub
    # has to carry them rather than being an opaque handle.
    "esp_partition.h": """
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef enum {
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0,
    ESP_PARTITION_SUBTYPE_DATA_NVS = 2,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82
} esp_partition_subtype_t;
typedef struct {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;
static inline const esp_partition_t *esp_partition_find_first(
    esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label)
{
    static const esp_partition_t found = {
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0x620000, 0x1E0000, "storage"
    };
    (void)type; (void)subtype; (void)label;
    return &found;
}
""",
    "freertos/FreeRTOS.h": """
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
""",
    "freertos/semphr.h": """
#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
typedef struct SemaphoreStub *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ return (SemaphoreHandle_t)(uintptr_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks)
{ (void)handle; (void)ticks; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{ (void)handle; return pdTRUE; }
""",
}

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alarm_journal.h"

/* alarm_journal.c is compiled with -Dfsync=pvdg_test_fsync, so this is the
 * fsync it calls. Durability is what the journal contract tests, and it tests
 * it by reopening the file from a fresh process -- which the OS satisfies from
 * the page cache without any flush to disk. A real fsync here would only slow
 * the test down; it would not make the assertion stronger. */
int pvdg_test_fsync(int fd)
{
    (void)fd;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    alarm_journal_init();
    alarm_journal_open();
    if (!alarm_journal_ready()) { printf("NOTREADY %s\n", alarm_journal_status()); return 3; }

    if (strcmp(argv[1], "write") == 0 && argc >= 4) {
        const unsigned long first = strtoul(argv[2], NULL, 10);
        const unsigned long count = strtoul(argv[3], NULL, 10);
        for (unsigned long i = 0; i < count; ++i) {
            alarm_journal_entry_t entry;
            memset(&entry, 0, sizeof(entry));
            entry.code = (unsigned char)(1U + ((first + i) % 4U));
            entry.transition = (unsigned char)((first + i) % 6U);
            entry.uptime_ms = (unsigned)(1000UL * (first + i));
            entry.detail = (unsigned short)((first + i) % 60000UL);
            if (!alarm_journal_append(&entry)) { printf("APPENDFAIL %lu\n", i); return 4; }
        }
    } else if (strcmp(argv[1], "read") == 0 && argc >= 4) {
        const unsigned long offset = strtoul(argv[2], NULL, 10);
        const unsigned long limit = strtoul(argv[3], NULL, 10);
        alarm_journal_entry_t *page = calloc(limit ? limit : 1UL, sizeof(*page));
        if (!page) return 5;
        bool more = false;
        const size_t produced = alarm_journal_read_page((uint32_t)offset, (size_t)limit,
                                                        page, &more);
        for (size_t i = 0; i < produced; ++i) {
            printf("E %u %u %u %u %u\n", (unsigned)page[i].sequence, (unsigned)page[i].code,
                   (unsigned)page[i].transition, (unsigned)page[i].uptime_ms,
                   (unsigned)page[i].detail);
        }
        printf("MORE %d\n", more ? 1 : 0);
        free(page);
    }
    printf("STORED %u\nNEXT %u\nUNREADABLE %u\nWRITEFAIL %u\n",
           (unsigned)alarm_journal_stored(), (unsigned)alarm_journal_next_sequence(),
           (unsigned)alarm_journal_invalid_skipped(), (unsigned)alarm_journal_write_failures());
    return 0;
}
"""

if shutil.which("gcc") is None:
    raise AssertionError("gcc is required to run the alarm journal round-trip contract")

work = Path(tempfile.mkdtemp(prefix="alarm_journal_"))
try:
    include = work / "stubs"
    (include / "freertos").mkdir(parents=True)
    for name, text in STUBS.items():
        (include / name).write_text(text, encoding="utf-8")
    (work / "harness.c").write_text(HARNESS, encoding="utf-8")
    journal_file = work / "alarm.jnl"
    binary = work / ("harness.exe" if sys.platform == "win32" else "harness")

    compile_command = [
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
        f"-I{include}", f"-I{ROOT / 'components/web_server/include'}",
        f'-DALARM_JOURNAL_FILE="{journal_file.as_posix()}"',
        # Redirect fsync to a harness-provided stub rather than defining it away.
        # "-Dfsync(fd)=0" rewrote glibc's own declaration in <unistd.h> --
        # `extern int fsync(int)` became `extern int 0` -- which broke the Linux
        # build while passing on MinGW, whose unistd.h does not declare fsync at
        # all. Renaming the symbol leaves every declaration well-formed on both.
        # fileno() and fsync() are POSIX, not ISO C. Under -std=c11 glibc hides
        # both behind a feature-test macro, so without this the harness compiled
        # on MinGW (which exposes them regardless) and failed on Linux with an
        # implicit declaration. ESP-IDF's newlib exposes them, so this is a
        # property of the test harness and not of the firmware.
        "-D_DEFAULT_SOURCE",
        "-D_POSIX_C_SOURCE=200809L",
        "-Dfsync=pvdg_test_fsync",
        "-include", "pvdg_test_fsync.h",
        str(work / "harness.c"), str(JOURNAL_PATH), "-o", str(binary),
    ]
    compiled = subprocess.run(compile_command, capture_output=True, text=True)
    require(compiled.returncode == 0,
            "alarm_journal.c does not compile clean with -Wall -Wextra -Werror:\n"
            + compiled.stdout + compiled.stderr)

    def run(*arguments) -> dict:
        result = subprocess.run([str(binary), *[str(a) for a in arguments]],
                                capture_output=True, text=True)
        require(result.returncode == 0,
                f"harness {arguments} failed ({result.returncode}): {result.stdout}{result.stderr}")
        entries = []
        values = {}
        for line in result.stdout.splitlines():
            parts = line.split()
            if parts and parts[0] == "E":
                entries.append(tuple(int(p) for p in parts[1:]))
            elif len(parts) == 2:
                values[parts[0]] = int(parts[1])
        values["entries"] = entries
        return values

    # 1. A fresh journal starts empty and does not need a formatted filesystem.
    first = run("read", 0, 10)
    require(first["STORED"] == 0 and first["entries"] == [],
            "a new journal must start empty")

    # 2. Records round-trip, newest first.
    run("write", 0, 10)
    page = run("read", 0, 5)
    require(page["STORED"] == 10, f"expected 10 stored records, got {page['STORED']}")
    require(len(page["entries"]) == 5, "the page size was not honoured")
    require(page["MORE"] == 1, "has_more must be set while older records remain")
    sequences = [entry[0] for entry in page["entries"]]
    require(sequences == [10, 9, 8, 7, 6],
            f"records must be returned newest first with monotonic sequences: {sequences}")

    # 3. Paging is a window, not a truncation: offset reaches the older records.
    older = run("read", 5, 5)
    require([entry[0] for entry in older["entries"]] == [5, 4, 3, 2, 1],
            "the second page must continue where the first ended")
    require(older["MORE"] == 0, "has_more must be clear on the last page")

    # 4. The journal survives a restart: the sequence continues, nothing is lost.
    reopened = run("write", 10, 5)
    require(reopened["STORED"] == 15, "records did not survive a reopen")
    require(reopened["NEXT"] == 16,
            f"the sequence must continue across a restart, got {reopened['NEXT']}")

    # 5. Corruption is survivable. A mangled record is skipped, not trusted, and
    #    does not take the rest of the journal with it.
    raw = bytearray(journal_file.read_bytes())
    record_bytes = 16
    for offset in range(3 * record_bytes, 4 * record_bytes):
        raw[offset] ^= 0xA5
    journal_file.write_bytes(bytes(raw))
    damaged = run("read", 0, 20)
    require(damaged["UNREADABLE"] == 1,
            f"the corrupt record must be counted, got {damaged['UNREADABLE']}")
    require(damaged["STORED"] == 14, "exactly one record should have been lost")
    survivors = [entry[0] for entry in damaged["entries"]]
    require(4 not in survivors, "a record that fails validation must not be returned")
    require(sorted(survivors, reverse=True) == survivors and len(survivors) == 14,
            f"the remaining records must still be readable and ordered: {survivors}")

    # 6. Wrap is oldest-first, and the journal keeps recording rather than
    #    refusing new records once full.
    journal_file.unlink()
    run("write", 0, capacity + 20)
    wrapped = run("read", 0, 5)
    require(wrapped["STORED"] == capacity,
            f"a full journal must hold exactly its capacity, got {wrapped['STORED']}")
    newest = [entry[0] for entry in wrapped["entries"]]
    require(newest == [capacity + 20, capacity + 19, capacity + 18, capacity + 17, capacity + 16],
            f"the newest records must survive the wrap: {newest}")
    oldest = run("read", capacity - 3, 5)
    oldest_sequences = [entry[0] for entry in oldest["entries"]]
    require(oldest_sequences == [23, 22, 21],
            f"the twenty oldest records must be the ones overwritten: {oldest_sequences}")
    require(oldest["MORE"] == 0, "there is nothing older than the oldest surviving record")
finally:
    shutil.rmtree(work, ignore_errors=True)

print("alarm journal and shelving source contract passed "
      f"(journal {capacity} records on the storage partition, page limit {page_max}, "
      f"shelf {shelf_min // 1000} s to {shelf_max // 3600000} h, "
      f"provisions the unused storage partition once and erases nothing else)")
