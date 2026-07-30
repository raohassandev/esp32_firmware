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
 * "no valid command". */
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
        float measured_grid_kw = oriented_grid_power(raw_grid_kw);

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
            source_detection_status_t detection;
            if (source_detection_get_status(&detection) == ESP_OK) {
                measured_fresh = detection.configured && detection.evidence_fresh &&
                                 !detection.fail_closed && !detection.transition_pending;
                detection_configured = detection.configured;
                if (detection.state == SOURCE_STATE_GRID) measured = MEASURED_SOURCE_GRID;
                else if (detection.state == SOURCE_STATE_GENERATOR) measured = MEASURED_SOURCE_GENERATOR;
            }
            source = source_mode_from_measured_source(measured, measured_fresh);
            evidence_fresh = measured_fresh;
        }
        grid_gate_input_t gate_input = {
            .configured = evidence.configured,
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
        if (source.mode == SOURCE_MODE_GENERATOR_ONLY) {
            generator_fleet_input_t fleet_input = {
                .evidence_fresh = measurement_fresh,
                .facility_load_kw = fabsf(measured_grid_kw),
                /* Only when the site has no generator-role meter at all: then a
                 * single commissioned engine is unambiguous, which is the legacy
                 * single-generator behaviour. */
                .allow_unmetered_single_engine = roles.generator_count == 0U,
                .engine_count = APP_MAX_GENERATORS,
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
            .ramp_down_kw_per_second = ramp_kw_per_second(&ramp, false, fleet_capacity_kw,
                                                          fleet_valid, interval_seconds),
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
            esp_err_t write_result = ESP_OK;
            if (first_command || changed || keepalive_due) {
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

    /* Commissioning evidence from persisted configuration. Each `known` flag is
     * set only now that the corresponding state has actually been read. */
    s_commissioning_inputs.meter_roles_known = true;
    s_commissioning_inputs.meter_roles_valid = roles.valid;
    s_commissioning_inputs.grid_meter_count = roles.grid_count;
    s_commissioning_inputs.duplicate_generator_slot = roles.duplicate_generator;

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
