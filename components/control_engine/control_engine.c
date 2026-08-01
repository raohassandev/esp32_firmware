#include "control_engine.h"

#include <math.h>
#include <stdlib.h>

#include "commissioning_gate.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "generator_fleet_limit.h"
#include "grid_control_gate.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "phase_selection.h"
#include "power_control_policy.h"
#include "safety_manager.h"
#include "solar_grid_config.h"
#include "source_detection.h"
#include "source_mode.h"

static const char *TAG = "control";
static control_config_t s_config;
static solar_grid_config_t s_grid_config;
static control_status_t s_status;
static bool s_runtime_forced_disabled;
static bool s_safe_zero_pending;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    bool configured;
    bool grid_available;
    bool grid_breaker_closed;
    uint16_t grid_available_raw;
    uint16_t grid_breaker_raw;
    uint32_t last_attempt_ms;
    uint32_t last_update_ms;
    uint32_t success_count;
    uint32_t error_count;
    esp_err_t last_error;
} grid_evidence_runtime_t;

static grid_evidence_runtime_t s_evidence;
static meter_role_assignment_t s_roles;

/*
 * COMMISSIONING GATE (P0-6).
 *
 * s_commissioning_inputs holds the parts of the evidence that come from
 * persisted configuration. They are resolved once in control_engine_init(),
 * because meter and inverter configuration changes already require a restart
 * and an app_config_t must never go on the control task's 4 kB stack.
 *
 * It is deliberately NOT pre-initialised to anything permissive. Until init
 * fills it in, every `known` flag is false and the gate evaluates to "not
 * commissioned" with reason "state unreadable", which is the correct answer for
 * a controller that has not yet read its own configuration. If init returns
 * early on an error, it STAYS that way and automatic control cannot engage.
 */
static commissioning_inputs_t s_commissioning_inputs;
static commissioning_status_t s_commissioning;
static bool s_commissioning_valid;
static portMUX_TYPE s_commissioning_lock = portMUX_INITIALIZER_UNLOCKED;

/* The generator fleet verdict the CONTROL LOOP actually acted on, published so an
 * engineer can see the floor that inhibited PV rather than a floor recomputed
 * elsewhere from configuration.
 *
 * That distinction is the whole reason this exists. A second computation over the
 * commissioned set answers a different question -- "what would the floor be if every
 * in-service engine were on the bus" -- and presenting it as the runtime answer would
 * misreport why the plant is being held down. `valid` stays false until the loop has
 * evaluated once, so a caller can tell "no verdict yet" from "a verdict of zero". */
static generator_fleet_limit_t s_fleet_limit;
static bool s_fleet_limit_valid;
static portMUX_TYPE s_fleet_lock = portMUX_INITIALIZER_UNLOCKED;

static void store_fleet_limit(const generator_fleet_limit_t *limit)
{
    portENTER_CRITICAL(&s_fleet_lock);
    s_fleet_limit = *limit;
    s_fleet_limit_valid = true;
    portEXIT_CRITICAL(&s_fleet_lock);
}

bool control_engine_get_generator_fleet(generator_fleet_limit_t *out_limit)
{
    if (!out_limit) return false;
    portENTER_CRITICAL(&s_fleet_lock);
    const bool valid = s_fleet_limit_valid;
    *out_limit = s_fleet_limit;
    portEXIT_CRITICAL(&s_fleet_lock);
    return valid;
}


/* The commissioned basis of the meter holding the GRID role. Read from the
 * snapshot taken at init, like every other meter fact this loop uses -- meter
 * configuration changes already require a restart. Defaults to the stricter
 * basis when no grid meter is resolved, which costs nothing: with no meter there
 * is no measurement and the freshness gate already holds PV down. */
static uint32_t s_grid_phase_basis = METER_PHASE_BASIS_LOWEST_PHASE;

static uint32_t grid_phase_basis(void)
{
    return s_grid_phase_basis;
}

/* The selection the loop last acted on, published so an engineer can see WHICH
 * conductor a limit is being enforced on, and whether per-phase control is
 * actually in force or has fallen back to the total.
 *
 * HMI-EVIDENCE: "enforced on L2 at -20 kW while the total reads +90 kW" is the
 * single sentence that explains a curtailment nobody looking at the total can
 * account for. Without it an operator sees PV held down on a site that appears
 * to be importing comfortably, and concludes the controller is broken. */
static phase_selection_t s_phase_selection;
static bool s_phase_selection_valid;
static portMUX_TYPE s_phase_lock = portMUX_INITIALIZER_UNLOCKED;

static void store_phase_selection(const phase_selection_t *selection)
{
    portENTER_CRITICAL(&s_phase_lock);
    s_phase_selection = *selection;
    s_phase_selection_valid = true;
    portEXIT_CRITICAL(&s_phase_lock);
}

bool control_engine_get_phase_selection(phase_selection_t *out_selection)
{
    if (!out_selection) return false;
    portENTER_CRITICAL(&s_phase_lock);
    const bool valid = s_phase_selection_valid;
    *out_selection = s_phase_selection;
    portEXIT_CRITICAL(&s_phase_lock);
    return valid;
}

static void store_commissioning(const commissioning_status_t *status)
{
    portENTER_CRITICAL(&s_commissioning_lock);
    s_commissioning = *status;
    s_commissioning_valid = true;
    portEXIT_CRITICAL(&s_commissioning_lock);
}

/* Completes the evidence with the parts that can change while running, then
 * evaluates. Called every control cycle; the evaluation is a pure function over
 * already-acquired state and performs no I/O. */
static commissioning_status_t evaluate_commissioning(bool source_detection_configured)
{
    commissioning_inputs_t inputs = s_commissioning_inputs;
    if (inputs.source_detection_known) {
        inputs.source_detection_configured = source_detection_configured;
    }
    return commissioning_gate_evaluate(&inputs);
}

_Static_assert(APP_MAX_GENERATORS == SOURCE_MAX_GENERATORS,
               "meter generator slots and source-mode generator channels must agree");
_Static_assert(APP_MAX_GENERATORS == SOLAR_GRID_MAX_GENERATORS,
               "meter generator slots and commissioned engine limits must agree");
_Static_assert(APP_MAX_GENERATORS == GENERATOR_FLEET_MAX_ENGINES,
               "meter generator slots and aggregate limit engines must agree");
_Static_assert(APP_MAX_GENERATORS == COMMISSIONING_MAX_GENERATORS,
               "meter generator slots and commissioning gate engine slots must agree");

/* The kW load-sharing vocabulary is declared three times -- once in the persisted
 * policy, once in the pure limit module, once in the pure gate -- because each of
 * those components deliberately depends on nothing. This file is the one place that
 * copies the value between them, so this is where the three must be proved
 * numerically identical. Without these, renumbering one enum would silently turn a
 * commissioned "base load" into "isochronous" and compute a floor for a plant that
 * is not sharing load that way. */
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_UNSET == (int)GENERATOR_SHARING_UNSET &&
                   (int)SOLAR_GRID_LOAD_SHARING_UNSET == (int)COMMISSIONING_SHARING_UNSET,
               "the uncommissioned load-sharing mode must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_ISOCHRONOUS == (int)GENERATOR_SHARING_ISOCHRONOUS &&
                   (int)SOLAR_GRID_LOAD_SHARING_ISOCHRONOUS == (int)COMMISSIONING_SHARING_ISOCHRONOUS,
               "isochronous load sharing must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_BASE_LOAD == (int)GENERATOR_SHARING_BASE_LOAD &&
                   (int)SOLAR_GRID_LOAD_SHARING_BASE_LOAD == (int)COMMISSIONING_SHARING_BASE_LOAD,
               "base-load sharing must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_DROOP == (int)GENERATOR_SHARING_DROOP &&
                   (int)SOLAR_GRID_LOAD_SHARING_DROOP == (int)COMMISSIONING_SHARING_DROOP,
               "droop sharing must be the same value everywhere, including where it is refused");
_Static_assert((int)SOLAR_GRID_LOAD_SHARING_COUNT == (int)GENERATOR_SHARING_MODE_COUNT &&
                   (int)SOLAR_GRID_LOAD_SHARING_COUNT == (int)COMMISSIONING_SHARING_COUNT,
               "a load-sharing mode added to one enum must be added to all three");
_Static_assert((int)SOLAR_GRID_ENGINE_ROLE_UNSET == (int)GENERATOR_ENGINE_ROLE_UNSET &&
                   (int)SOLAR_GRID_ENGINE_ROLE_UNSET == (int)COMMISSIONING_ENGINE_ROLE_UNSET,
               "the undeclared engine role must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_ENGINE_ROLE_SWING == (int)GENERATOR_ENGINE_ROLE_SWING &&
                   (int)SOLAR_GRID_ENGINE_ROLE_SWING == (int)COMMISSIONING_ENGINE_ROLE_SWING,
               "the swing engine role must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_ENGINE_ROLE_BASE_LOAD == (int)GENERATOR_ENGINE_ROLE_BASE_LOAD &&
                   (int)SOLAR_GRID_ENGINE_ROLE_BASE_LOAD == (int)COMMISSIONING_ENGINE_ROLE_BASE_LOAD,
               "the base-loaded engine role must be the same value everywhere");
_Static_assert((int)SOLAR_GRID_ENGINE_ROLE_COUNT == (int)GENERATOR_ENGINE_ROLE_COUNT &&
                   (int)SOLAR_GRID_ENGINE_ROLE_COUNT == (int)COMMISSIONING_ENGINE_ROLE_COUNT,
               "an engine role added to one enum must be added to all three");

/* Smallest increase worth a Modbus write, in kW. Below this a re-command carries
 * no information the inverter can act on, and at a fast control period it is pure
 * traffic. Decreases bypass this entirely -- reducing PV protects the generator
 * and must never be withheld for being small.
 *
 * Deliberately coarse relative to a 100 kW machine: the readback tolerance in the
 * profiles is 0.2 % (0.2 kW at 100 kW), so a threshold below that would command
 * changes finer than the device can be confirmed to have applied. */
#define CONTROL_COMMAND_EPSILON_KW 0.05f

/* How often an UNCHANGED setpoint is refreshed. A commanded limit can expire on
 * its own: the SmartLogger's schedule-validity period (register 42019) drops it
 * after a configured time and PV rises again with nothing reported. This is
 * comfortably inside any such window while staying far above the documented
 * one-second minimum between adjustments. */
#define CONTROL_COMMAND_KEEPALIVE_MS 2000U

/* Converts a ramp profile into the kW/s the policy layer expects.
 *
 * A disabled ramp must let the command step straight to the allowed target in a
 * SINGLE cycle. The policy limits movement to rate x interval, so the rate for a
 * disabled ramp has to be scaled by the interval: returning fleet_capacity_kw
 * alone means "full range per second", which at the shipped 250 ms interval
 * clamps each cycle to a quarter of the range and takes four cycles to reach
 * target. That was wrong twice over -- it is not the documented behaviour, and it
 * coupled the ramp to the poll rate in the worst direction, so polling faster for
 * fresher data made the effective ramp slower as a fraction of range.
 *
 * It does NOT bypass the policy: the export/import target, the generator limit
 * and every safety clamp are applied before the rate limiter and still hold. A
 * disabled ramp removes a rate limit, not a safety limit.
 *
 * An enabled ramp is a true rate: percent of fleet capacity per second,
 * independent of the interval, which is what makes it a commissioning value an
 * engineer can reason about. */
static float ramp_kw_per_second(const ramp_profile_t *ramp, bool upward,
                                float fleet_capacity_kw, bool fleet_valid,
                                float interval_seconds)
{
    if (!fleet_valid) return 0.0f;
    if (!ramp->enabled) {
        /* Full range within one cycle, whatever the interval. Guarded so a
         * nonsensical interval cannot produce a non-finite rate, which the policy
         * would reject as invalid input and refuse to command at all. */
        if (!isfinite(interval_seconds) || interval_seconds <= 0.0f) {
            return fleet_capacity_kw;
        }
        return fleet_capacity_kw / interval_seconds;
    }
    const float percent = upward ? ramp->up_percent_per_second
                                 : ramp->down_percent_per_second;
    if (!isfinite(percent) || percent <= 0.0f) return 0.0f;
    return fleet_capacity_kw * percent * 0.01f;
}

/* One sentence per way the aggregate generator limit can fail closed, so an
 * engineer is told which piece of evidence is missing rather than a generic
 * "no valid command".
 *
 * THREE REASONS DELIBERATELY HAVE NO SENTENCE HERE YET, and fall to the default: the
 * base-load setpoint-agreement tolerance being uncommissioned, a base-loaded engine
 * having no usable measurement, and a base-loaded engine's measured power disagreeing
 * with its setpoint. That is not an oversight, and it is not a judgement that they do
 * not deserve one -- the drift reason in particular is the most operationally useful
 * sentence in this list. tests/multi_engine_commissioning_source_contract.py requires
 * every sentence in this function to be carried verbatim by web/solar-grid.js, and this
 * change is not permitted to edit web assets. Adding the sentences here without the
 * browser's copies would fail that contract, and a paraphrase in the browser would fail
 * it for a better reason. The commissioning gate DOES carry a full operator sentence
 * for the uncommissioned tolerance, which is where an engineer meets it first; the two
 * runtime reasons currently read as "the aggregate generator limit could not be
 * established, so PV is held at zero", which is true but not specific.
 *
 * WHAT THE WEB OWNER NEEDS: three entries in FLEET_REASON_SENTENCES keyed
 * base_load_tolerance_unset, base_load_unmeasured and base_load_setpoint_drift, and the
 * matching switch arms added here, in the same commit. The exact wording is in the
 * handover notes; the slugs are already published by generator_fleet_reason_id(). */
static const char *generator_fleet_inhibit(uint8_t reason)
{
    switch (reason) {
    case GENERATOR_FLEET_NO_ENGINE_CONFIGURED:
        return "No generator engine is commissioned, so no minimum-loading floor can be computed.";
    case GENERATOR_FLEET_RATING_UNKNOWN:
        return "A commissioned generator engine has no usable rating or minimum-loading figure.";
    case GENERATOR_FLEET_RUNNING_SET_UNKNOWN:
        return "Which generator engines are online cannot be determined, so PV is held at zero.";
    case GENERATOR_FLEET_NO_ENGINE_ONLINE:
        return "A generator is carrying the plant but no commissioned engine is measured online.";
    case GENERATOR_FLEET_LOAD_UNKNOWN:
        return "The plant load behind the generator limit is missing or non-finite.";
    case GENERATOR_FLEET_SHARING_MODE_UNSET:
        return "More than one generator engine is online and no kW load-sharing mode is commissioned, so which engine sets the minimum-loading floor is unknown.";
    case GENERATOR_FLEET_SHARING_MODE_UNSUPPORTED:
        return "The commissioned kW load-sharing mode is not one a defensible minimum-loading floor can be computed for; droop sharing is refused rather than approximated.";
    case GENERATOR_FLEET_BASE_LOAD_UNKNOWN:
        return "Base-load sharing is commissioned but an online engine has no declared role, or a base-loaded engine has no fixed kW setpoint.";
    case GENERATOR_FLEET_BASE_LOAD_BELOW_MINIMUM:
        return "A base-loaded engine's fixed kW setpoint is below its own minimum loading, which no PV limit can correct.";
    case GENERATOR_FLEET_NO_SWING_ENGINE:
        return "Every online engine is held at a fixed kW, so nothing on the bus would absorb the load the controller shapes.";
    default:
        return "The aggregate generator limit could not be established, so PV is held at zero.";
    }
}

static meter_role_assignment_t current_role_assignment(void)
{
    meter_role_assignment_t roles;
    portENTER_CRITICAL(&s_lock);
    roles = s_roles;
    portEXIT_CRITICAL(&s_lock);
    return roles;
}
static portMUX_TYPE s_evidence_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void runtime_control_snapshot(bool *enabled, bool *safe_zero_pending)
{
    portENTER_CRITICAL(&s_lock);
    if (enabled) *enabled = s_config.enabled && !s_runtime_forced_disabled;
    if (safe_zero_pending) *safe_zero_pending = s_safe_zero_pending;
    portEXIT_CRITICAL(&s_lock);
}

static void clear_safe_zero_pending(void)
{
    portENTER_CRITICAL(&s_lock);
    s_safe_zero_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

static bool meter_sample_fresh(const meter_data_t *meter, uint32_t timestamp)
{
    if (!meter || !meter->online || meter->degraded || meter->last_update_ms == 0U ||
        !isfinite(meter->active_power_kw)) {
        return false;
    }
    uint32_t timeout = s_config.meter_stale_timeout_ms;
    if (timeout < 100U) timeout = 100U;
    return timestamp - meter->last_update_ms <= timeout;
}

static float safe_interval_seconds(uint32_t timestamp, uint32_t *previous_timestamp)
{
    float interval = (timestamp - *previous_timestamp) / 1000.0f;
    *previous_timestamp = timestamp;
    float configured = s_config.interval_ms / 1000.0f;
    if (!isfinite(configured) || configured <= 0.0f) configured = 0.5f;
    if (!isfinite(interval) || interval <= 0.0f || interval > configured * 4.0f) {
        interval = configured;
    }
    return interval;
}

static grid_evidence_runtime_t evidence_snapshot(void)
{
    grid_evidence_runtime_t snapshot;
    portENTER_CRITICAL(&s_evidence_lock);
    snapshot = s_evidence;
    portEXIT_CRITICAL(&s_evidence_lock);
    return snapshot;
}

static void evidence_store(const grid_evidence_runtime_t *next)
{
    portENTER_CRITICAL(&s_evidence_lock);
    s_evidence = *next;
    portEXIT_CRITICAL(&s_evidence_lock);
}

static bool signal_active(const solar_grid_signal_config_t *signal, uint16_t raw)
{
    return (raw & signal->mask) == (signal->active_value & signal->mask);
}

static bool same_signal_register(const solar_grid_signal_config_t *left,
                                 const solar_grid_signal_config_t *right)
{
    return left->meter_index == right->meter_index &&
           left->function_code == right->function_code &&
           left->address == right->address;
}

static esp_err_t read_signal(const solar_grid_signal_config_t *signal,
                             uint16_t *raw)
{
    if (!signal || !signal->enabled || !raw) return ESP_ERR_INVALID_ARG;
    return meter_manager_read_registers(signal->meter_index,
                                        signal->function_code,
                                        signal->address,
                                        1U,
                                        raw);
}

static void grid_evidence_task(void *argument)
{
    (void)argument;
    const solar_grid_signal_config_t available = s_grid_config.grid_available;
    const solar_grid_signal_config_t breaker = s_grid_config.grid_breaker_closed;
    const bool shared_register = same_signal_register(&available, &breaker);

    while (true) {
        uint16_t available_raw = 0U;
        uint16_t breaker_raw = 0U;
        uint32_t timestamp = now_ms();
        esp_err_t available_error = read_signal(&available, &available_raw);
        esp_err_t breaker_error = ESP_OK;
        if (shared_register && available_error == ESP_OK) {
            breaker_raw = available_raw;
        } else {
            breaker_error = read_signal(&breaker, &breaker_raw);
        }

        grid_evidence_runtime_t next = evidence_snapshot();
        next.last_attempt_ms = timestamp;
        esp_err_t error = available_error != ESP_OK ? available_error : breaker_error;
        if (error == ESP_OK) {
            next.grid_available_raw = available_raw;
            next.grid_breaker_raw = breaker_raw;
            next.grid_available = signal_active(&available, available_raw);
            next.grid_breaker_closed = signal_active(&breaker, breaker_raw);
            next.last_update_ms = timestamp;
            next.success_count++;
            next.last_error = ESP_OK;
        } else {
            next.error_count++;
            next.last_error = error;
            if (next.error_count == 1U || next.error_count % 30U == 0U) {
                ESP_LOGW(TAG, "Grid evidence read failed: %s [error %u]",
                         esp_err_to_name(error), (unsigned)next.error_count);
            }
        }
        evidence_store(&next);

        uint32_t delay_ms = s_grid_config.evidence_poll_interval_ms;
        if (delay_ms < 100U) delay_ms = 100U;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static float oriented_grid_power(float raw_grid_kw)
{
    if (!isfinite(raw_grid_kw)) return NAN;
    return s_grid_config.meter_orientation == SOLAR_GRID_EXPORT_POSITIVE
               ? -raw_grid_kw
               : raw_grid_kw;
}

static float configured_grid_target(void)
{
    switch (s_grid_config.policy) {
    case SOLAR_GRID_POLICY_LIMITED_EXPORT:
        return -fabsf(s_grid_config.export_limit_kw);
    case SOLAR_GRID_POLICY_MINIMUM_IMPORT:
        return fmaxf(0.0f, s_grid_config.minimum_import_kw);
    case SOLAR_GRID_POLICY_ZERO_EXPORT:
    default:
        return 0.0f;
    }
}

static void control_task(void *argument)
{
    (void)argument;
    float integral_kw = 0.0f;
    float current_target_kw = 0.0f;
    uint32_t previous_ms = now_ms();
    bool previous_cycle_valid = false;
    grid_gate_memory_t gate_memory = {0};
    /* Command-issue bookkeeping. The loop recomputes every cycle but only writes
     * on a change or when the keepalive falls due; see the write site below. */
    float last_commanded_kw = 0.0f;
    uint32_t last_command_issued_ms = 0U;
    bool command_ever_issued = false;
    /* Last observed value of inverter_manager_fleet_rejoins(). */
    uint32_t last_seen_rejoins = inverter_manager_fleet_rejoins();

    while (true) {
        uint32_t timestamp = now_ms();
        float interval_seconds = safe_interval_seconds(timestamp, &previous_ms);
        bool control_enabled = false;
        bool safe_zero_pending = false;
        runtime_control_snapshot(&control_enabled, &safe_zero_pending);

        /* Selected by role, not by array position. Reordering the meter list
         * must never silently change which physical instrument the control loop
         * regulates against. An absent or ambiguous grid assignment yields no
         * measurement, so the existing freshness gate fails closed. */
        meter_data_t grid = {0};
        const meter_role_assignment_t roles = current_role_assignment();
        bool have_grid = roles.valid && roles.grid_index != METER_ROLE_INDEX_NONE &&
                         meter_manager_get_data(roles.grid_index, &grid);
        float fleet_capacity_kw = inverter_manager_get_total_rated_kw();
        bool measurement_fresh = have_grid && meter_sample_fresh(&grid, timestamp);
        bool fleet_valid = isfinite(fleet_capacity_kw) && fleet_capacity_kw > 0.0f;
        float raw_grid_kw = measurement_fresh ? grid.active_power_kw : NAN;

        /*
         * WHICH MEASUREMENT THE GRID POLICY IS ENFORCED ON.
         *
         * On an unbalanced three-phase site a limit satisfied by the TOTAL can be
         * violated on a single conductor: the total reads zero while one phase
         * exports, and the utility meters the phase. Where the site commissioned
         * lowest-phase control and all three phases were read, the policy is
         * driven by the worst conductor instead of the sum.
         *
         * The selection is a pure function so the reasoning is executed by
         * tests/phase_selection_test.c rather than asserted here. It orients the
         * SELECTED value, not the phases: sign convention is a property of the
         * installation, so it applies identically to whichever figure is chosen,
         * and orienting first would mean the minimum was taken over the wrong
         * sign on an export-positive site -- selecting the phase LEAST able to
         * export rather than the one most able to.
         */
        phase_selection_input_t phase_input = {
            .total_kw = raw_grid_kw,
            .total_valid = measurement_fresh,
            .basis = (uint8_t)(grid_phase_basis() == METER_PHASE_BASIS_LOWEST_PHASE
                                   ? PHASE_BASIS_PER_PHASE : PHASE_BASIS_TOTAL),
        };
        for (int phase = 0; phase < 3; ++phase) {
            phase_input.phase_kw[phase] = grid.phase_power_kw[phase];
            phase_input.phase_valid[phase] = measurement_fresh && grid.phase_valid[phase];
        }
        const phase_selection_t phase = phase_selection_evaluate(&phase_input);
        store_phase_selection(&phase);

        float measured_grid_kw = oriented_grid_power(phase.valid ? phase.controlling_kw
                                                                 : raw_grid_kw);

        grid_evidence_runtime_t evidence = evidence_snapshot();
        bool evidence_fresh = evidence.configured && evidence.last_update_ms != 0U &&
                              timestamp - evidence.last_update_ms <=
                                  s_grid_config.evidence_stale_timeout_ms;

        /* Two independent ways to establish which source is carrying the plant.
         * Breaker/synchronisation evidence from a genset controller is stronger
         * and wins when configured. Otherwise fall back to the measured source
         * identity, which says only which source is carrying load - it never
         * claims a breaker position or that two sources are synchronised. */
        source_mode_result_t source;
        bool detection_configured = false;
        if (evidence.configured) {
            source_evidence_t source_evidence = {
                .evidence_fresh = evidence_fresh,
                .transfer_active = false,
                .grid_available = evidence.grid_available,
                .grid_breaker_closed = evidence.grid_breaker_closed,
                .generator_running = false,
                .generator_breaker_closed = false,
                .grid_generator_synchronized = false,
            };
            source = source_mode_evaluate(&source_evidence);
        } else {
            measured_source_t measured = MEASURED_SOURCE_UNKNOWN;
            bool measured_fresh = false;
            bool synchronised = false;
            source_detection_status_t detection;
            if (source_detection_get_status(&detection) == ESP_OK) {
                measured_fresh = detection.configured && detection.evidence_fresh &&
                                 !detection.fail_closed && !detection.transition_pending;
                detection_configured = detection.configured;
                if (detection.state == SOURCE_STATE_GRID) measured = MEASURED_SOURCE_GRID;
                else if (detection.state == SOURCE_STATE_GENERATOR) measured = MEASURED_SOURCE_GENERATOR;
                /* Parallel operation on a plant commissioned to synchronise.
                 * Reported by NAME rather than falling through to UNKNOWN,
                 * because the two mean different things to whoever reads the
                 * screen: "unknown" sends an engineer to look for a broken
                 * sensor, "synchronised" tells them the plant is doing exactly
                 * what it was built to do and this controller has no strategy
                 * for it yet. Either way the gate refuses it and PV is held at
                 * zero, so the honesty costs nothing in behaviour. */
                else if (detection.state == SOURCE_STATE_SYNCHRONISED) {
                    synchronised = true;
                }
            }
            source = source_mode_from_measured_source(measured, measured_fresh);
            if (synchronised && measured_fresh) {
                source.mode = SOURCE_MODE_GRID_GENERATOR_SYNC;
                /* Vouched for, because the strategy now exists: the grid policy
                 * sets the target and the generator floor caps the maximum, so
                 * the more restrictive protection wins. Reaching this state at
                 * all already required an engineer to commission the plant as
                 * able to synchronise. */
                source.control_allowed = true;
            }
            evidence_fresh = measured_fresh;
        }
        grid_gate_input_t gate_input = {
            /* EITHER source of evidence configures this gate.
             *
             * This read evidence.configured alone, which is the breaker map. So
             * on a plant commissioned the other way -- one meter, a digital
             * input, and no breaker contacts -- the fallback source identity
             * computed immediately above was handed to the gate and thrown away
             * on its first line, and control was inhibited forever with a
             * message naming grid evidence the engineer was never asked to
             * configure. The fallback was written, documented and unreachable.
             *
             * Breaker evidence still WINS when configured; this only stops the
             * gate from treating "no breaker map" as "no evidence at all". */
            .configured = evidence.configured || detection_configured,
            .evidence_fresh = evidence_fresh,
            .source_mode = source.mode,
            .source_control_allowed = source.control_allowed,
            .timestamp_ms = timestamp,
            .loss_trip_ms = s_grid_config.grid_loss_trip_ms,
            .recovery_stable_ms = s_grid_config.grid_recovery_stable_ms,
        };
        grid_gate_output_t gate = grid_control_gate_step(&gate_memory, &gate_input);

        /* THE COMMISSIONING GATE (P0-6).
         *
         * Applied to the control task's own enable, not merely reported: while
         * the gate is closed the policy is never stepped, so there is no
         * command to clamp and no path by which automatic control can engage.
         * Unknown or unreadable configuration evaluates to "not commissioned",
         * so this fails closed rather than open. */
        const commissioning_status_t commissioning =
            evaluate_commissioning(detection_configured);
        store_commissioning(&commissioning);
        if (!commissioning.commissioned) control_enabled = false;

        /* P0-9: a setpoint that could not be confirmed by readback latches its
         * inverter out of the commandable fleet, which is what actually stops
         * the command. This is the operator-facing statement of that fact. */
        const bool confirmation_fault = inverter_manager_write_confirmation_fault();
        const inverter_write_state_t fleet_confirmation =
            inverter_manager_fleet_write_confirmation();

        /* The prerequisite enable register is observed the same way and for the
         * same structural reason, but it is NOT the same fault and is never
         * merged into the one above. A confirmation fault says the setpoint read
         * back wrong. A prerequisite fault says the setpoint will read back
         * RIGHT and be ignored anyway, because the register that arms the limit
         * is not confirmed to hold. An engineer told only "unconfirmed setpoint"
         * would go looking at the setpoint register, which is working.
         *
         * Both calls read already-acquired state. Neither performs Modbus I/O,
         * so the loop gains no blocking transaction at its 20 ms period. The
         * summary struct is a couple of dozen bytes and is safe on this task's
         * 4 kB stack, unlike an app_config_t. */
        const bool prerequisite_fault = inverter_manager_prerequisite_enable_fault();
        inverter_fleet_commissioning_t fleet_prerequisite;
        inverter_manager_commissioning_summary(&fleet_prerequisite);

        /* While a generator carries the plant, PV must leave enough load on the
         * machine to satisfy its minimum loading and stay clear of reverse
         * power. An uncommissioned rating yields zero, which holds PV off rather
         * than commanding against a machine of unknown capacity. This reduces
         * the likelihood of reverse power; it does not replace the generator's
         * own protection relay. */
        /* Ramping is chosen by which source is carrying the plant: a generator
         * needs its rate limited, the grid does not. */
        const bool generator_carrying = source.mode == SOURCE_MODE_GENERATOR_ONLY ||
                                        source.mode == SOURCE_MODE_ISLAND ||
                                        source.mode == SOURCE_MODE_GRID_GENERATOR_SYNC;
        const ramp_profile_t ramp = generator_carrying ? s_config.generator_ramp
                                                       : s_config.grid_ramp;

        /* WHICH ENGINES ARE RUNNING IS A RUNTIME FACT, NOT CONFIGURATION.
         *
         * The commissioned ratings say what machines exist; the generator-role
         * meters say which of them are on the bus right now. Both are needed,
         * because the minimum-loading floor is computed against the aggregate
         * rating of the engines actually online -- with two engines running and a
         * rating for one, the denominator is wrong in the permissive direction and
         * the controller would allow far more PV than the plant can carry.
         *
         * Every read here is of already-acquired cached state:
         * solar_grid_config_generator() reads the static snapshot taken at init,
         * and meter_manager_get_data() returns the last poll result. No Modbus
         * transaction is issued, so the 20 ms loop gains no blocking I/O. */
        float generator_safe_limit_kw = 0.0f;
        generator_fleet_limit_t fleet_limit = {0};
        /* ISLAND is a generator carrying the plant with no grid behind it, so it
         * needs the same floor as GENERATOR_ONLY -- arguably more, since there
         * is nothing to fall back on. Computing it for only one of the two left
         * the other with a floor of zero while the gate released control, which
         * is an unprotected machine. */
        if (source.mode == SOURCE_MODE_GENERATOR_ONLY ||
            source.mode == SOURCE_MODE_ISLAND ||
            source.mode == SOURCE_MODE_GRID_GENERATOR_SYNC) {
            /*
             * THE PLANT LOAD, NOT THE GENERATOR'S SHARE OF IT.
             *
             * This passed the source meter reading alone. In generator mode that
             * meter reads what the GENERATOR is producing, and the plant is
             * carrying that plus whatever the inverters are generating. The
             * limit module subtracts the required generator load from what it is
             * given and hands the remainder back as an ABSOLUTE cap on PV, so
             * feeding it the generator's own output made the cap a measure of
             * present headroom rather than of allowable PV -- and a cap that
             * moves whenever PV moves.
             *
             * That converges to half the correct answer. With load L and floor
             * R, the cap is (L - PV) - R, and driving PV to the cap settles at
             * PV = (L - R) / 2 instead of L - R. A plant would have run at half
             * the solar it could safely carry, silently, with every number on
             * screen looking reasonable.
             *
             * Total load = source meter + measured solar, which is the site
             * definition: Total kW = Grid + Gen + Solar.
             *
             * Only inverters reporting a VALID measurement contribute. An
             * inverter whose telemetry is stale or unsupported contributes zero,
             * which understates the load and therefore understates the PV cap:
             * the error direction is toward a more loaded generator, which is
             * the safe direction. Reading cached state only; no Modbus here. */
            float measured_solar_kw = 0.0f;
            for (uint8_t slot = 0U; slot < inverter_manager_get_count(); ++slot) {
                inverter_data_t solar = {0};
                if (!inverter_manager_get_data(slot, &solar)) continue;
                if (!solar.telemetry_valid || solar.telemetry_stale) continue;
                if (!isfinite(solar.measured_power_kw) || solar.measured_power_kw < 0.0f) continue;
                measured_solar_kw += solar.measured_power_kw;
            }

            /* IN PARALLEL THE SOURCE METER IS NOT THE WHOLE SUPPLY.
             *
             * With grid and generator both on the bus the source meter reads the
             * GRID's share only, so the plant load is grid + generator + solar.
             * Omitting the generator's own output would understate the load,
             * understate the floor's headroom, and curtail PV harder than
             * necessary -- safe, but it would throw away solar on exactly the
             * sites that paid for a synchroniser.
             *
             * Summed from the generator-role meters, which the loop already
             * reads for the running-set decision. A generator with no meter
             * contributes nothing, which understates the load and therefore errs
             * toward a more loaded generator. */
            float parallel_generator_kw = 0.0f;
            if (source.mode == SOURCE_MODE_GRID_GENERATOR_SYNC) {
                for (uint8_t slot = 0U; slot < APP_MAX_GENERATORS; ++slot) {
                    const uint8_t meter_index = roles.generator_index[slot];
                    if (!roles.valid || meter_index == METER_ROLE_INDEX_NONE) continue;
                    meter_data_t generator_meter = {0};
                    if (!meter_manager_get_data(meter_index, &generator_meter)) continue;
                    if (!meter_sample_fresh(&generator_meter, timestamp)) continue;
                    if (!isfinite(generator_meter.active_power_kw)) continue;
                    parallel_generator_kw += fabsf(generator_meter.active_power_kw);
                }
            }

            generator_fleet_input_t fleet_input = {
                .evidence_fresh = measurement_fresh,
                .facility_load_kw = fabsf(measured_grid_kw) + measured_solar_kw +
                                    parallel_generator_kw,
                /* Only when the site has no generator-role meter at all: then a
                 * single commissioned engine is unambiguous, which is the legacy
                 * single-generator behaviour. */
                .allow_unmetered_single_engine = roles.generator_count == 0U,
                /* Copied straight through: the persisted accessor already reports an
                 * unrecognised stored value as UNSET, and the limit module refuses
                 * anything it does not model. Nothing here interprets the mode. */
                .sharing_mode = solar_grid_config_load_sharing_mode(&s_grid_config),
                .engine_count = APP_MAX_GENERATORS,
                /* The commissioned band within which a base-loaded engine's measured
                 * power must agree with its setpoint. Copied straight through: the
                 * persisted accessors already report anything that is not a usable
                 * tolerance as zero, and zero in both means NOT COMMISSIONED, on which
                 * the limit module refuses base-load sharing rather than computing a
                 * floor from a setpoint nobody has been shown to be holding. Nothing
                 * here invents a band, and nothing here reads the measurement -- both
                 * belong to the pure function that can be unit tested. */
                .base_load_tolerance_kw =
                    solar_grid_config_base_load_tolerance_kw(&s_grid_config),
                .base_load_tolerance_percent_of_rating =
                    solar_grid_config_base_load_tolerance_percent(&s_grid_config),
            };
            for (uint8_t slot = 0U; slot < APP_MAX_GENERATORS; ++slot) {
                const solar_grid_generator_limits_t limits =
                    solar_grid_config_generator(&s_grid_config, slot);
                generator_engine_input_t *engine = &fleet_input.engines[slot];
                engine->configured = limits.enabled;
                engine->rated_kw = limits.rated_kw;
                engine->minimum_loading_percent = limits.minimum_loading_percent;
                engine->reserve_kw = limits.reserve_kw;
                engine->reverse_power_margin_kw = limits.reverse_power_margin_kw;
                engine->role = solar_grid_config_engine_role(&s_grid_config, slot);
                engine->base_load_kw =
                    solar_grid_config_engine_base_load_kw(&s_grid_config, slot);

                const uint8_t meter_index = roles.generator_index[slot];
                engine->metered = roles.valid && meter_index != METER_ROLE_INDEX_NONE;
                if (!engine->metered) continue;
                meter_data_t generator_meter = {0};
                if (!meter_manager_get_data(meter_index, &generator_meter)) continue;
                /* The same freshness rule the grid meter is held to, so one
                 * definition of "fresh" governs the whole loop. */
                engine->sample_fresh = meter_sample_fresh(&generator_meter, timestamp);
                engine->measured_kw = generator_meter.active_power_kw;
            }
            fleet_limit = generator_fleet_limit_evaluate(&fleet_input);
            /* An unknown running set yields zero, which holds PV off rather than
             * commanding against a plant of unknown capacity. */
            generator_safe_limit_kw = fleet_limit.known ? fleet_limit.safe_pv_kw : 0.0f;
        }
        /* Published every cycle, in generator mode or not, so a verdict from a
         * previous source mode is never left standing as if it were current. */
        store_fleet_limit(&fleet_limit);

        power_control_input_t input = {
            .measurement_fresh = measurement_fresh && fleet_valid && gate.control_allowed,
            .source_mode = source.mode,
            .policy = (grid_policy_t)s_grid_config.policy,
            .measured_grid_kw = measured_grid_kw,
            .export_limit_kw = s_grid_config.export_limit_kw,
            .minimum_import_kw = s_grid_config.minimum_import_kw,
            .current_pv_command_kw = current_target_kw,
            .fleet_capacity_kw = fleet_capacity_kw,
            .kp = s_config.kp,
            .ki = s_config.ki,
            .deadband_kw = s_config.deadband_kw,
            .interval_seconds = interval_seconds,
            .ramp_up_kw_per_second = ramp_kw_per_second(&ramp, true, fleet_capacity_kw,
                                                        fleet_valid, interval_seconds),
            /* Accelerated ONLY downward, and only while the generators are
             * urgently underloaded. Lowering PV is the single lever this
             * controller has to raise generator load, so that is the direction
             * worth hurrying; hurrying upward would feed PV into a starving
             * machine. The multiplier is 1.0 for anything the fleet module will
             * not vouch for, so an uncertain plant ramps at the commissioned
             * rate rather than one inferred here. */
            .ramp_down_kw_per_second =
                ramp_kw_per_second(&ramp, false, fleet_capacity_kw, fleet_valid,
                                   interval_seconds) *
                generator_urgent_ramp_multiplier(generator_carrying, fleet_limit.known,
                                                 fabsf(measured_grid_kw),
                                                 fleet_limit.online_rated_kw),
            .integral_kw = integral_kw,
            .generator_safe_limit_kw = generator_safe_limit_kw,
        };

        power_control_output_t policy = {0};
        if (control_enabled) policy = power_control_step(&input);

        if (!control_enabled || !policy.valid) {
            integral_kw = 0.0f;
            policy.requested_pv_kw = 0.0f;
        } else if (!previous_cycle_valid) {
            current_target_kw = 0.0f;
        }
        if (policy.valid) integral_kw = policy.next_integral_kw;

        float requested_kw = control_enabled && policy.valid
                                 ? policy.requested_pv_kw
                                 : 0.0f;
        float applied_kw = safety_manager_limit_target_kw(requested_kw, &grid, timestamp);
        if (!isfinite(applied_kw) || applied_kw < 0.0f) applied_kw = 0.0f;

        uint32_t alarm_flags = safety_manager_get_alarm_flags();
        app_mode_t mode = APP_MODE_DISABLED;
        /* Only an accepted inverter write sets this, so a failed command can
         * never be reported as a successful one. */
        bool command_accepted = false;
        if (control_enabled) {
            mode = policy.valid && alarm_flags == 0U ? APP_MODE_GRID : APP_MODE_FAILSAFE;
            /* Issue a command when the setpoint has actually moved, or when the
             * keepalive is due -- not on every cycle.
             *
             * Every cycle was survivable at a 250 ms period. It is not once the
             * loop runs at the rate measurements arrive: a 20 ms period would push
             * 50 writes per second at equipment for which no manual sanctions any
             * rate, and the Huawei SmartLogger explicitly documents a minimum of
             * one second between adjustments. Recomputing fast and commanding only
             * on change keeps the fast reaction while removing traffic that carries
             * no information.
             *
             * The keepalive is not decoration. A commanded limit can EXPIRE: the
             * SmartLogger's schedule-validity period (register 42019) drops it
             * after a configured time, and PV rises again with nothing reported.
             * Refreshing an unchanged setpoint periodically is what keeps a
             * standing limit standing.
             *
             * A change of any size in the REDUCING direction always writes
             * immediately, because that is the direction that protects the
             * generator. The threshold only suppresses insignificant increases. */
            const bool first_command = !command_ever_issued;
            const float delta_kw = applied_kw - last_commanded_kw;
            const bool keepalive_due =
                timestamp - last_command_issued_ms >= CONTROL_COMMAND_KEEPALIVE_MS;
            const bool changed = delta_kw < 0.0f
                                     ? true
                                     : delta_kw > CONTROL_COMMAND_EPSILON_KW;
            /*
             * ONE WRITE WHEN A MACHINE COMES BACK.
             *
             * While an inverter is out of the fleet the loop goes on commanding
             * the ones that remain, and commands are only issued when the target
             * changes -- so a machine that rejoins is still holding the setpoint
             * it had before its link dropped, and nothing would rewrite it while
             * the target happens to be steady. The plant then runs with one
             * inverter enforcing a stale limit and the controller believing the
             * whole fleet is on the current one.
             *
             * The counter is monotonic, so a cycle that does not observe a rejoin
             * cannot lose it.
             */
            const uint32_t rejoins = inverter_manager_fleet_rejoins();
            const bool fleet_rejoined = rejoins != last_seen_rejoins;
            last_seen_rejoins = rejoins;

            esp_err_t write_result = ESP_OK;
            if (first_command || changed || keepalive_due || fleet_rejoined) {
                write_result = inverter_manager_set_total_power_kw(applied_kw);
                if (write_result == ESP_OK) {
                    last_commanded_kw = applied_kw;
                    last_command_issued_ms = timestamp;
                    command_ever_issued = true;
                }
            }
            if (write_result != ESP_OK) {
                if (applied_kw > 0.0f) {
                    ESP_LOGW(TAG, "inverter fleet command failed: %s",
                             esp_err_to_name(write_result));
                }
                applied_kw = 0.0f;
                current_target_kw = 0.0f;
                integral_kw = 0.0f;
                mode = APP_MODE_FAILSAFE;
            } else {
                current_target_kw = applied_kw;
                command_accepted = true;
            }
        } else if (safe_zero_pending) {
            /* The HTTP/configuration path only sets this latch. The control task
             * owns the physical zero command and retains the last confirmed
             * applied value if that command cannot be confirmed. */
            esp_err_t zero_result = inverter_manager_set_total_power_kw(0.0f);
            if (zero_result == ESP_OK ||
                (current_target_kw <= 0.0f && zero_result == ESP_ERR_INVALID_STATE)) {
                clear_safe_zero_pending();
                current_target_kw = 0.0f;
                applied_kw = 0.0f;
                command_accepted = true;
            } else {
                applied_kw = current_target_kw;
                mode = APP_MODE_FAILSAFE;
                ESP_LOGE(TAG, "Safe-zero command after control disable failed: %s",
                         esp_err_to_name(zero_result));
            }
            integral_kw = 0.0f;
        } else {
            current_target_kw = 0.0f;
            integral_kw = 0.0f;
            applied_kw = 0.0f;
        }

        previous_cycle_valid = control_enabled && policy.valid && alarm_flags == 0U;
        uint32_t evidence_age = evidence.last_update_ms != 0U
                                    ? timestamp - evidence.last_update_ms
                                    : 0U;

        control_status_t next = {
            .enabled = control_enabled,
            .mode = mode,
            .grid_power_kw = measured_grid_kw,
            .raw_grid_power_kw = raw_grid_kw,
            .grid_target_kw = configured_grid_target(),
            .error_kw = policy.valid ? policy.error_kw : NAN,
            .requested_pv_kw = requested_kw,
            .applied_pv_kw = applied_kw,
            .grid_policy = (uint8_t)s_grid_config.policy,
            .source_mode = (uint8_t)source.mode,
            .grid_gate_state = (uint8_t)gate.state,
            .grid_evidence_configured = evidence.configured,
            .grid_evidence_fresh = evidence_fresh,
            .grid_available = evidence.grid_available,
            .grid_breaker_closed = evidence.grid_breaker_closed,
            .grid_recovery_stable = gate.recovery_stable,
            .grid_loss_confirmed = gate.loss_confirmed,
            .grid_evidence_age_ms = evidence_age,
            .grid_evidence_success_count = evidence.success_count,
            .grid_evidence_error_count = evidence.error_count,
            .grid_evidence_last_error = evidence.last_error,
            .grid_available_raw = evidence.grid_available_raw,
            .grid_breaker_raw = evidence.grid_breaker_raw,
            .alarm_flags = alarm_flags,
            .last_cycle_ms = timestamp,
            /* Commissioning is part of the authority answer, not a footnote to
             * it: an uncommissioned controller has no authority to command. */
            /* An unconfirmed prerequisite withdraws authority for exactly the
             * reason an unconfirmed setpoint does, and is treated identically
             * rather than more leniently: while it holds, this controller cannot
             * know that a limit it writes will be honoured, and reporting
             * authority in that state is the false-confirmation trap the
             * prerequisite sequencing exists to prevent.
             *
             * Worth being explicit, because it looks alarming on a MIXED fleet:
             * this field is a REPORT, not a gate. Nothing gates a command on it.
             * Commanding is gated by control_enabled above, and which inverters
             * get commanded is decided per inverter inside inverter_manager,
             * where an unverified prerequisite excludes only that machine. So one
             * unconfirmed Solis does not stop a healthy Huawei being reduced --
             * which matters, because withholding a REDUCTION is the harm this
             * product exists to prevent, and it would be an unacceptable price
             * for a tidier status field. */
            .command_authority = commissioning.commissioned && control_enabled &&
                                 policy.valid && alarm_flags == 0U &&
                                 !confirmation_fault && !prerequisite_fault,
            .commissioned = commissioning.commissioned,
            .commissioning_scope = (uint8_t)commissioning.scope,
            .commissioning_unmet_count = commissioning.unmet_count,
            .commissioning_first_unmet = commissioning.first_unmet,
            .write_confirmation = (uint8_t)fleet_confirmation,
            .write_confirmation_fault = confirmation_fault,
            .prerequisite_enable_fault = prerequisite_fault,
            .prerequisite_required_count = fleet_prerequisite.prerequisite_required_count,
            .prerequisite_unconfirmed_count =
                fleet_prerequisite.prerequisite_unconfirmed_count,
            .prerequisite_unverifiable_count =
                fleet_prerequisite.prerequisite_unverifiable_count,
        };
        /* One authoritative answer, in the firmware's own words, rather than the
         * interface inferring intent from several scattered flags. Ordered most
         * specific first so the operator is told the thing they can act on. */
        const char *inhibit =
            !commissioning.commissioned ? commissioning_gate_summary(&commissioning)
            : !control_enabled        ? "Automatic control is disabled; engineering authorisation is required."
            : alarm_flags != 0U       ? "An active safety alarm is blocking commands."
            /* Both prerequisite reasons sit AHEAD of the confirmation fault
             * because they are the more specific and the more dangerous answer.
             * A prerequisite fault does not show up in the setpoint readback at
             * all: the setpoint is accepted, echoed back and ignored, so the
             * readback looks perfect while the inverter runs unlimited. Reporting
             * only "unconfirmed setpoint" would send an engineer to the register
             * that is working. The unverifiable case is stated first because it
             * is permanent -- no amount of polling resolves it, and the remedy is
             * a manual citation rather than waiting. */
            : prerequisite_fault &&
              fleet_prerequisite.prerequisite_unverifiable_count > 0U
                                      ? "An inverter enable register cannot be read back; its setpoint would read back correctly and be ignored."
            : prerequisite_fault      ? "An inverter prerequisite enable register is not confirmed to hold; its setpoint would read back correctly and be ignored."
            : confirmation_fault      ? "An inverter setpoint could not be confirmed by readback; that inverter is held at zero and excluded from the fleet."
            : !roles.valid            ? "No single enabled meter is assigned the grid role."
            : !measurement_fresh      ? "The grid measurement is missing, stale or non-finite."
            : !fleet_valid            ? "No commissioned inverter capacity is available to command."
            : !gate.control_allowed   ? "The grid-evidence gate has not confirmed a stable source."
            : !source.control_allowed ? "The source carrying the plant is not settled."
            /* Ahead of the generic policy answer: while a generator carries the
             * plant, an unestablished running set is the specific and actionable
             * reason PV is held at zero. */
            : source.mode == SOURCE_MODE_GENERATOR_ONLY && !fleet_limit.known
                                      ? generator_fleet_inhibit(fleet_limit.reason)
            : !policy.valid           ? "The control policy produced no valid command this cycle."
                                      : "";
        strlcpy(next.inhibit_reason, inhibit, sizeof(next.inhibit_reason));

        portENTER_CRITICAL(&s_lock);
        if (next.command_authority != s_status.command_authority) {
            next.last_authority_change_ms = timestamp;
        } else {
            next.last_authority_change_ms = s_status.last_authority_change_ms;
        }
        /* Only an accepted write advances this; a failed command must not look
         * like a successful one. */
        next.last_command_ms = command_accepted ? timestamp : s_status.last_command_ms;
        s_status = next;
        portEXIT_CRITICAL(&s_lock);

        uint32_t delay_ms = s_config.interval_ms;
        if (delay_ms < 100U) delay_ms = 100U;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t control_engine_init(void)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    esp_err_t error = config_manager_get_snapshot(config);
    if (error != ESP_OK) {
        free(config);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(error));
        return error;
    }
    s_config = config->control;
    /* Resolved once here rather than every cycle: meter configuration changes
     * already require a restart, and an app_config_t must not go on this task's
     * 4 kB stack. */
    const meter_role_assignment_t roles = config_manager_role_assignment(config);
    portENTER_CRITICAL(&s_lock);
    s_roles = roles;
    portEXIT_CRITICAL(&s_lock);
    s_grid_phase_basis = roles.valid && roles.grid_index != METER_ROLE_INDEX_NONE
                             ? config->meters[roles.grid_index].phase_control_basis
                             : METER_PHASE_BASIS_LOWEST_PHASE;

    /* Commissioning evidence from persisted configuration. Each `known` flag is
     * set only now that the corresponding state has actually been read. */
    s_commissioning_inputs.meter_roles_known = true;
    s_commissioning_inputs.meter_roles_valid = roles.valid;
    s_commissioning_inputs.grid_meter_count = roles.grid_count;
    s_commissioning_inputs.duplicate_generator_slot = roles.duplicate_generator;

    /* THE ONLY PLACE THE PHASE-SCOPE PREDICATE IS EVALUATED. Counted over
     * ENABLED meters only: a disabled meter is not wired into anything the
     * controller acts on, so demanding a model for it would block commissioning
     * on a row an engineer left behind. Bounded by both meter_count and
     * APP_MAX_METERS because a stored count is data, not a promise. */
    s_commissioning_inputs.meter_models_known = true;
    s_commissioning_inputs.enabled_meter_count = 0U;
    s_commissioning_inputs.in_scope_meter_count = 0U;
    s_commissioning_inputs.undeclared_meter_count = 0U;
    for (uint8_t index = 0; index < config->meter_count && index < APP_MAX_METERS; ++index) {
        const meter_config_t *meter = &config->meters[index];
        if (!meter->enabled) continue;
        s_commissioning_inputs.enabled_meter_count++;
        if (meter->model == METER_MODEL_UNDECLARED) {
            s_commissioning_inputs.undeclared_meter_count++;
        } else if (meter_model_in_phase_scope(meter->model)) {
            s_commissioning_inputs.in_scope_meter_count++;
        }
    }

    s_commissioning_inputs.ramp_policy_known = true;
    s_commissioning_inputs.generator_ramp_enabled = s_config.generator_ramp.enabled;
    s_commissioning_inputs.generator_ramp_up_percent_per_second =
        s_config.generator_ramp.up_percent_per_second;
    s_commissioning_inputs.generator_ramp_down_percent_per_second =
        s_config.generator_ramp.down_percent_per_second;

    s_commissioning_inputs.control_tuning_known = true;
    s_commissioning_inputs.kp = s_config.kp;
    s_commissioning_inputs.ki = s_config.ki;
    s_commissioning_inputs.deadband_kw = s_config.deadband_kw;
    s_commissioning_inputs.interval_ms = s_config.interval_ms;
    s_commissioning_inputs.meter_stale_timeout_ms = s_config.meter_stale_timeout_ms;
    free(config);

    inverter_fleet_commissioning_t fleet = {0};
    inverter_manager_commissioning_summary(&fleet);
    s_commissioning_inputs.inverter_fleet_known = fleet.known;
    s_commissioning_inputs.enabled_inverter_count = fleet.enabled_count;
    s_commissioning_inputs.write_qualified_inverter_count = fleet.write_qualified_count;
    s_commissioning_inputs.lab_only_inverter_count = fleet.lab_only_count;
    s_commissioning_inputs.readback_capable_inverter_count = fleet.readback_capable_count;
    s_commissioning_inputs.commissioned_capacity_kw = fleet.commissioned_capacity_kw;

    /* Publish the fail-closed evaluation immediately. If any step below returns
     * early, this is what the API and the control task will keep seeing. */
    {
        const commissioning_status_t initial = commissioning_gate_evaluate(&s_commissioning_inputs);
        store_commissioning(&initial);
    }

    if (!roles.valid) {
        ESP_LOGW(TAG, "Automatic Solar-Grid control remains fail-closed: %s",
                 roles.grid_count == 0U
                     ? "no enabled meter is assigned the grid role"
                     : roles.grid_count > 1U
                           ? "more than one enabled meter is assigned the grid role"
                           : "two meters claim the same generator slot");
    }

    error = solar_grid_config_get_snapshot(&s_grid_config);
    /* The snapshot was read, so this part of the state is now KNOWN even when it
     * turns out to be invalid - that is a specific, reportable failure rather
     * than an unreadable one. A failed read leaves the flag false. */
    s_commissioning_inputs.grid_policy_known = error == ESP_OK;
    s_commissioning_inputs.grid_policy_valid =
        error == ESP_OK && solar_grid_config_valid(&s_grid_config);
    /* Per-engine generator policy. `referenced_by_meter` is what makes a hole
     * visible: a meter attributed to a generator slot that carries no commissioned
     * rating means the site can run an engine the policy does not describe, and no
     * aggregate minimum-loading floor can be computed for that configuration. */
    s_commissioning_inputs.generator_limits_known = error == ESP_OK;
    /* A failed read leaves the mode UNSET rather than whatever was there before, so
     * an unreadable configuration can never present itself as a commissioned one. */
    s_commissioning_inputs.generator_load_sharing_mode =
        error == ESP_OK ? solar_grid_config_load_sharing_mode(&s_grid_config)
                        : (uint8_t)COMMISSIONING_SHARING_UNSET;
    /* A failed read leaves the base-load setpoint-agreement tolerance uncommissioned
     * rather than whatever was there before, for the same reason as the mode above: an
     * unreadable configuration must never present itself as a commissioned one. */
    s_commissioning_inputs.generator_base_load_tolerance_kw =
        error == ESP_OK ? solar_grid_config_base_load_tolerance_kw(&s_grid_config) : 0.0f;
    s_commissioning_inputs.generator_base_load_tolerance_percent_of_rating =
        error == ESP_OK ? solar_grid_config_base_load_tolerance_percent(&s_grid_config)
                        : 0.0f;
    for (uint8_t slot = 0U; slot < APP_MAX_GENERATORS; ++slot) {
        const solar_grid_generator_limits_t limits =
            error == ESP_OK ? solar_grid_config_generator(&s_grid_config, slot)
                            : (solar_grid_generator_limits_t){0};
        s_commissioning_inputs.generators[slot].enabled = limits.enabled;
        s_commissioning_inputs.generators[slot].rated_kw = limits.rated_kw;
        s_commissioning_inputs.generators[slot].minimum_loading_percent =
            limits.minimum_loading_percent;
        s_commissioning_inputs.generators[slot].referenced_by_meter =
            roles.generator_index[slot] != METER_ROLE_INDEX_NONE;
        s_commissioning_inputs.generators[slot].role =
            error == ESP_OK ? solar_grid_config_engine_role(&s_grid_config, slot)
                            : (uint8_t)COMMISSIONING_ENGINE_ROLE_UNSET;
        s_commissioning_inputs.generators[slot].base_load_kw =
            error == ESP_OK ? solar_grid_config_engine_base_load_kw(&s_grid_config, slot)
                            : 0.0f;
    }
    s_commissioning_inputs.source_detection_known = true;
    s_commissioning_inputs.grid_evidence_configured =
        error == ESP_OK && solar_grid_config_evidence_complete(&s_grid_config);
    /* The whole collection succeeded, so the gate may now distinguish a specific
     * unmet prerequisite from "unreadable". */
    s_commissioning_inputs.state_readable = true;
    {
        const commissioning_status_t collected =
            commissioning_gate_evaluate(&s_commissioning_inputs);
        store_commissioning(&collected);
        if (!collected.commissioned) {
            ESP_LOGW(TAG,
                     "Automatic control remains gated: %u of %u commissioning prerequisites unmet, first is '%s' - %s",
                     (unsigned)collected.unmet_count,
                     (unsigned)COMMISSIONING_PREREQ_COUNT,
                     commissioning_prereq_id(collected.first_unmet),
                     commissioning_gate_summary(&collected));
        }
    }
    if (error != ESP_OK || !solar_grid_config_valid(&s_grid_config)) {
        ESP_LOGE(TAG, "Solar-Grid configuration unavailable or invalid");
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }

    grid_evidence_runtime_t evidence = {
        .configured = solar_grid_config_evidence_complete(&s_grid_config),
        .last_error = ESP_OK,
    };
    evidence_store(&evidence);

    portENTER_CRITICAL(&s_lock);
    s_runtime_forced_disabled = false;
    s_safe_zero_pending = false;
    s_status = (control_status_t){
        .enabled = s_config.enabled,
        .mode = s_config.enabled ? APP_MODE_FAILSAFE : APP_MODE_DISABLED,
        .grid_power_kw = NAN,
        .raw_grid_power_kw = NAN,
        .grid_target_kw = configured_grid_target(),
        .error_kw = NAN,
        .requested_pv_kw = 0.0f,
        .applied_pv_kw = 0.0f,
        .grid_policy = (uint8_t)s_grid_config.policy,
        .source_mode = SOURCE_MODE_UNKNOWN,
        .grid_gate_state = evidence.configured ? GRID_GATE_WAITING_EVIDENCE
                                               : GRID_GATE_UNCONFIGURED,
        .grid_evidence_configured = evidence.configured,
    };
    portEXIT_CRITICAL(&s_lock);

    if (evidence.configured &&
        xTaskCreatePinnedToCore(grid_evidence_task, "grid_evidence", 4096, NULL, 9, NULL,
                                PVDG_CONTROL_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(control_task, "pvdg_control", 4096, NULL, 10, NULL,
                            PVDG_CONTROL_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (!evidence.configured) {
        ESP_LOGW(TAG, "Automatic Solar-Grid control remains fail-closed: explicit grid availability and breaker evidence are not configured");
    } else {
        ESP_LOGI(TAG, "Solar-Grid policy '%s' loaded with explicit Modbus grid evidence",
                 solar_grid_policy_name(s_grid_config.policy));
    }
    return ESP_OK;
}

void control_engine_get_status(control_status_t *out_status)
{
    if (!out_status) return;
    portENTER_CRITICAL(&s_lock);
    *out_status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

void control_engine_get_commissioning(commissioning_status_t *out_status)
{
    if (!out_status) return;
    commissioning_status_t snapshot;
    bool valid;
    portENTER_CRITICAL(&s_commissioning_lock);
    snapshot = s_commissioning;
    valid = s_commissioning_valid;
    portEXIT_CRITICAL(&s_commissioning_lock);
    /* Nothing has been evaluated yet, so report the fully fail-closed answer
     * rather than a zeroed struct that would read as "no unmet prerequisites". */
    if (!valid) snapshot = commissioning_gate_evaluate(NULL);
    *out_status = snapshot;
}

void control_engine_force_disable(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_status.enabled || s_status.applied_pv_kw > 0.0f) {
        s_safe_zero_pending = true;
    }
    s_runtime_forced_disabled = true;
    s_status.enabled = false;
    s_status.mode = APP_MODE_DISABLED;
    s_status.requested_pv_kw = 0.0f;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "Runtime control disable latched; control task will confirm safe zero before clearing the pending state");
}

esp_err_t control_engine_set_enabled(bool enabled)
{
    if (!enabled) {
        control_engine_force_disable();
        return ESP_OK;
    }

    /* The gate as the last control cycle evaluated it. Deliberately not
     * recomputed here: recomputing would answer from configuration, while the
     * loop's verdict also carries what it observed on the bus, and the loop's
     * verdict is the one that will decide whether anything is commanded. */
    commissioning_status_t commissioning;
    control_engine_get_commissioning(&commissioning);
    if (!commissioning.commissioned) {
        ESP_LOGW(TAG, "Arm refused: commissioning gate not satisfied (%u unmet)",
                 (unsigned)commissioning.unmet_count);
        return ESP_ERR_INVALID_STATE;
    }

    /* A latched disable is not finished until the loop has actually driven the
     * fleet to zero. Arming across that window would leave the safe-zero write
     * racing the first commanded setpoint, and which one landed last would
     * depend on timing. */
    bool settling = false;
    portENTER_CRITICAL(&s_lock);
    settling = s_safe_zero_pending;
    if (!settling) {
        s_config.enabled = true;
        s_runtime_forced_disabled = false;
    }
    portEXIT_CRITICAL(&s_lock);

    if (settling) {
        ESP_LOGW(TAG, "Arm refused: a previous disable has not yet confirmed safe zero");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "Automatic control armed; the commissioning gate is re-evaluated every cycle");
    return ESP_OK;
}
