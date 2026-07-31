/*
 * END-TO-END: EM500 tariff register -> source detection -> source mode ->
 * curtailment policy -> the exact word written to the inverter's power-limit
 * register.
 *
 * This is the controller's primary function, so it is proved across the real
 * production translation units rather than a model of them. Every stage below is
 * the shipped source file:
 *
 *   components/source_detection/source_detection_engine.c  tariff word -> state
 *   components/control_engine/source_mode.c                state -> source mode
 *   components/control_engine/power_control_policy.c       mode -> PV setpoint
 *   components/inverter_manager/inverter_profiles.c        the profile scale
 *   components/inverter_manager/inverter_profile_decode.c  setpoint -> wire word
 *
 * The scenario drives register 0x2100 through 0 -> 1 -> 0, which is what the
 * SolTrix Modbus simulator's meter slave does when its tariff input is flipped,
 * and asserts at every stage. The load case is chosen so the generator ceiling
 * lands on exactly 45 % of the fleet, because 45 % is the percentage whose
 * correct encoding (450) and incorrect encoding (45) differ by the factor of ten
 * that a missing scale would introduce.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "inverter_profile_decode.h"
#include "inverter_profiles.h"
#include "power_control_policy.h"
#include "source_detection_engine.h"
#include "source_mode.h"

#define TARIFF_GRID 0U
#define TARIFF_GENERATOR 1U

#define DEBOUNCE_MS 2000U
#define POLL_MS 500U
#define FLEET_KW 100.0f

/* Chosen so source_mode_generator_safe_pv_kw() returns exactly 45 kW:
 * facility 100 kW minus (200 kW rating x 25 % minimum loading = 50) minus
 * 4 kW reserve minus 1 kW reverse-power margin = 45 kW, which is 45 % of a
 * 100 kW fleet. */
#define GENERATOR_RATED_KW 200.0f
#define GENERATOR_MINIMUM_LOADING_PERCENT 25.0f
#define GENERATOR_RESERVE_KW 4.0f
#define GENERATOR_REVERSE_MARGIN_KW 1.0f
#define FACILITY_LOAD_KW 100.0f
#define EXPECTED_SAFE_PV_KW 45.0f

static source_detection_policy_t site_policy(void)
{
    /* The commissioned EM500 mapping: zero is grid, energised is generator. */
    source_detection_policy_t policy = {
        .mode = SOURCE_DETECTION_MODE_SINGLE_INPUT,
        .debounce_ms = DEBOUNCE_MS,
        .stale_timeout_ms = 5000U,
        .single_grid_value = 0U,
        .single_generator_value = 1U,
        /* The site commissioned an EM500, which is what licenses reading 0x2100
         * as a bitmask. Without this the bitmask words below are refused and the
         * plant stays fail-closed. */
        .single_bitmask_semantics = true,
        .grid_threshold_kw = 1.0f,
        .generator_threshold_kw = 1.0f,
    };
    return policy;
}

/* Mirrors inverter_manager.c: share_kw against the machine's rating, clamped to
 * the profile's own declared percentage envelope. */
static float share_to_percent(const inverter_profile_t *profile, float share_kw,
                              float rated_kw)
{
    assert(profile);
    assert(rated_kw > 0.0f);
    float percent = share_kw <= 0.0f ? 0.0f : 100.0f * share_kw / rated_kw;
    if (percent > 0.0f && percent < profile->minimum_percent) percent = 0.0f;
    if (percent > profile->maximum_percent) percent = profile->maximum_percent;
    return percent;
}

/* Mirrors encode_command() in inverter_manager.c for a one-register integer
 * setpoint, which is what both owner-stated brands use. */
static uint16_t wire_word(const inverter_profile_t *profile, float percent)
{
    assert(profile);
    assert(profile->has_power_limit);
    assert(profile->power_limit_words == 1U);
    uint16_t words[2] = {0U, 0U};
    const double raw = (double)percent * (double)profile->raw_units_per_percent;
    assert(inverter_profile_encode_value(raw, INVERTER_VALUE_U16,
                                         profile->power_limit_word_order,
                                         profile->power_limit_words, words) == ESP_OK);
    return words[0];
}

/* One turn of the whole loop: read the meter word, resolve the source, decide
 * the setpoint. Returns everything the assertions need. */
typedef struct {
    source_detection_result_t detection;
    source_mode_result_t source;
    power_control_output_t control;
    float safe_pv_kw;
} loop_cycle_t;

static loop_cycle_t step_loop(source_detection_memory_t *memory,
                              const source_detection_policy_t *policy,
                              uint16_t tariff_word,
                              uint32_t now_ms,
                              float current_pv_command_kw)
{
    loop_cycle_t cycle = {0};

    const source_detection_evidence_t evidence = {
        .single_has_sample = true,
        .single_raw_value = tariff_word,
        .single_age_ms = 0U,
    };
    cycle.detection = source_detection_step(memory, policy, &evidence, now_ms);

    /* Exactly the translation control_engine.c performs at its detection
     * fallback: only a resolved, fresh, non-pending, non-fail-closed detection
     * is allowed to name a source. */
    measured_source_t measured = MEASURED_SOURCE_UNKNOWN;
    if (cycle.detection.state == SOURCE_STATE_GRID) measured = MEASURED_SOURCE_GRID;
    else if (cycle.detection.state == SOURCE_STATE_GENERATOR) measured = MEASURED_SOURCE_GENERATOR;
    const bool measured_fresh = cycle.detection.evidence_fresh &&
                                !cycle.detection.fail_closed &&
                                !cycle.detection.transition_pending;
    cycle.source = source_mode_from_measured_source(measured, measured_fresh);

    const generator_limit_input_t limit_input = {
        .evidence_fresh = true,
        .facility_load_kw = FACILITY_LOAD_KW,
        .running_generator_rated_kw = GENERATOR_RATED_KW,
        .minimum_loading_percent = GENERATOR_MINIMUM_LOADING_PERCENT,
        .reserve_kw = GENERATOR_RESERVE_KW,
        .reverse_power_margin_kw = GENERATOR_REVERSE_MARGIN_KW,
    };
    cycle.safe_pv_kw = source_mode_generator_safe_pv_kw(&limit_input);

    const power_control_input_t control_input = {
        .measurement_fresh = true,
        .source_mode = cycle.source.mode,
        .policy = GRID_POLICY_ZERO_EXPORT,
        .measured_grid_kw = FACILITY_LOAD_KW,
        .export_limit_kw = 0.0f,
        .minimum_import_kw = 0.0f,
        .current_pv_command_kw = current_pv_command_kw,
        .fleet_capacity_kw = FLEET_KW,
        .kp = 1.0f,
        .ki = 0.0f,
        .deadband_kw = 0.0f,
        .interval_seconds = (float)POLL_MS / 1000.0f,
        /* Unlimited ramp: this test is about WHICH ceiling applies, not how long
         * the controller takes to walk to it. Ramp behaviour is proved by the
         * control-engine tests. */
        .ramp_up_kw_per_second = 1.0e6f,
        .ramp_down_kw_per_second = 1.0e6f,
        .integral_kw = 0.0f,
        .generator_safe_limit_kw = cycle.safe_pv_kw,
    };
    cycle.control = power_control_step(&control_input);
    return cycle;
}

/* The generator ceiling must be the stated 45 kW, or the 450/45 assertion below
 * would be testing the wrong number. */
static void test_generator_ceiling_is_the_expected_forty_five_kilowatts(void)
{
    const generator_limit_input_t input = {
        .evidence_fresh = true,
        .facility_load_kw = FACILITY_LOAD_KW,
        .running_generator_rated_kw = GENERATOR_RATED_KW,
        .minimum_loading_percent = GENERATOR_MINIMUM_LOADING_PERCENT,
        .reserve_kw = GENERATOR_RESERVE_KW,
        .reverse_power_margin_kw = GENERATOR_REVERSE_MARGIN_KW,
    };
    assert(fabsf(source_mode_generator_safe_pv_kw(&input) - EXPECTED_SAFE_PV_KW) < 0.001f);
}

static void test_tariff_zero_one_zero_drives_curtailment_and_release(void)
{
    const inverter_profile_t *huawei = inverter_profiles_find("huawei.sun2000.pending");
    assert(huawei);

    const source_detection_policy_t policy = site_policy();
    source_detection_memory_t memory = {0};
    uint32_t now = 0U;
    float pv_command_kw = 0.0f;

    /* ---- PHASE 1: tariff 0. The plant is on grid. ---------------------- */
    loop_cycle_t cycle = step_loop(&memory, &policy, TARIFF_GRID, now, pv_command_kw);
    assert(cycle.detection.candidate_state == SOURCE_STATE_GRID);
    /* Fail-closed on the very first sample: a source that has not yet held for
     * the debounce interval commands nothing. */
    assert(cycle.detection.state == SOURCE_STATE_UNKNOWN);
    assert(cycle.detection.reason == SOURCE_REASON_DEBOUNCE_PENDING);
    assert(cycle.detection.transition_pending);
    assert(cycle.detection.fail_closed);
    assert(cycle.source.mode == SOURCE_MODE_UNKNOWN);
    assert(!cycle.control.valid);

    /* The debounce MUST eventually complete. Polling at the meter's own rate,
     * grid resolves within the configured interval and stays resolved. */
    uint32_t cycles_to_settle = 0U;
    while (cycle.detection.state != SOURCE_STATE_GRID) {
        now += POLL_MS;
        cycles_to_settle++;
        assert(cycles_to_settle <= (DEBOUNCE_MS / POLL_MS) + 1U);
        cycle = step_loop(&memory, &policy, TARIFF_GRID, now, pv_command_kw);
    }
    assert(cycle.detection.tariff == SOURCE_TARIFF_1);
    assert(cycle.detection.control_allowed);
    assert(!cycle.detection.fail_closed);
    assert(cycle.source.mode == SOURCE_MODE_GRID_ONLY);
    assert(cycle.source.control_allowed);
    assert(cycle.control.valid);
    /* On grid the generator ceiling does not apply: PV may run to the full
     * fleet capacity even though a safe-PV figure is computable. */
    assert(!cycle.control.curtailed_by_generator);
    assert(cycle.control.requested_pv_kw > EXPECTED_SAFE_PV_KW);
    assert(fabsf(cycle.control.requested_pv_kw - FLEET_KW) < 0.001f);
    pv_command_kw = cycle.control.requested_pv_kw;

    /* Full output on grid encodes as 1000 on a x10 register, never 100. */
    const float grid_percent = share_to_percent(huawei, pv_command_kw, FLEET_KW);
    assert(fabsf(grid_percent - 100.0f) < 0.001f);
    assert(wire_word(huawei, grid_percent) == 1000U);

    /* ---- PHASE 2: tariff flips to 1. The genset takes the plant. ------- */
    now += POLL_MS;
    cycle = step_loop(&memory, &policy, TARIFF_GENERATOR, now, pv_command_kw);
    assert(cycle.detection.candidate_state == SOURCE_STATE_GENERATOR);
    assert(cycle.detection.state == SOURCE_STATE_UNKNOWN);
    assert(cycle.detection.reason == SOURCE_REASON_DEBOUNCE_PENDING);
    assert(cycle.detection.fail_closed);

    cycles_to_settle = 0U;
    while (cycle.detection.state != SOURCE_STATE_GENERATOR) {
        now += POLL_MS;
        cycles_to_settle++;
        assert(cycles_to_settle <= (DEBOUNCE_MS / POLL_MS) + 1U);
        cycle = step_loop(&memory, &policy, TARIFF_GENERATOR, now, pv_command_kw);
    }
    assert(cycle.detection.tariff == SOURCE_TARIFF_2);
    assert(cycle.detection.control_allowed);
    assert(cycle.source.mode == SOURCE_MODE_GENERATOR_ONLY);
    assert(cycle.source.control_allowed);

    /* THE POINT OF THE PRODUCT: on generator, PV is held below the ceiling that
     * keeps load on the machine. */
    assert(cycle.control.valid);
    assert(cycle.control.curtailed_by_generator);
    assert(fabsf(cycle.safe_pv_kw - EXPECTED_SAFE_PV_KW) < 0.001f);
    assert(cycle.control.requested_pv_kw <= EXPECTED_SAFE_PV_KW + 0.001f);
    assert(fabsf(cycle.control.requested_pv_kw - EXPECTED_SAFE_PV_KW) < 0.001f);
    assert(cycle.control.requested_pv_kw < FLEET_KW);
    pv_command_kw = cycle.control.requested_pv_kw;

    /* THE WIRE. 45 % on a Huawei x10 register at 40125 is the word 450. */
    const float generator_percent = share_to_percent(huawei, pv_command_kw, FLEET_KW);
    assert(fabsf(generator_percent - 45.0f) < 0.001f);
    assert(huawei->power_limit_address == 40125U);
    assert(huawei->power_limit_function == 6U);
    assert(wire_word(huawei, generator_percent) == 450U);
    /* The silent, dangerous failure: 45 on the wire would command 4.5 %. */
    assert(wire_word(huawei, generator_percent) != 45U);
    /* And it is not the Solis encoding either. */
    assert(wire_word(huawei, generator_percent) != 4500U);

    /* The same setpoint on a Solis x100 register is 4500, not 450. */
    const inverter_profile_t *solis = inverter_profiles_find("solis.commercial.pending");
    assert(solis);
    assert(wire_word(solis, generator_percent) == 4500U);
    assert(wire_word(solis, generator_percent) != wire_word(huawei, generator_percent));

    /* The controller's own readback decodes the word back to the percent it
     * meant, in the opposite direction, through the profile's readback scale. */
    float readback = NAN;
    const uint16_t echoed[1] = {450U};
    assert(inverter_profile_decode_value(echoed, 1U, huawei->power_limit_readback_type,
                                         huawei->power_limit_readback_word_order,
                                         huawei->power_limit_readback_scale,
                                         &readback) == ESP_OK);
    assert(fabsf(readback - 45.0f) < 0.001f);
    assert(inverter_profile_readback_matches(generator_percent, readback,
                                             huawei->readback_tolerance_percent));
    /* A 45 echoed back must NOT satisfy the confirmation for a 45 % request. */
    const uint16_t wrong_echo[1] = {45U};
    assert(inverter_profile_decode_value(wrong_echo, 1U, huawei->power_limit_readback_type,
                                         huawei->power_limit_readback_word_order,
                                         huawei->power_limit_readback_scale,
                                         &readback) == ESP_OK);
    assert(!inverter_profile_readback_matches(generator_percent, readback,
                                              huawei->readback_tolerance_percent));

    /* ---- PHASE 3: tariff returns to 0. The grid is back. --------------- */
    now += POLL_MS;
    cycle = step_loop(&memory, &policy, TARIFF_GRID, now, pv_command_kw);
    assert(cycle.detection.candidate_state == SOURCE_STATE_GRID);
    assert(cycle.detection.state == SOURCE_STATE_UNKNOWN);
    assert(cycle.detection.reason == SOURCE_REASON_DEBOUNCE_PENDING);

    cycles_to_settle = 0U;
    while (cycle.detection.state != SOURCE_STATE_GRID) {
        now += POLL_MS;
        cycles_to_settle++;
        assert(cycles_to_settle <= (DEBOUNCE_MS / POLL_MS) + 1U);
        cycle = step_loop(&memory, &policy, TARIFF_GRID, now, pv_command_kw);
    }
    assert(cycle.detection.tariff == SOURCE_TARIFF_1);
    assert(cycle.source.mode == SOURCE_MODE_GRID_ONLY);
    assert(cycle.control.valid);
    assert(!cycle.control.curtailed_by_generator);
    assert(cycle.control.requested_pv_kw > EXPECTED_SAFE_PV_KW);
    assert(wire_word(huawei, share_to_percent(huawei, cycle.control.requested_pv_kw,
                                              FLEET_KW)) == 1000U);
}

/* A bitmask word from a second wired digital input must curtail exactly as a
 * plain 1 does. Before the bitmask rule this read as UNKNOWN, control stayed
 * fail-closed, and the generator was never protected. */
static void test_bitmask_tariff_word_still_curtails(void)
{
    const source_detection_policy_t policy = site_policy();

    for (uint16_t word = 1U; word != 0U; word = (uint16_t)(word << 1)) {
        source_detection_memory_t memory = {0};
        loop_cycle_t cycle = {0};
        uint32_t now = 0U;
        for (uint32_t i = 0U; i < (DEBOUNCE_MS / POLL_MS) + 2U; ++i) {
            cycle = step_loop(&memory, &policy, word, now, 0.0f);
            now += POLL_MS;
        }
        assert(cycle.detection.state == SOURCE_STATE_GENERATOR);
        assert(cycle.source.mode == SOURCE_MODE_GENERATOR_ONLY);
        assert(cycle.control.valid);
        assert(cycle.control.curtailed_by_generator);
        assert(cycle.control.requested_pv_kw <= EXPECTED_SAFE_PV_KW + 0.001f);
    }
}

/* Zero is the ONLY word that releases curtailment. Asserted over the whole
 * 16-bit space so no value can quietly read as grid. */
static void test_only_zero_reads_as_grid(void)
{
    const source_detection_policy_t policy = site_policy();
    const source_detection_evidence_t grid_evidence = {
        .single_has_sample = true, .single_raw_value = 0U, .single_age_ms = 0U,
    };
    assert(source_detection_observe(&policy, &grid_evidence).candidate_state ==
           SOURCE_STATE_GRID);

    for (uint32_t word = 1U; word <= 0xFFFFU; ++word) {
        const source_detection_evidence_t evidence = {
            .single_has_sample = true,
            .single_raw_value = (uint16_t)word,
            .single_age_ms = 0U,
        };
        assert(source_detection_observe(&policy, &evidence).candidate_state ==
               SOURCE_STATE_GENERATOR);
    }
}

/* Stale evidence must not hold the plant in grid mode. If the meter stops
 * answering, the last word is not trusted and control fails closed. */
static void test_stale_tariff_fails_closed_rather_than_assuming_grid(void)
{
    const source_detection_policy_t policy = site_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .single_has_sample = true, .single_raw_value = TARIFF_GRID, .single_age_ms = 0U,
    };
    for (uint32_t i = 0U; i < 6U; ++i) {
        (void)source_detection_step(&memory, &policy, &evidence, i * POLL_MS);
    }
    assert(source_detection_step(&memory, &policy, &evidence, 6U * POLL_MS).state ==
           SOURCE_STATE_GRID);

    evidence.single_age_ms = policy.stale_timeout_ms + 1U;
    const source_detection_result_t stale =
        source_detection_step(&memory, &policy, &evidence, 7U * POLL_MS);
    assert(stale.state == SOURCE_STATE_UNKNOWN);
    assert(stale.reason == SOURCE_REASON_EVIDENCE_STALE);
    assert(stale.fail_closed);
    assert(!stale.control_allowed);
    assert(source_mode_from_measured_source(MEASURED_SOURCE_UNKNOWN, false).mode ==
           SOURCE_MODE_UNKNOWN);
}

int main(void)
{
    test_generator_ceiling_is_the_expected_forty_five_kilowatts();
    test_tariff_zero_one_zero_drives_curtailment_and_release();
    test_bitmask_tariff_word_still_curtails();
    test_only_zero_reads_as_grid();
    test_stale_tariff_fails_closed_rather_than_assuming_grid();
    puts("EM500 tariff -> generator curtailment end-to-end tests passed");
    return 0;
}
