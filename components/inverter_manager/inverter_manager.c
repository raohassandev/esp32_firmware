#include "inverter_manager.h"
#include "inverter_prerequisite.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"
#include "inverter_telemetry_block.h"
#include "inverter_profile_decode.h"
#include "inverter_status.h"
#include "inverter_write_confirmation.h"
#include "esp_check.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "modbus_tcp.h"

static const char *TAG = "inverters";
static const char *DEFAULT_PROFILE_ID = "custom.modbus-percent-v1";

#define INVERTER_TELEMETRY_TASK_STACK 5120
#define INVERTER_TELEMETRY_TASK_PRIORITY 5
#define INVERTER_TELEMETRY_IDLE_MS 100
/* The monitoring block's own period. See poll_measurements. */
#define INVERTER_MEASUREMENT_BLOCK_INTERVAL_MS 1000U
#define INVERTER_IDENTITY_RECHECK_MS 60000U
/* Transport-level retries only. There is no sleep between attempts and no
 * readback transaction here: both would sit inside the control loop, and Modbus
 * speed is the highest priority requirement for this controller. */
#define INVERTER_COMMAND_MAX_ATTEMPTS 2U
#define INVERTER_SAFE_FALLBACK_PERCENT 0.0f

/*
 * DEFERRED WRITE CONFIRMATION WINDOWS (P0-9).
 *
 * NOT manufacturer values. No inverter manual consulted for this firmware
 * specifies how long a setpoint takes to appear in its readback register, so
 * nothing here is derived from one. These are firmware-side acquisition windows:
 *
 *  SETTLE   - a readback that disagrees within this window of the write is
 *             treated as "not applied yet", not as a mismatch. It only ever
 *             delays a verdict; it can never turn a mismatch into a success.
 *             This is only the DEFAULT: a profile may specify its own settle
 *             window via power_limit_settle_ms, because how long a setpoint
 *             takes to reach the readback register is a property of the device.
 *             The lab simulator, measured, defers by ~1500 ms, against which
 *             this 500 ms default would report a false mismatch.
 *  DEADLINE - past this age a write with no matching readback is UNVERIFIED and
 *             the inverter is driven safe. It bounds how long an unconfirmed
 *             setpoint may stand.
 *
 * Both must be validated per site against real hardware during commissioning.
 */
#define INVERTER_CONFIRMATION_SETTLE_MS 500U
#define INVERTER_CONFIRMATION_DEADLINE_MS 5000U

/*
 * PREREQUISITE ENABLE RE-VERIFICATION PERIOD.
 *
 * NOT a manufacturer value. No manual states how often the power-limitation
 * switch should be checked, so this is a firmware-side risk decision and the
 * reasoning is recorded here rather than left as a number.
 *
 * Verifying once is not enough. These registers are ordinary writable switches:
 * a commissioning engineer, a plant SCADA system or a second Modbus master can
 * turn one off at any time, and the Solis manual is explicit about what happens
 * when it does -- tag 3070 "0x55 OFF(Power to 100%)". The machine does not
 * freeze at the last limit, it goes to full output. So a controller that
 * verified the switch once at start-up and then trusted it forever would keep
 * reporting a confirmed limit while a 100 kW inverter ran wide open, which is
 * precisely the false confirmation this whole mechanism exists to prevent.
 *
 * 5000 ms is chosen against three constraints:
 *
 *  - Cost. One extra register read per inverter per 5 s. The affected brands are
 *    all behind RS-485 gateways polling at 1000 ms with two or three
 *    transactions per poll, so this adds a few percent to the bus load. It is
 *    also gated on the poll tick, so it can never out-run telemetry_poll_ms.
 *  - Exposure. The worst case is that the switch is turned off just after a
 *    successful read, so the plant can be unlimited for up to one period plus
 *    one poll before the controller notices and drops the inverter out of the
 *    commandable fleet. Six seconds of unnoticed full output is a real risk and
 *    the honest reason it is accepted is that the alternative -- reading it on
 *    every control cycle -- would put a transaction on the 20 ms control path,
 *    which is forbidden and would be a worse failure.
 *  - Flapping. The sample must not expire before its own re-read is due, or the
 *    inverter would leave and rejoin the fleet continuously and the control
 *    engine would see the capacity oscillate. Hence a separate expiry at three
 *    times the recheck period, mirroring how telemetry staleness is derived
 *    from the poll period.
 *
 * A profile may tighten the period via prerequisite_recheck_ms. Both numbers
 * must be re-examined against real equipment at commissioning.
 */
#define INVERTER_PREREQUISITE_RECHECK_MS 5000U
#define INVERTER_PREREQUISITE_EXPIRY_MULTIPLE 3U

typedef struct {
    inverter_config_t config;
    const inverter_profile_t *profile;
    /* How far a command to this inverter may go. write_allowed is kept as the
     * single boolean the command path tests, and is derived from this so the two
     * can never disagree. */
    inverter_write_permission_t permission;
    bool lab_target;
    bool write_allowed;
    bool identity_checked;
    uint32_t last_identity_ms;
    uint32_t next_poll_ms;
    /* The monitoring block is due independently of the control read. */
    uint32_t next_measurements_ms;
    /* Earliest timestamp at which prerequisite I/O may be attempted again. Keeps
     * a device that refuses the enable write from being hammered once per
     * acquisition pass. */
    uint32_t next_prerequisite_ms;
    modbus_connection_t connection;
    SemaphoreHandle_t io_mutex;
    inverter_data_t data;
    portMUX_TYPE lock;
} inverter_runtime_t;

typedef struct {
    inverter_runtime_t *runtime;
    const inverter_profile_t *profile;
    float rated_kw;
    float percent;
    float commanded_kw;
    uint16_t words[2];
    uint8_t word_count;
} command_target_t;

static inverter_runtime_t s_inverters[APP_MAX_INVERTERS];
static uint8_t s_inverter_count;
/* Consecutive command refusals, for log throttling. See the refusal site. */
static uint32_t s_command_refusals;
static float s_total_rated_kw;
static portMUX_TYPE s_capacity_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_telemetry_task;
/* False until init has resolved every configured inverter and its profile. The
 * commissioning gate must not read an empty fleet as a commissioned one. */
static bool s_fleet_resolved;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Defined below with the command path; declared here because the background
 * acquisition task owns the safe-zero that an unconfirmed write demands. */
static esp_err_t encode_command(const inverter_profile_t *profile, float percent,
                                uint16_t *words, uint8_t *word_count);
static esp_err_t write_profile_command(inverter_runtime_t *runtime,
                                       const inverter_profile_t *profile,
                                       const uint16_t *words,
                                       uint8_t word_count);

static uint32_t profile_poll_ms(const inverter_profile_t *profile)
{
    return profile && profile->telemetry_poll_ms >= 100U
        ? profile->telemetry_poll_ms
        : 1000U;
}

static uint32_t profile_stale_ms(const inverter_profile_t *profile)
{
    uint32_t poll = profile_poll_ms(profile);
    return profile && profile->telemetry_stale_timeout_ms >= poll
        ? profile->telemetry_stale_timeout_ms
        : poll * 3U;
}

static uint32_t identity_raw(const uint16_t *words, uint8_t count)
{
    if (!words || count == 0) return 0;
    if (count == 1) return words[0];
    return ((uint32_t)words[0] << 16) | words[1];
}

static bool identity_matches(const inverter_profile_t *profile,
                             const uint16_t *words, uint8_t count)
{
    if (!profile || !profile->has_identity_probe) return true;
    uint32_t mask = profile->identity_mask ? profile->identity_mask : UINT32_MAX;
    return (identity_raw(words, count) & mask) == (profile->identity_expected & mask);
}

static bool identity_is_current(const inverter_runtime_t *runtime, uint32_t timestamp)
{
    if (!runtime->data.identity_supported) return true;
    return runtime->identity_checked && runtime->data.identity_verified &&
           runtime->last_identity_ms != 0U &&
           timestamp - runtime->last_identity_ms <= INVERTER_IDENTITY_RECHECK_MS;
}

static void invalidate_identity(inverter_runtime_t *runtime)
{
    portENTER_CRITICAL(&runtime->lock);
    runtime->identity_checked = false;
    runtime->last_identity_ms = 0U;
    if (runtime->data.identity_supported) runtime->data.identity_verified = false;
    portEXIT_CRITICAL(&runtime->lock);
}

/*
 * COMMUNICATIONS-FAILURE GRACE.
 *
 * A Modbus link in an industrial cabinet drops the occasional transaction --
 * VFD noise, a contactor, a marginal cable. Treating the first failed read as
 * "this inverter is gone" removes it from the commandable capacity instantly,
 * and capacity is the denominator the setpoint percentage is computed against.
 * So one dropped frame moves the command on EVERY OTHER inverter, and a
 * marginal link makes the whole fleet hunt.
 *
 * An inverter therefore keeps its place in the capacity until nothing has been
 * read from it for this long. Note which direction the grace errs in: during
 * the window the controller believes it has MORE capacity than it does, so the
 * percentage it commands under-delivers and the generator carries more load,
 * not less. And by the time the window expires the inverter's own
 * communications fail-safe -- a shorter timeout, one minute by default -- has
 * already driven it to zero. The two are sized so the controller re-allocates
 * only after the lost machine has stopped generating.
 *
 * HMI-EVIDENCE: seconds since the last successful read, per inverter, is the
 * single most useful number for tracing a comms fault -- it distinguishes "the
 * cable is out" from "this link answers one poll in three", which look the same
 * through an online/offline boolean. It is computed here and reaches no screen.
 *
 * PHASE-2: the product owner asked for this window to be entered by the engineer
 * at commissioning, defaulting to two minutes. It is a compile-time constant for
 * now because making it configurable is an app_config_t schema change, and the
 * safety behaviour is worth having before the form is. The value below IS the
 * agreed default.
 */
/* Defined in config_types.h so the configuration validator can compare it
 * against each inverter's own commissioned fail-safe. */

/* True while this inverter has answered at least once AND that answer is inside
 * the grace window.
 *
 * Both halves matter. Requiring a prior successful read is what stops a machine
 * that has NEVER replied from being carried by the grace: it has not lost
 * communications, it has never had any, and counting its rating would put
 * capacity behind a device that is not there. Expressed positively for that
 * reason -- the negative form reads as "not failed", which is true of a device
 * that never started. */
static bool link_within_grace_locked(const inverter_runtime_t *runtime, uint32_t timestamp)
{
    if (runtime->data.last_telemetry_ms == 0U) return false;
    return (uint32_t)(timestamp - runtime->data.last_telemetry_ms) <= INVERTER_COMMS_FAIL_GRACE_MS;
}

static void recompute_commandable_capacity(void)
{
    float total = 0.0f;
    uint32_t timestamp = now_ms();
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        /* An unconfirmed write latches confirmation_fault, which removes this
         * inverter from the commandable capacity entirely. That is the
         * structural consequence of P0-9: a machine whose setpoint could not be
         * verified is not part of the fleet the control engine may command. */
        /* Momentary loss is tolerated; a sustained one is not. online and
         * telemetry_stale flip on a single dropped frame, so a healthy link OR
         * a recent one keeps the inverter in the fleet. The terms BELOW are
         * deliberately outside the grace: a confirmation fault, an unverified
         * prerequisite and a stale identity are statements about correctness
         * rather than about the link, and none of them gets better by waiting. */
        const bool link_healthy = runtime->data.online && runtime->data.telemetry_valid &&
                                  !runtime->data.telemetry_stale;
        const bool link_recent = link_within_grace_locked(runtime, timestamp);
        bool eligible = runtime->config.enabled && runtime->write_allowed &&
                        runtime->data.connection_initialized &&
                        (link_healthy || link_recent) &&
                        !runtime->data.confirmation_fault &&
                        /* An unverified prerequisite enable register removes this
                         * inverter for exactly the same reason a confirmation
                         * fault does: its setpoint would be accepted, echoed and
                         * ignored. The flag is false when zeroed, so unknown is
                         * not satisfied. */
                        runtime->data.prerequisite_satisfied &&
                        identity_is_current(runtime, timestamp) &&
                        isfinite(runtime->config.rated_power_kw) &&
                        runtime->config.rated_power_kw > 0.0f;
        float rated = runtime->config.rated_power_kw;
        portEXIT_CRITICAL(&runtime->lock);
        if (eligible) total += rated;
    }
    portENTER_CRITICAL(&s_capacity_lock);
    s_total_rated_kw = total;
    portEXIT_CRITICAL(&s_capacity_lock);
}

static esp_err_t read_profile_block(inverter_runtime_t *runtime,
                                    uint8_t function_code,
                                    uint16_t address,
                                    uint8_t count,
                                    uint16_t *words)
{
    if (!runtime || !runtime->io_mutex || !words || count == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(runtime->io_mutex,
                       pdMS_TO_TICKS(runtime->config.endpoint.timeout_ms + 100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = modbus_tcp_read_registers(&runtime->connection, function_code,
                                              address, count, words);
    xSemaphoreGive(runtime->io_mutex);
    return err;
}

static esp_err_t verify_identity(inverter_runtime_t *runtime)
{
    const inverter_profile_t *profile = runtime->profile;
    uint32_t timestamp = now_ms();
    if (!profile || !profile->has_identity_probe) {
        portENTER_CRITICAL(&runtime->lock);
        runtime->identity_checked = true;
        runtime->last_identity_ms = timestamp;
        runtime->data.identity_supported = false;
        runtime->data.identity_verified = true;
        portEXIT_CRITICAL(&runtime->lock);
        return ESP_OK;
    }

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->identity_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = read_profile_block(runtime, profile->identity_function,
                                       profile->identity_address, count, words);
    bool matched = err == ESP_OK && identity_matches(profile, words, count);

    portENTER_CRITICAL(&runtime->lock);
    runtime->identity_checked = matched;
    runtime->last_identity_ms = matched ? timestamp : 0U;
    runtime->data.identity_supported = true;
    runtime->data.identity_verified = matched;
    runtime->data.last_error = err == ESP_OK && !matched ? ESP_ERR_INVALID_RESPONSE : err;
    portEXIT_CRITICAL(&runtime->lock);

    return matched ? ESP_OK : (err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err);
}

static esp_err_t poll_active_power(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || !profile->has_active_power) return ESP_ERR_NOT_SUPPORTED;

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->active_power_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;

    esp_err_t err = read_profile_block(runtime, profile->active_power_function,
                                       profile->active_power_address, count, words);
    float power_kw = 0.0f;
    if (err == ESP_OK) {
        err = inverter_profile_decode_value(words, count,
                                            profile->active_power_type,
                                            profile->active_power_word_order,
                                            profile->active_power_scale,
                                            &power_kw);
        if (err == ESP_OK && !isfinite(power_kw)) err = ESP_ERR_INVALID_RESPONSE;
    }

    portENTER_CRITICAL(&runtime->lock);
    runtime->data.telemetry_supported = true;
    if (err == ESP_OK) {
        runtime->data.measured_power_kw = power_kw;
        runtime->data.telemetry_valid = true;
        runtime->data.telemetry_stale = false;
        runtime->data.online = true;
        runtime->data.last_telemetry_ms = timestamp;
        runtime->data.read_successes++;
        runtime->data.consecutive_read_failures = 0;
        runtime->data.last_error = ESP_OK;
    } else {
        runtime->data.read_errors++;
        runtime->data.consecutive_read_failures++;
        runtime->data.last_error = err;
        runtime->data.online = false;
        runtime->data.telemetry_valid = false;
        runtime->data.telemetry_stale = true;
        runtime->identity_checked = false;
        runtime->last_identity_ms = 0U;
        if (runtime->data.identity_supported) runtime->data.identity_verified = false;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

/*
 * THE FULL TELEMETRY BLOCK: everything the machine measures, in one transaction.
 *
 * ITS OWN CADENCE, AND ITS OWN CONSEQUENCES. poll_active_power above reads the
 * one register the control path uses -- the confirmation evaluator and the fleet
 * cap both depend on it -- at the profile's poll rate. This is 102 registers of
 * DC strings, AC per phase, yield, temperature and device status, which nothing
 * in the control path reads. So it polls once a second at most, and a failure
 * here does NOT mark the inverter offline, does not count against read quality,
 * does not touch the identity check and cannot influence a command. The previous
 * sample simply stays in place with its own timestamp, and a page says how old
 * it is.
 *
 * That separation is the whole design. If this block could take an inverter
 * offline, then adding a monitoring page would have made the controller less
 * reliable -- which is precisely the trade this refuses to make.
 *
 * READING GRANTS NOTHING. Every register is RO in the manufacturer's manual.
 * Nothing here promotes a profile out of LAB_ONLY or counts as physical
 * qualification for writing.
 */
static esp_err_t poll_measurements(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || profile->telemetry_layout == INVERTER_TELEMETRY_LAYOUT_NONE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (profile->telemetry_registers == 0U ||
        profile->telemetry_registers > INVERTER_HUAWEI_BLOCK_REGISTERS) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS] = {0};
    esp_err_t err = read_profile_block(runtime, profile->telemetry_function,
                                       profile->telemetry_start,
                                       (uint8_t)profile->telemetry_registers, words);
    if (err != ESP_OK) return err;

    inverter_measurements_t decoded;
    switch (profile->telemetry_layout) {
    case INVERTER_TELEMETRY_LAYOUT_HUAWEI_V3:
        if (!inverter_huawei_block_decode(words, profile->telemetry_registers, &decoded)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        break;
    default:
        /* A layout the catalogue names but this build cannot decode. Refused
         * rather than decoded with the nearest available table: registers read
         * through the wrong manufacturer's map produce numbers, and numbers are
         * what get believed. */
        return ESP_ERR_NOT_SUPPORTED;
    }

    portENTER_CRITICAL(&runtime->lock);
    runtime->data.measurements = decoded;
    runtime->data.measurements_updated_ms = timestamp;
    portEXIT_CRITICAL(&runtime->lock);
    return ESP_OK;
}

static esp_err_t poll_readback(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || !profile->has_power_limit_readback) return ESP_ERR_NOT_SUPPORTED;

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->power_limit_readback_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;

    esp_err_t err = read_profile_block(runtime, profile->power_limit_readback_function,
                                       profile->power_limit_readback_address, count, words);
    float readback_percent = 0.0f;
    if (err == ESP_OK) {
        err = inverter_profile_decode_value(words, count,
                                            profile->power_limit_readback_type,
                                            profile->power_limit_readback_word_order,
                                            profile->power_limit_readback_scale,
                                            &readback_percent);
        if (err == ESP_OK && !isfinite(readback_percent)) err = ESP_ERR_INVALID_RESPONSE;
    }

    portENTER_CRITICAL(&runtime->lock);
    if (err == ESP_OK) {
        runtime->data.readback_percent = readback_percent;
        runtime->data.has_readback = true;
        runtime->data.last_readback_ms = timestamp;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

/* Reads the scheduling-authority register: which authority currently owns
 * scheduling of this command target. Strictly read-only -- the function code is
 * checked to be a read code by inverter_profile_command_authority_described(),
 * and this never issues a write of any kind.
 *
 * It is a CONTENTION DETECTOR, not a precondition. The decision about what a
 * foreign value means belongs to the pure confirmation evaluator, which only
 * consults it after a write; see the profile comment for why gating a command on
 * it would deadlock. */
static esp_err_t poll_command_authority(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!inverter_profile_command_authority_described(profile)) {
        portENTER_CRITICAL(&runtime->lock);
        runtime->data.authority_supported = false;
        portEXIT_CRITICAL(&runtime->lock);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t word = 0;
    esp_err_t err = read_profile_block(runtime, profile->command_authority_function,
                                       profile->command_authority_address, 1U, &word);
    const uint16_t mask = inverter_profile_command_authority_mask(profile);
    const bool holds = err == ESP_OK &&
                       (uint16_t)(word & mask) ==
                           (uint16_t)(profile->command_authority_expected & mask);

    portENTER_CRITICAL(&runtime->lock);
    const bool was_holding = runtime->data.authority_read_valid &&
                             runtime->data.authority_holds;
    /* A target we owned and no longer own is the failure this poll exists to
     * catch: another master took the plant over underneath us. Counted whether
     * the transition came from a foreign value or from the read failing, because
     * both mean we can no longer show that we are the authority. */
    if (was_holding && !holds) runtime->data.authority_lost_count++;
    runtime->data.authority_supported = true;
    runtime->data.authority_last_error = err;
    if (err == ESP_OK) {
        runtime->data.authority_read_valid = true;
        runtime->data.authority_holds = holds;
        runtime->data.authority_raw = word;
        runtime->data.last_authority_read_ms = timestamp;
    } else {
        /* A failed read is not evidence that we still own the target. */
        runtime->data.authority_holds = false;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

/* Writes one register. Used only for the prerequisite enable register, and it
 * refuses any function code that is not a documented write code, so a
 * mis-transcribed profile cannot turn into a blind write to an unknown
 * register. One transaction, no sleep, no retry loop. */
static esp_err_t write_profile_register(inverter_runtime_t *runtime,
                                        uint8_t function_code,
                                        uint16_t address,
                                        uint16_t value)
{
    if (!runtime || !runtime->io_mutex) return ESP_ERR_INVALID_ARG;
    if (!inverter_prerequisite_write_function_supported(function_code)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (xSemaphoreTake(runtime->io_mutex,
                       pdMS_TO_TICKS(runtime->config.endpoint.timeout_ms + 100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = function_code == 16U
        ? modbus_tcp_write_multiple(&runtime->connection, address, &value, 1U)
        : modbus_tcp_write_single(&runtime->connection, address, value);
    xSemaphoreGive(runtime->io_mutex);
    return err;
}

static uint32_t prerequisite_recheck_ms(const inverter_profile_t *profile)
{
    uint32_t recheck = profile && profile->prerequisite_recheck_ms
                           ? profile->prerequisite_recheck_ms
                           : (uint32_t)INVERTER_PREREQUISITE_RECHECK_MS;
    /* A zero-length period would make every sample instantly due and, worse,
     * instantly expired. Floored so the schedule stays sane. */
    if (recheck < 100U) recheck = 100U;
    return recheck;
}

/* Reads the prerequisite enable register and records whether it holds. Read-only
 * function codes only, and the decision about what it means is not taken here. */
static esp_err_t read_prerequisite(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!inverter_profile_prerequisite_readback_described(profile)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t word = 0;
    esp_err_t err = read_profile_block(runtime, profile->prerequisite_readback_function,
                                       profile->prerequisite_readback_address, 1U, &word);
    const uint16_t mask = inverter_profile_prerequisite_mask(profile);
    const bool holds = err == ESP_OK &&
                       (uint16_t)(word & mask) == (uint16_t)(profile->prerequisite_value & mask);

    portENTER_CRITICAL(&runtime->lock);
    runtime->data.prerequisite_last_error = err;
    if (err == ESP_OK) {
        runtime->data.prerequisite_read_valid = true;
        runtime->data.prerequisite_holds = holds;
        runtime->data.prerequisite_raw = word;
        runtime->data.last_prerequisite_read_ms = timestamp;
    } else {
        /* A failed read is not evidence that the register still holds. The
         * previous sample keeps ageing and will expire. */
        runtime->data.prerequisite_holds = false;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

/* Writes the prerequisite enable register. Deliberately does NOT set
 * prerequisite_satisfied: an accepted write proves only that the transport took
 * the frame. Only read_prerequisite() may conclude that the register holds, and
 * the next pass of the acquisition loop performs that re-read. */
static esp_err_t write_prerequisite(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!inverter_profile_prerequisite_write_described(profile)) return ESP_ERR_NOT_SUPPORTED;

    esp_err_t err = write_profile_register(runtime, profile->prerequisite_write_function,
                                          profile->prerequisite_address,
                                          profile->prerequisite_value);
    portENTER_CRITICAL(&runtime->lock);
    runtime->data.prerequisite_write_issued = true;
    runtime->data.last_prerequisite_write_ms = timestamp;
    runtime->data.prerequisite_write_count++;
    runtime->data.prerequisite_last_error = err;
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

/*
 * Prerequisite enable sequencing, run ONLY from the background acquisition task.
 *
 * The sequence is read -> (write if it does not hold) -> re-read, spread across
 * acquisition passes rather than performed as a blocking burst, because the
 * control loop runs at a 20 ms period and HTTP handlers must never perform a
 * direct blocking Modbus transaction. Each pass issues at most ONE transaction.
 *
 * The decision itself is the pure evaluator in inverter_prerequisite.c; this
 * function assembles the evidence, performs the I/O it asks for and stores the
 * outcome. prerequisite_satisfied is written from the verdict and from nowhere
 * else.
 */
static void service_prerequisite(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    const uint32_t recheck = prerequisite_recheck_ms(profile);
    const uint32_t expiry = recheck * (uint32_t)INVERTER_PREREQUISITE_EXPIRY_MULTIPLE;

    inverter_prerequisite_evidence_t evidence = {0};
    evidence.populated = true;
    evidence.required = profile && profile->requires_prerequisite_enable;
    evidence.write_described = inverter_profile_prerequisite_write_described(profile);
    evidence.readback_described = inverter_profile_prerequisite_readback_described(profile);
    evidence.recheck_ms = recheck;
    evidence.expiry_ms = expiry;

    portENTER_CRITICAL(&runtime->lock);
    evidence.have_sample = runtime->data.prerequisite_read_valid &&
                           runtime->data.last_prerequisite_read_ms != 0U;
    evidence.sample_holds = runtime->data.prerequisite_holds;
    evidence.write_issued = runtime->data.prerequisite_write_issued;
    /* Strictly after: a reading taken at or before our own write describes a
     * state that write has since replaced. */
    evidence.sample_after_write =
        !runtime->data.prerequisite_write_issued ||
        runtime->data.last_prerequisite_read_ms > runtime->data.last_prerequisite_write_ms;
    evidence.sample_age_ms = evidence.have_sample
                                 ? timestamp - runtime->data.last_prerequisite_read_ms
                                 : 0U;
    portEXIT_CRITICAL(&runtime->lock);

    inverter_prerequisite_verdict_t decision = inverter_prerequisite_evaluate(&evidence);

    portENTER_CRITICAL(&runtime->lock);
    const bool was_satisfied = runtime->data.prerequisite_satisfied;
    runtime->data.prerequisite_required = evidence.required;
    runtime->data.prerequisite_describable =
        evidence.write_described && evidence.readback_described;
    runtime->data.prerequisite_unverifiable = decision.unverifiable;
    runtime->data.prerequisite_satisfied = decision.satisfied;
    if (decision.satisfied && !was_satisfied) runtime->data.prerequisite_confirmed_count++;
    /* A prerequisite that STOPS holding is the failure this re-verification
     * exists for, so it is counted separately from a first-time failure to
     * establish it. Solis returns the machine to 100 % when the switch goes off,
     * so this counter being non-zero is a site problem, not a comms problem. */
    if (was_satisfied && !decision.satisfied) runtime->data.prerequisite_lost_count++;
    portEXIT_CRITICAL(&runtime->lock);

    if (was_satisfied && !decision.satisfied) {
        ESP_LOGE(TAG,
                 "%s prerequisite enable register no longer confirmed; the inverter is "
                 "removed from the commandable fleet until a read confirms it again",
                 runtime->config.name);
    }

    if (decision.action == INVERTER_PREREQ_ACTION_NONE) return;
    /* Rate-limit the I/O itself. Without this, a device that refuses the enable
     * write would be written to on every acquisition pass. */
    if ((int32_t)(timestamp - runtime->next_prerequisite_ms) < 0) return;
    runtime->next_prerequisite_ms = timestamp + recheck;

    if (decision.action == INVERTER_PREREQ_ACTION_WRITE) {
        esp_err_t err = write_prerequisite(runtime, timestamp);
        ESP_LOGW(TAG, "%s prerequisite enable register does not hold; write %s "
                      "(re-read will decide)",
                 runtime->config.name, err == ESP_OK ? "issued" : esp_err_to_name(err));
        /* No verdict is drawn from the write. The next pass re-reads, and only
         * that read may set prerequisite_satisfied. Scheduled immediately so the
         * confirming read is not delayed by a whole recheck period. */
        runtime->next_prerequisite_ms = timestamp;
        return;
    }

    (void)read_prerequisite(runtime, timestamp);
}

/*
 * Deferred write confirmation (P0-9), evaluated in the background acquisition
 * task from the readback poll_readback() has already taken. The control loop
 * and every HTTP handler are free of it.
 *
 * Returns true when the caller must drive this inverter to its safe fallback.
 * The decision itself is made by the pure evaluator in
 * inverter_write_confirmation.c; this function only assembles the evidence and
 * stores the outcome.
 */
static bool evaluate_write_confirmation(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    const bool readback_supported = profile && profile->has_power_limit_readback;
    const float tolerance = profile ? profile->readback_tolerance_percent : 0.0f;

    inverter_write_evidence_t evidence = {0};
    portENTER_CRITICAL(&runtime->lock);
    evidence.readback_supported = readback_supported;
    evidence.write_issued = runtime->data.write_issued;
    evidence.write_accepted = runtime->data.last_write_accepted;
    evidence.readback_valid = runtime->data.has_readback;
    /* Strictly after: a sample taken at or before the write proves nothing
     * about that write. */
    evidence.readback_after_write = runtime->data.has_readback &&
                                    runtime->data.last_readback_ms > runtime->data.last_write_ms;
    evidence.commanded_percent = runtime->data.requested_percent;
    evidence.readback_percent = runtime->data.readback_percent;
    evidence.tolerance_percent = tolerance;
    evidence.age_since_write_ms = runtime->data.write_issued
                                      ? timestamp - runtime->data.last_write_ms
                                      : 0U;
    /* A device-specific settle window overrides the firmware default. Clamped
     * below the deadline: a settle window at or past the deadline would leave a
     * disagreement permanently "pending" and an unconfirmed setpoint standing,
     * which is exactly what the deadline exists to prevent. */
    const uint32_t profile_settle_ms =
        runtime->profile ? runtime->profile->power_limit_settle_ms : 0U;
    uint32_t settle_ms = profile_settle_ms ? profile_settle_ms
                                           : (uint32_t)INVERTER_CONFIRMATION_SETTLE_MS;
    if (settle_ms >= (uint32_t)INVERTER_CONFIRMATION_DEADLINE_MS) {
        settle_ms = (uint32_t)INVERTER_CONFIRMATION_DEADLINE_MS - 1U;
    }
    evidence.settle_ms = settle_ms;
    evidence.deadline_ms = INVERTER_CONFIRMATION_DEADLINE_MS;

    /* Measured-power confirmation. The measured quantity IS the profile's
     * active-power telemetry, so no extra transaction is introduced: the
     * background poll that already runs is the confirmation source.
     *
     * The mode is taken from the profile AS DECLARED and is never downgraded to
     * NONE when the description is incomplete. A downgrade would silently fall
     * back to confirming on the setpoint readback, which for a stored-command
     * echo is the false confirmation the mode exists to prevent; the pure
     * evaluator refuses incomplete measured evidence instead. */
    evidence.measured_mode = profile ? profile->measured_power_confirm
                                     : INVERTER_MEASURED_CONFIRM_NONE;
    evidence.measured_valid = runtime->data.telemetry_valid && !runtime->data.telemetry_stale;
    /* Strictly after: a measurement taken at or before the write describes a
     * plant the write has since acted on. */
    evidence.measured_after_write = runtime->data.write_issued &&
                                    runtime->data.last_telemetry_ms > runtime->data.last_write_ms;
    evidence.measured_kw = runtime->data.measured_power_kw;
    evidence.baseline_valid = runtime->data.baseline_valid;
    evidence.baseline_before_write =
        runtime->data.baseline_valid &&
        runtime->data.baseline_sample_ms < runtime->data.last_write_ms;
    evidence.baseline_kw = runtime->data.baseline_power_kw;
    /* The capacity the commanded percentage refers to. For a plant-level endpoint
     * this configured rating MUST be the plant total; a wrong value makes every
     * measured verdict wrong, which is why it is a commissioning value. */
    evidence.capacity_kw = runtime->data.rated_power_kw;
    evidence.measured_tolerance_kw = profile ? profile->measured_tolerance_kw : 0.0f;
    evidence.measured_tolerance_percent_of_capacity =
        profile ? profile->measured_tolerance_percent_of_capacity : 0.0f;

    /* Scheduling-authority assertion, consulted only after a write. */
    evidence.authority_checked = inverter_profile_command_authority_described(profile);
    evidence.authority_valid = runtime->data.authority_read_valid;
    evidence.authority_after_write =
        runtime->data.write_issued &&
        runtime->data.last_authority_read_ms > runtime->data.last_write_ms;
    evidence.authority_holds = runtime->data.authority_holds;
    portEXIT_CRITICAL(&runtime->lock);

    inverter_write_verdict_t verdict = inverter_write_confirmation_evaluate(&evidence);

    portENTER_CRITICAL(&runtime->lock);
    /* Only a CHANGE of verdict is an event. This function runs on every pass of
     * the acquisition loop, so counting or restamping on every pass would inflate
     * the diagnostics and make a single stale command look perpetually fresh.
     * The safe-zero DEMAND is deliberately not gated on the change: it stands
     * until issue_safe_zero() actually gets a write accepted, so a failed safe
     * zero is retried on the next pass rather than dropped. */
    const bool changed = runtime->data.write_confirmation != (uint8_t)verdict.state;
    runtime->data.write_confirmation = (uint8_t)verdict.state;
    /* What the verdict rests on, so "confirmed" is never reported without it. */
    runtime->data.write_proof = (uint8_t)verdict.proof;
    runtime->data.limit_demonstrated = verdict.limit_demonstrated;
    if (verdict.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM && changed) {
        /* Measured output is below the commanded limit but was already below it
         * before the command, so the evidence cannot tell an honoured limit from
         * falling irradiance. Counted and reported; deliberately NOT a fault and
         * deliberately NOT a safe-zero, because driving PV to zero every time
         * irradiance dips below the commanded limit would be worse than the
         * ambiguity. If output ever rises ABOVE the limit the next pass returns
         * MISMATCHED and the safe fallback is demanded then. */
        runtime->data.ambiguous_count++;
    }
    const bool safe_zero_required = verdict.settled && verdict.requires_safe_zero &&
                                    !runtime->data.safe_zero_issued;

    if (changed && verdict.state == INVERTER_WRITE_CONFIRMED) {
        /* Only here does an issued write become a commanded value. */
        runtime->data.commanded_percent = runtime->data.requested_percent;
        runtime->data.commanded_power_kw =
            runtime->data.rated_power_kw * runtime->data.requested_percent / 100.0f;
        runtime->data.has_command = true;
        runtime->data.last_command_ms = timestamp;
        runtime->data.command_mismatch = false;
        runtime->data.confirmation_fault = false;
        runtime->data.confirmed_count++;
    } else if (verdict.settled && verdict.requires_safe_zero) {
        if (changed) {
            if (verdict.state == INVERTER_WRITE_MISMATCHED) {
                runtime->data.command_mismatch = true;
                runtime->data.mismatch_count++;
            } else {
                runtime->data.unverified_count++;
            }
        }
        runtime->data.confirmation_fault = true;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return safe_zero_required;
}

/* Collapse the operational state to UNKNOWN. Called for every non-positive
 * outcome: unconfigured register, failed read, unmapped raw value, stale
 * sample, or a telemetry failure that invalidates the whole inverter. */
static void mark_status_unknown(inverter_runtime_t *runtime, bool stale)
{
    runtime->data.status_state = INVERTER_STATE_UNKNOWN;
    runtime->data.status_raw_valid = false;
    runtime->data.status_stale = stale;
}

/* Operational status acquisition. This runs ONLY inside the background
 * telemetry task; HTTP handlers must never perform blocking Modbus
 * transactions. It is strictly read-only: function codes 3 and 4 only, and it
 * never issues a write of any kind. */
static esp_err_t poll_status(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || !inverter_status_register_is_configured(&profile->status_register)) {
        portENTER_CRITICAL(&runtime->lock);
        runtime->data.status_supported = false;
        mark_status_unknown(runtime, false);
        portEXIT_CRITICAL(&runtime->lock);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const inverter_status_register_t *status_register = &profile->status_register;
    if (!inverter_status_function_is_read_only(status_register->function)) {
        portENTER_CRITICAL(&runtime->lock);
        runtime->data.status_supported = true;
        runtime->data.status_last_error = ESP_ERR_NOT_SUPPORTED;
        mark_status_unknown(runtime, true);
        portEXIT_CRITICAL(&runtime->lock);
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t words[INVERTER_STATUS_MAX_WORDS] = {0};
    esp_err_t err = read_profile_block(runtime, status_register->function,
                                       status_register->address,
                                       status_register->words, words);
    uint32_t raw = 0;
    bool decoded = err == ESP_OK &&
                   inverter_status_decode_raw(status_register, words,
                                              status_register->words, &raw);
    if (err == ESP_OK && !decoded) err = ESP_ERR_INVALID_RESPONSE;

    /* A fresh successful read has age zero; evaluate() still applies the
     * mapping table and returns UNKNOWN for any unmapped raw value. */
    inverter_state_t state = inverter_status_evaluate(status_register, err == ESP_OK, raw, 0U,
                                                      profile_stale_ms(profile));

    portENTER_CRITICAL(&runtime->lock);
    runtime->data.status_supported = true;
    runtime->data.status_last_error = err;
    if (err == ESP_OK) {
        runtime->data.status_state = state;
        runtime->data.status_raw = raw;
        runtime->data.status_raw_valid = true;
        runtime->data.status_stale = false;
        runtime->data.last_status_ms = timestamp;
        runtime->data.status_read_successes++;
    } else {
        runtime->data.status_read_errors++;
        mark_status_unknown(runtime, true);
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

static void update_stale_state(inverter_runtime_t *runtime, uint32_t timestamp)
{
    portENTER_CRITICAL(&runtime->lock);
    if (runtime->data.telemetry_supported && runtime->data.telemetry_valid) {
        uint32_t age = timestamp - runtime->data.last_telemetry_ms;
        if (age > profile_stale_ms(runtime->profile)) {
            runtime->data.telemetry_stale = true;
            runtime->data.telemetry_valid = false;
            runtime->data.online = false;
            runtime->identity_checked = false;
            runtime->last_identity_ms = 0U;
            if (runtime->data.identity_supported) runtime->data.identity_verified = false;
            mark_status_unknown(runtime, true);
        }
    }
    /* Status ages out independently: a sample older than the profile stale
     * timeout is not evidence of anything and must read UNKNOWN. */
    if (runtime->data.status_supported) {
        uint32_t status_age = timestamp - runtime->data.last_status_ms;
        if (runtime->data.last_status_ms == 0U ||
            status_age > profile_stale_ms(runtime->profile)) {
            mark_status_unknown(runtime, true);
        }
    } else {
        mark_status_unknown(runtime, false);
    }
    portEXIT_CRITICAL(&runtime->lock);
}

/* Shortest interval this device documents between two power-limit commands.
 *
 * A profile may state its own. Otherwise a logger-gateway connection gets 1000 ms,
 * because the Huawei SmartLogger Modbus definitions state that the adjustment
 * value "should be issued at intervals of not less than 1 seconds", and this
 * controller's default control period is 250 ms -- four times faster. A direct
 * connection gets no limit, since no direct-inverter manual read for this project
 * documents one; that is an absence of evidence, not evidence of absence, and it
 * is on the commissioning checklist to confirm per site. */
static uint32_t min_command_interval_ms(const inverter_profile_t *profile)
{
    if (!profile) return 0U;
    if (profile->min_command_interval_ms) return profile->min_command_interval_ms;
    if (profile->connection == INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY) return 1000U;
    return 0U;
}

/* Snapshots this inverter's command history and asks the pure rule whether the
 * command must be withheld. The rule itself lives in inverter_write_confirmation
 * so host tests execute it; see inverter_command_rate_limited() for why a
 * reduction is never withheld. */
static bool command_is_rate_limited(inverter_runtime_t *runtime,
                                    const inverter_profile_t *profile,
                                    float percent, uint32_t timestamp)
{
    portENTER_CRITICAL(&runtime->lock);
    const bool written_before = runtime->data.write_issued;
    const uint32_t last_write_ms = runtime->data.last_write_ms;
    const float previous_percent = runtime->data.requested_percent;
    portEXIT_CRITICAL(&runtime->lock);

    return inverter_command_rate_limited(min_command_interval_ms(profile),
                                         written_before, last_write_ms,
                                         previous_percent, percent, timestamp);
}

/* Records a write that has just been issued. The value becomes the thing the
 * next readback is judged against - it does NOT become a commanded value. Only
 * evaluate_write_confirmation() may do that, and only on a matching readback. */
static void note_write_issued(inverter_runtime_t *runtime, float percent,
                              bool accepted, uint32_t timestamp)
{
    portENTER_CRITICAL(&runtime->lock);
    /* Capture the pre-command measured output BEFORE anything else, because after
     * this write there is no way to recover it, and for a profile confirming on
     * measured power it is the difference between demonstrating a limit and
     * guessing: a measurement below the commanded limit is equally consistent
     * with the limit being honoured and with the sun going in. Only a fall from
     * ABOVE the new limit to at-or-below it proves anything.
     *
     * Only a live, non-stale telemetry sample counts, and its own timestamp is
     * kept rather than this write's, so the evaluator can insist the baseline
     * genuinely predates the command. */
    runtime->data.baseline_valid = runtime->data.telemetry_valid &&
                                   !runtime->data.telemetry_stale &&
                                   runtime->data.last_telemetry_ms != 0U &&
                                   runtime->data.last_telemetry_ms < timestamp;
    runtime->data.baseline_power_kw = runtime->data.measured_power_kw;
    runtime->data.baseline_sample_ms = runtime->data.last_telemetry_ms;
    runtime->data.requested_percent = percent;
    runtime->data.write_issued = true;
    runtime->data.last_write_accepted = accepted;
    runtime->data.last_write_ms = timestamp;
    runtime->data.write_confirmation = (uint8_t)(accepted ? INVERTER_WRITE_PENDING
                                                          : INVERTER_WRITE_UNVERIFIED);
    /* A new command has proved nothing yet, so no proof from the previous one may
     * carry over into it. */
    runtime->data.write_proof = (uint8_t)INVERTER_WRITE_PROOF_NONE;
    runtime->data.limit_demonstrated = false;
    /* A new write opens a new confirmation window, so a previously issued safe
     * zero no longer suppresses the next one. issue_safe_zero() re-latches it
     * immediately afterwards, and only when its own write was accepted. */
    runtime->data.safe_zero_issued = false;
    if (accepted) runtime->data.write_successes++;
    else runtime->data.write_errors++;
    if (!accepted) runtime->data.confirmation_fault = true;
    portEXIT_CRITICAL(&runtime->lock);
}

/*
 * Drives one inverter to its safe fallback. A single Modbus write: no sleep and
 * no readback transaction, so it stays cheap wherever it is called from. The
 * confirmation of the zero rides the ordinary background readback like any
 * other write.
 */
static esp_err_t issue_safe_zero(inverter_runtime_t *runtime)
{
    const inverter_profile_t *profile = runtime->profile;
    uint16_t words[2] = {0};
    uint8_t word_count = 0;
    esp_err_t err = encode_command(profile, INVERTER_SAFE_FALLBACK_PERCENT,
                                   words, &word_count);
    if (err == ESP_OK) err = write_profile_command(runtime, profile, words, word_count);

    uint32_t timestamp = now_ms();
    note_write_issued(runtime, INVERTER_SAFE_FALLBACK_PERCENT, err == ESP_OK, timestamp);
    portENTER_CRITICAL(&runtime->lock);
    /* Latched until a readback actually confirms the safe value. */
    runtime->data.confirmation_fault = true;
    /* Suppress further safe-zero attempts only if this one actually reached the
     * device. A safe zero the transport rejected must be retried on the next
     * pass, not silently considered done. */
    runtime->data.safe_zero_issued = err == ESP_OK;
    if (err != ESP_OK) runtime->data.last_error = err;
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

static void inverter_telemetry_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint32_t timestamp = now_ms();
        for (uint8_t i = 0; i < s_inverter_count; ++i) {
            inverter_runtime_t *runtime = &s_inverters[i];
            if (!runtime->config.enabled || !runtime->data.connection_initialized ||
                !runtime->profile || !inverter_profile_allows_read(runtime->profile)) {
                continue;
            }
            update_stale_state(runtime, timestamp);

            /* Prerequisite enable sequencing. Evaluated on every pass and before
             * the poll gate for the same reason as the write confirmation below:
             * a sample must be able to EXPIRE on an inverter that has gone quiet
             * or failed its identity check while holding a setpoint. Losing the
             * enable switch silently is the failure being prevented, and it does
             * not announce itself. At most one Modbus transaction per pass, rate
             * limited to the recheck period, and never on the control path. */
            service_prerequisite(runtime, timestamp);

            /* Deferred write confirmation (P0-9). Evaluated on every pass of the
             * acquisition loop rather than only on a poll tick, and before any
             * gate that can `continue`, so the confirmation deadline still
             * expires on an inverter that has gone silent or failed its identity
             * check while holding a setpoint. The evaluation itself is a pure
             * function over already-acquired state; only the safe zero it may
             * demand touches Modbus, and that is one write with no sleep. */
            if (evaluate_write_confirmation(runtime, timestamp)) {
                esp_err_t zero_err = issue_safe_zero(runtime);
                ESP_LOGE(TAG,
                         "inverter %u setpoint could not be confirmed (%s); safe zero %s",
                         i, inverter_write_state_name(
                                (inverter_write_state_t)runtime->data.write_confirmation),
                         zero_err == ESP_OK ? "issued" : esp_err_to_name(zero_err));
            }

            if ((int32_t)(timestamp - runtime->next_poll_ms) < 0) continue;
            runtime->next_poll_ms = timestamp + profile_poll_ms(runtime->profile);

            if (!identity_is_current(runtime, timestamp) && verify_identity(runtime) != ESP_OK) {
                portENTER_CRITICAL(&runtime->lock);
                runtime->data.online = false;
                runtime->data.telemetry_valid = false;
                runtime->data.telemetry_stale = true;
                mark_status_unknown(runtime, true);
                portEXIT_CRITICAL(&runtime->lock);
                continue;
            }

            esp_err_t telemetry_err = poll_active_power(runtime, timestamp);
            if (telemetry_err == ESP_OK && runtime->profile->has_power_limit_readback) {
                (void)poll_readback(runtime, timestamp);
            }
            /* Who owns scheduling of this target. Read-only, and only for a
             * profile that describes it, so no existing profile gains a
             * transaction. It rides the ordinary telemetry cadence like every
             * other read: the confirmation evaluator needs a sample taken after
             * the write, not a synchronous one. */
            if (telemetry_err == ESP_OK &&
                inverter_profile_command_authority_described(runtime->profile)) {
                (void)poll_command_authority(runtime, timestamp);
            }
            if (telemetry_err == ESP_OK) {
                (void)poll_status(runtime, timestamp);
                /* And the measurement block, at most once a second: it is a
                 * hundred registers that no control decision reads, so it must
                 * not ride the control cadence. Its return value is discarded on
                 * purpose -- a failure here is not an inverter fault, and
                 * treating it as one would make adding a monitoring page a
                 * reliability regression. */
                if ((int32_t)(timestamp - runtime->next_measurements_ms) >= 0) {
                    runtime->next_measurements_ms =
                        timestamp + INVERTER_MEASUREMENT_BLOCK_INTERVAL_MS;
                    (void)poll_measurements(runtime, timestamp);
                }
            } else {
                portENTER_CRITICAL(&runtime->lock);
                mark_status_unknown(runtime, true);
                portEXIT_CRITICAL(&runtime->lock);
            }
        }
        recompute_commandable_capacity();
        vTaskDelay(pdMS_TO_TICKS(INVERTER_TELEMETRY_IDLE_MS));
    }
}

esp_err_t inverter_manager_init(void)
{
    ESP_RETURN_ON_ERROR(inverter_profile_store_init(), TAG,
                        "inverter profile assignment store unavailable");

    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_inverter_count = cfg->inverter_count <= APP_MAX_INVERTERS
                           ? cfg->inverter_count
                           : APP_MAX_INVERTERS;
    portENTER_CRITICAL(&s_capacity_lock);
    s_total_rated_kw = 0.0f;
    portEXIT_CRITICAL(&s_capacity_lock);

    esp_err_t first_error = ESP_OK;
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->config = cfg->inverters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->data.rated_power_kw = runtime->config.rated_power_kw;

        char profile_id[INVERTER_PROFILE_ID_MAX] = {0};
        if (inverter_profile_store_get(i, profile_id, sizeof(profile_id)) != ESP_OK) {
            strlcpy(profile_id, DEFAULT_PROFILE_ID, sizeof(profile_id));
        }
        runtime->profile = inverter_profiles_find(profile_id);
        if (!runtime->profile) runtime->profile = inverter_profiles_find(DEFAULT_PROFILE_ID);
        /* Fail closed if the declaration cannot be read: an unreadable store
         * means "not a lab target", never "assume it is one". */
        bool lab_target = false;
        if (inverter_profile_store_lab_target_get(i, &lab_target) != ESP_OK) lab_target = false;
        runtime->lab_target = lab_target;
        runtime->permission = inverter_profile_write_permission(runtime->profile, lab_target);
        runtime->write_allowed = runtime->permission != INVERTER_WRITE_FORBIDDEN;
        runtime->data.identity_supported = runtime->profile && runtime->profile->has_identity_probe;
        runtime->data.telemetry_supported = runtime->profile && runtime->profile->has_active_power;
        runtime->data.status_supported = inverter_profile_has_status_register(runtime->profile);
        runtime->data.status_state = INVERTER_STATE_UNKNOWN;
        runtime->data.status_raw_valid = false;
        runtime->data.status_stale = runtime->data.status_supported;
        runtime->next_poll_ms = now_ms();
        runtime->next_prerequisite_ms = runtime->next_poll_ms;
        /* An inverter that needs a prerequisite starts NOT satisfied and must earn
         * it with a read. One that needs none is satisfied by definition, and
         * saying so here keeps the eligibility test a single boolean rather than a
         * pair of conditions that could get out of step. */
        runtime->data.prerequisite_required =
            runtime->profile && runtime->profile->requires_prerequisite_enable;
        runtime->data.prerequisite_describable =
            inverter_profile_prerequisite_write_described(runtime->profile) &&
            inverter_profile_prerequisite_readback_described(runtime->profile);
        runtime->data.prerequisite_unverifiable =
            inverter_profile_prerequisite_blocks_write(runtime->profile);
        runtime->data.prerequisite_satisfied = !runtime->data.prerequisite_required;

        if (!runtime->config.enabled) continue;
        if (!isfinite(runtime->config.rated_power_kw) || runtime->config.rated_power_kw <= 0.0f) {
            runtime->data.last_error = ESP_ERR_INVALID_ARG;
            if (first_error == ESP_OK) first_error = ESP_ERR_INVALID_ARG;
            continue;
        }
        runtime->io_mutex = xSemaphoreCreateMutex();
        if (!runtime->io_mutex) {
            runtime->data.last_error = ESP_ERR_NO_MEM;
            if (first_error == ESP_OK) first_error = ESP_ERR_NO_MEM;
            continue;
        }

        esp_err_t init_err = modbus_tcp_connection_init(&runtime->connection, &runtime->config.endpoint);
        if (init_err != ESP_OK) {
            runtime->data.last_error = init_err;
            if (first_error == ESP_OK) first_error = init_err;
            ESP_LOGE(TAG, "inverter %u connection init failed: %s", i, esp_err_to_name(init_err));
            continue;
        }
        runtime->data.connection_initialized = true;

        const char *resolved_profile_id = runtime->profile ? runtime->profile->id : "missing";
        if (runtime->permission == INVERTER_WRITE_FORBIDDEN &&
            inverter_profile_prerequisite_blocks_write(runtime->profile)) {
            /* Distinguished from the ordinary refusal, because the remedy is
             * completely different: this one needs a manual citation for an
             * enable register and its readback, not a qualification decision. */
            ESP_LOGE(TAG,
                     "inverter %u profile '%s' needs a prerequisite enable register that the "
                     "profile does not describe as writable AND readable; commanding it would "
                     "report a CONFIRMED limit the inverter ignores, so the command path "
                     "remains locked", i, resolved_profile_id);
        } else if (runtime->permission == INVERTER_WRITE_FORBIDDEN) {
            ESP_LOGW(TAG,
                     "inverter %u profile '%s' is not production-approved and no simulator has "
                     "been declared; command path remains locked", i, resolved_profile_id);
        } else if (runtime->permission == INVERTER_WRITE_LAB_ONLY) {
            /* Logged at warning level on every start, deliberately: a controller
             * commanding a declared simulator must never look like a controller
             * commanding a plant. */
            ESP_LOGW(TAG,
                     "inverter %u profile '%s' is commandable ONLY because its endpoint is "
                     "declared a lab simulator. This is not production control and is not "
                     "evidence about physical equipment.", i, resolved_profile_id);
        }
    }
    free(cfg);
    s_fleet_resolved = true;

    if (!s_telemetry_task) {
        BaseType_t created = xTaskCreate(inverter_telemetry_task, "inv_telemetry",
                                         INVERTER_TELEMETRY_TASK_STACK, NULL,
                                         INVERTER_TELEMETRY_TASK_PRIORITY,
                                         &s_telemetry_task);
        if (created != pdPASS) return ESP_ERR_NO_MEM;
    }
    return first_error;
}

uint8_t inverter_manager_get_count(void)
{
    return s_inverter_count;
}

void inverter_manager_commissioning_summary(inverter_fleet_commissioning_t *out_summary)
{
    if (!out_summary) return;
    /* Zeroed first, so an early return leaves the caller with known == false and
     * every count at zero - the fail-closed answer. */
    memset(out_summary, 0, sizeof(*out_summary));
    if (!s_fleet_resolved) return;

    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        if (!runtime->config.enabled) continue;
        out_summary->enabled_count++;
        const inverter_profile_t *profile = runtime->profile;
        if (profile && profile->has_power_limit_readback) {
            out_summary->readback_capable_count++;
        }
        /* Counted BEFORE the permission switch, because the FORBIDDEN case
         * continues -- and a prerequisite the profile cannot describe is exactly
         * why an inverter is forbidden. Omitting it here would hide the only
         * count that explains the refusal. */
        if (profile && profile->requires_prerequisite_enable) {
            out_summary->prerequisite_required_count++;
            if (inverter_profile_prerequisite_blocks_write(profile)) {
                out_summary->prerequisite_unverifiable_count++;
            }
        }
        portENTER_CRITICAL(&runtime->lock);
        const bool prerequisite_satisfied = runtime->data.prerequisite_satisfied;
        portEXIT_CRITICAL(&runtime->lock);
        if (profile && profile->requires_prerequisite_enable && !prerequisite_satisfied) {
            out_summary->prerequisite_unconfirmed_count++;
        }
        switch (runtime->permission) {
            case INVERTER_WRITE_PRODUCTION:
                out_summary->write_qualified_count++;
                break;
            case INVERTER_WRITE_LAB_ONLY:
                out_summary->lab_only_count++;
                out_summary->lab_mode = true;
                break;
            case INVERTER_WRITE_FORBIDDEN:
            default:
                continue;
        }
        if (isfinite(runtime->config.rated_power_kw) &&
            runtime->config.rated_power_kw > 0.0f) {
            out_summary->commissioned_capacity_kw += runtime->config.rated_power_kw;
        }
    }
    out_summary->known = true;
}

float inverter_manager_get_total_rated_kw(void)
{
    portENTER_CRITICAL(&s_capacity_lock);
    float total = s_total_rated_kw;
    portEXIT_CRITICAL(&s_capacity_lock);
    return total;
}

static esp_err_t encode_command(const inverter_profile_t *profile, float percent,
                                uint16_t *words, uint8_t *word_count)
{
    if (!profile || !words || !word_count || !isfinite(percent) || percent < 0.0f ||
        !isfinite(profile->raw_units_per_percent) || profile->raw_units_per_percent <= 0.0f ||
        !isfinite(profile->minimum_percent) || !isfinite(profile->maximum_percent) ||
        profile->minimum_percent < 0.0f || profile->maximum_percent < profile->minimum_percent ||
        (profile->power_limit_words != 1U && profile->power_limit_words != 2U)) {
        return ESP_ERR_INVALID_ARG;
    }

    double raw_value = (double)percent * (double)profile->raw_units_per_percent;
    if (!isfinite(raw_value) || raw_value < 0.0) return ESP_ERR_INVALID_ARG;

    /*
     * Which encoding the device's register actually uses. Only FLOAT32 is taken
     * from the profile: for an integer command the width has always decided the
     * range, and every profile written before power_limit_type existed leaves it
     * zero, so deriving the integer type from the width keeps them all encoding
     * byte-for-byte as before.
     *
     * A float register cannot be inferred from the width. U32 and Float32 are both
     * two registers wide, and 50 % encoded as the integer 50 lands in a Float32
     * register as 7e-44 -- effectively zero output -- while the readback decodes
     * the same two registers the same wrong way, agrees with the request, and
     * reports the command CONFIRMED. That is the single worst outcome this module
     * can produce, and it is why the type is declared per profile from the manual
     * rather than guessed here.
     */
    inverter_value_type_t type = profile->power_limit_type == INVERTER_VALUE_FLOAT32
        ? INVERTER_VALUE_FLOAT32
        : (profile->power_limit_words == 1U ? INVERTER_VALUE_U16 : INVERTER_VALUE_U32);

    /* A float register is two registers that must be written together, and Modbus
     * has no way to write two registers with function 0x06. Refusing here rather
     * than in the transport keeps the failure at plan time, before any inverter in
     * the fleet has been written. */
    if (type == INVERTER_VALUE_FLOAT32 &&
        (profile->power_limit_words != 2U || profile->power_limit_function != 16U)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = inverter_profile_encode_value(raw_value, type,
                                                  profile->power_limit_word_order,
                                                  profile->power_limit_words, words);
    if (err != ESP_OK) return err;
    *word_count = profile->power_limit_words;
    return ESP_OK;
}

static esp_err_t write_profile_command(inverter_runtime_t *runtime,
                                       const inverter_profile_t *profile,
                                       const uint16_t *words,
                                       uint8_t word_count)
{
    if (!runtime || !profile || !words || !runtime->io_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(runtime->io_mutex,
                       pdMS_TO_TICKS(runtime->config.endpoint.timeout_ms + 100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err;
    if (profile->power_limit_function == 16U) {
        err = modbus_tcp_write_multiple(&runtime->connection,
                                        profile->power_limit_address,
                                        words, word_count);
    } else if (profile->power_limit_function == 6U && word_count == 1U) {
        err = modbus_tcp_write_single(&runtime->connection,
                                      profile->power_limit_address,
                                      words[0]);
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreGive(runtime->io_mutex);
    return err;
}

/*
 * Drives every inverter already written in this fleet plan to its safe
 * fallback. One Modbus write each, no sleep and no readback transaction: this
 * runs on the control task's cycle and must not extend it.
 *
 * "Safe" here means the safe value was ISSUED and the transport accepted it.
 * Whether it actually took effect is confirmed later by the background readback
 * like any other write - which is exactly why issue_safe_zero() latches
 * confirmation_fault until that confirmation arrives.
 */
static bool rollback_targets(command_target_t *targets, uint8_t count)
{
    bool all_safe = true;
    for (uint8_t index = 0; index < count; ++index) {
        command_target_t *target = &targets[index];
        if (issue_safe_zero(target->runtime) != ESP_OK) all_safe = false;
        invalidate_identity(target->runtime);
    }
    return all_safe;
}

esp_err_t inverter_manager_set_total_power_kw(float target_kw)
{
    if (!isfinite(target_kw) || target_kw < 0.0f) return ESP_ERR_INVALID_ARG;

    command_target_t targets[APP_MAX_INVERTERS] = {0};
    uint8_t target_count = 0;
    float total_rated_kw = 0.0f;
    uint32_t timestamp = now_ms();

    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        bool eligible = runtime->config.enabled && runtime->data.connection_initialized &&
                        runtime->write_allowed && runtime->data.online &&
                        runtime->data.telemetry_valid && !runtime->data.telemetry_stale &&
                        !runtime->data.confirmation_fault &&
                        /* Reading a flag the background task maintains. No
                         * transaction is added to the control path. */
                        runtime->data.prerequisite_satisfied &&
                        identity_is_current(runtime, timestamp) &&
                        isfinite(runtime->config.rated_power_kw) &&
                        runtime->config.rated_power_kw > 0.0f;
        float rated = runtime->config.rated_power_kw;
        const inverter_profile_t *profile = runtime->profile;
        portEXIT_CRITICAL(&runtime->lock);
        if (!eligible || !profile || target_count >= APP_MAX_INVERTERS) continue;
        targets[target_count++] = (command_target_t){
            .runtime = runtime,
            .profile = profile,
            .rated_kw = rated,
        };
        total_rated_kw += rated;
    }

    if (target_count == 0 || !isfinite(total_rated_kw) || total_rated_kw <= 0.0f) {
        /*
         * THROTTLED, BECAUSE THIS IS A STANDING CONDITION AND NOT AN EVENT.
         *
         * The control loop asks for a command every cycle, so on a plant with no
         * qualified inverter -- which is every plant before commissioning
         * finishes -- this fired four times a second and buried the boot log.
         * Observed on the bench 2026-08-01: the Wi-Fi association failures and
         * the meter's route error were both scrolled off the console by it,
         * which are the two messages an engineer is actually looking for.
         *
         * A message that appears too often stops being read, and then the
         * absence of a message stops meaning anything. So: the first refusal
         * after any change, then one every thirtieth, which is roughly every
         * eight seconds at the control rate. `first` counts refusals rather than
         * timing them, so the cadence follows the loop rather than the clock.
         */
        if (s_command_refusals++ % 30U == 0U) {
            /* One string literal, deliberately. Splitting it across a
             * concatenation is identical to the compiler and invisible to
             * tests/inverter_runtime_write_gate_source_contract.py, which greps
             * the source for this exact refusal -- and the point of that
             * contract is that the refusal cannot be quietly removed. */
            ESP_LOGW(TAG, "power command rejected: no online production-approved inverter profile is commandable (refusal %u)",
                     (unsigned)s_command_refusals);
        }
        return ESP_ERR_INVALID_STATE;
    }
    /* A command got through, so the next refusal is news again and is logged
     * immediately rather than waiting out the remainder of a throttle window. */
    s_command_refusals = 0;
    if (target_kw > total_rated_kw) target_kw = total_rated_kw;

    /* Build and validate the complete immutable fleet plan before issuing the
     * first physical write. This prevents partial commands caused by a later
     * scaling, range, width, or aggregate-cap failure. */
    float planned_total_kw = 0.0f;
    for (uint8_t i = 0; i < target_count; ++i) {
        command_target_t *target = &targets[i];
        float share_kw = target_kw * target->rated_kw / total_rated_kw;
        float percent = target_kw <= 0.0f ? 0.0f : 100.0f * share_kw / target->rated_kw;
        if (!isfinite(percent)) return ESP_ERR_INVALID_ARG;
        if (percent > 0.0f && percent < target->profile->minimum_percent) percent = 0.0f;
        if (percent > target->profile->maximum_percent) percent = target->profile->maximum_percent;
        float commanded_kw = target->rated_kw * percent / 100.0f;
        if (!isfinite(commanded_kw) || planned_total_kw + commanded_kw > target_kw + 0.01f) {
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t encode_error = encode_command(target->profile, percent,
                                                target->words, &target->word_count);
        if (encode_error != ESP_OK) return encode_error;
        target->percent = percent;
        target->commanded_kw = commanded_kw;
        planned_total_kw += commanded_kw;
    }

    /*
     * ISSUE ONLY. No sleep, no readback transaction, no second round trip.
     *
     * This function runs on the control task. Everything it does here is on the
     * control period, so the only Modbus traffic it may generate is the write
     * itself. Confirmation of that write is deferred to the background
     * acquisition task (see evaluate_write_confirmation), which judges the
     * readback it already polls against the value recorded by
     * note_write_issued().
     *
     * ESP_OK therefore means "the write was issued and the transport accepted
     * it" - NOT "the setpoint took effect". The caller must read the
     * confirmation state for that, and until a readback confirms it the value
     * is reported as pending, never as commanded.
     */
    for (uint8_t i = 0; i < target_count; ++i) {
        command_target_t *target = &targets[i];
        esp_err_t write_error = ESP_ERR_INVALID_RESPONSE;

        if (command_is_rate_limited(target->runtime, target->profile, target->percent,
                                    now_ms())) {
            /* Deliberately no note_write_issued(): nothing was sent, so the
             * previous setpoint and its confirmation window still stand. Writing
             * here would reset the confirmation window for a command that never
             * left the controller. */
            continue;
        }

        /* Transport retries only, back to back. A retry costs one round trip;
         * it never costs a sleep. */
        for (uint8_t attempt = 1; attempt <= INVERTER_COMMAND_MAX_ATTEMPTS; ++attempt) {
            write_error = write_profile_command(target->runtime, target->profile,
                                                target->words, target->word_count);
            if (write_error == ESP_OK) break;
        }

        note_write_issued(target->runtime, target->percent, write_error == ESP_OK,
                          now_ms());
        portENTER_CRITICAL(&target->runtime->lock);
        target->runtime->data.last_error = write_error;
        portEXIT_CRITICAL(&target->runtime->lock);

        if (write_error != ESP_OK) {
            /* Everything written so far in this plan goes safe immediately, in
             * this call, so a half-applied fleet never outlives the cycle. */
            bool rollback_issued = rollback_targets(targets, (uint8_t)(i + 1U));
            ESP_LOGE(TAG, "%s command write failed: %s; safe zero %s",
                     target->runtime->config.name, esp_err_to_name(write_error),
                     rollback_issued ? "issued" : "failed");
            return rollback_issued ? write_error : ESP_FAIL;
        }
    }
    return ESP_OK;
}

inverter_write_state_t inverter_manager_fleet_write_confirmation(void)
{
    inverter_write_state_t states[APP_MAX_INVERTERS];
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_inverter_count && count < APP_MAX_INVERTERS; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        bool considered = runtime->config.enabled && runtime->write_allowed &&
                          runtime->data.write_issued;
        inverter_write_state_t state = (inverter_write_state_t)runtime->data.write_confirmation;
        portEXIT_CRITICAL(&runtime->lock);
        if (considered) states[count++] = state;
    }
    /* No inverter has ever been written to, so there is nothing outstanding.
     * This is the one case that is legitimately not a confirmation failure. */
    if (count == 0U) return INVERTER_WRITE_CONFIRMED;
    return inverter_write_state_worst(states, count);
}

bool inverter_manager_write_confirmation_fault(void)
{
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        bool fault = runtime->config.enabled && runtime->data.confirmation_fault;
        portEXIT_CRITICAL(&runtime->lock);
        if (fault) return true;
    }
    return false;
}

bool inverter_manager_prerequisite_enable_fault(void)
{
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        /* Only reportable for a device that actually needs one. An inverter with
         * no prerequisite must never contribute to this fault, or the operator
         * would be sent looking for a register that does not exist. */
        bool fault = runtime->config.enabled && runtime->data.prerequisite_required &&
                     !runtime->data.prerequisite_satisfied;
        portEXIT_CRITICAL(&runtime->lock);
        if (fault) return true;
    }
    return false;
}

esp_err_t inverter_manager_probe_read_only(uint8_t inverter_index,
                                           inverter_probe_result_t *result)
{
    if (!result || inverter_index >= s_inverter_count) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    inverter_runtime_t *runtime = &s_inverters[inverter_index];
    result->connection_initialized = runtime->data.connection_initialized;
    result->profile_read_allowed = inverter_profile_allows_read(runtime->profile);

    if (!runtime->config.enabled || !runtime->data.connection_initialized) return ESP_ERR_INVALID_STATE;
    if (!result->profile_read_allowed || !runtime->profile) return ESP_ERR_NOT_SUPPORTED;

    bool attempted = false;
    esp_err_t final_error = ESP_OK;

    if (runtime->profile->has_identity_probe) {
        attempted = true;
        result->identity_attempted = true;
        result->identity_count = runtime->profile->identity_words;
        if (result->identity_count > INVERTER_PROBE_MAX_REGISTERS) {
            result->identity_count = INVERTER_PROBE_MAX_REGISTERS;
        }
        result->identity_error = read_profile_block(runtime,
            runtime->profile->identity_function,
            runtime->profile->identity_address,
            result->identity_count,
            result->identity_registers);
        result->identity_ok = result->identity_error == ESP_OK &&
            identity_matches(runtime->profile, result->identity_registers,
                             result->identity_count);
        if (!result->identity_ok) {
            final_error = result->identity_error == ESP_OK
                ? ESP_ERR_INVALID_RESPONSE : result->identity_error;
        }
    }

    if (runtime->profile->has_active_power) {
        attempted = true;
        result->active_power_attempted = true;
        result->active_power_count = runtime->profile->active_power_words;
        if (result->active_power_count > INVERTER_PROBE_MAX_REGISTERS) {
            result->active_power_count = INVERTER_PROBE_MAX_REGISTERS;
        }
        result->active_power_error = read_profile_block(runtime,
            runtime->profile->active_power_function,
            runtime->profile->active_power_address,
            result->active_power_count,
            result->active_power_registers);
        result->active_power_ok = result->active_power_error == ESP_OK;
        if (!result->active_power_ok && final_error == ESP_OK) {
            final_error = result->active_power_error;
        }
    }

    if (!attempted) return ESP_ERR_NOT_SUPPORTED;
    return final_error;
}

bool inverter_manager_get_data(uint8_t index, inverter_data_t *out_data)
{
    if (!out_data || index >= s_inverter_count) return false;
    inverter_runtime_t *runtime = &s_inverters[index];
    portENTER_CRITICAL(&runtime->lock);
    *out_data = runtime->data;
    portEXIT_CRITICAL(&runtime->lock);
    return true;
}

/*
 * Fleet synchronisation predicate for the control engine.
 *
 * Returns true ONLY when at least one inverter is enabled and every enabled
 * inverter reports INVERTER_STATE_ON_GRID from a sample that is neither stale
 * nor missing. Absent status configuration, a failed read, an unmapped raw
 * value and a stale sample all present as UNKNOWN and therefore return false.
 * This is a read of already-acquired state; it performs no Modbus I/O and is
 * safe to call from any task.
 */
bool inverter_manager_fleet_synchronised(void)
{
    inverter_status_sample_t samples[APP_MAX_INVERTERS] = {0};
    uint32_t timestamp = now_ms();
    uint8_t count = 0;

    for (uint8_t i = 0; i < s_inverter_count && count < APP_MAX_INVERTERS; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        uint32_t stale_ms = profile_stale_ms(runtime->profile);
        portENTER_CRITICAL(&runtime->lock);
        bool enabled = runtime->config.enabled;
        bool fresh = runtime->data.status_supported && !runtime->data.status_stale &&
                     runtime->data.last_status_ms != 0U &&
                     (timestamp - runtime->data.last_status_ms) <= stale_ms;
        inverter_state_t state = fresh ? runtime->data.status_state : INVERTER_STATE_UNKNOWN;
        portEXIT_CRITICAL(&runtime->lock);
        samples[count++] = (inverter_status_sample_t){
            .enabled = enabled,
            .sample_fresh = fresh,
            .state = state,
        };
    }
    return inverter_status_fleet_synchronised(samples, count);
}
