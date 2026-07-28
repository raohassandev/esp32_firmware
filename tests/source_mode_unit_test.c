#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "source_mode.h"

static void test_modes(void)
{
    source_evidence_t e = {0};
    source_mode_result_t r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_UNKNOWN);
    assert(!r.control_allowed);

    e.evidence_fresh = true;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_NO_SOURCE);

    e.grid_available = true;
    e.grid_breaker_closed = true;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_GRID_ONLY && r.control_allowed);

    e.grid_breaker_closed = false;
    e.generator_running = true;
    e.generator_breaker_closed = true;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_GENERATOR_ONLY && r.control_allowed);

    e.grid_available = false;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_ISLAND && r.control_allowed);

    e.grid_available = true;
    e.grid_breaker_closed = true;
    e.grid_generator_synchronized = true;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_GRID_GENERATOR_SYNC && r.control_allowed);

    e.grid_generator_synchronized = false;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_CONFLICT && !r.control_allowed && r.evidence_conflict);

    e.transfer_active = true;
    r = source_mode_evaluate(&e);
    assert(r.mode == SOURCE_MODE_TRANSFER && r.transition_active && !r.control_allowed);
}

static void test_generator_fleet(void)
{
    generator_channel_evidence_t channels[SOURCE_MAX_GENERATORS] = {
        {
            .configured = true, .evidence_fresh = true, .running = true,
            .breaker_closed = true, .rated_kw = 100.0f, .measured_kw = 45.0f,
            .minimum_loading_percent = 30.0f, .reserve_kw = 5.0f,
            .reverse_power_margin_kw = 2.0f,
        },
        {
            .configured = true, .evidence_fresh = true, .running = true,
            .breaker_closed = true, .rated_kw = 200.0f, .measured_kw = 90.0f,
            .minimum_loading_percent = 25.0f, .reserve_kw = 10.0f,
            .reverse_power_margin_kw = 3.0f,
        },
    };
    generator_fleet_result_t r = source_mode_aggregate_generators(channels);
    assert(r.valid && !r.conflict && r.running_count == 2U);
    assert(fabsf(r.running_rated_kw - 300.0f) < 0.001f);
    assert(fabsf(r.measured_total_kw - 135.0f) < 0.001f);
    assert(fabsf(r.required_minimum_kw - 100.0f) < 0.001f);

    channels[1].breaker_closed = true;
    channels[1].running = false;
    r = source_mode_aggregate_generators(channels);
    assert(!r.valid && r.conflict);

    channels[1].running = true;
    channels[1].measured_kw = NAN;
    r = source_mode_aggregate_generators(channels);
    assert(!r.valid && r.conflict);
}

static void test_generator_limit(void)
{
    generator_limit_input_t in = {
        .evidence_fresh = true,
        .facility_load_kw = 300.0f,
        .running_generator_rated_kw = 200.0f,
        .minimum_loading_percent = 30.0f,
        .reserve_kw = 20.0f,
        .reverse_power_margin_kw = 5.0f,
    };
    assert(fabsf(source_mode_generator_safe_pv_kw(&in) - 215.0f) < 0.001f);

    in.facility_load_kw = 50.0f;
    assert(source_mode_generator_safe_pv_kw(&in) == 0.0f);

    in.facility_load_kw = NAN;
    assert(source_mode_generator_safe_pv_kw(&in) == 0.0f);

    in.facility_load_kw = 300.0f;
    in.evidence_fresh = false;
    assert(source_mode_generator_safe_pv_kw(&in) == 0.0f);
}

int main(void)
{
    test_modes();
    test_generator_fleet();
    test_generator_limit();
    puts("source mode unit tests passed");
    return 0;
}
