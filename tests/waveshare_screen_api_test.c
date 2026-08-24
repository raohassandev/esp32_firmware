#include <assert.h>
#include <string.h>

#include "screen_api.h"

static void test_live_unknown_is_not_zero(void)
{
    const char *json =
        "{\"grid_kw\":null,\"solar_kw\":81.25,\"requested_pv_kw\":45.0,"
        "\"applied_pv_kw\":null,\"control_enabled\":false,"
        "\"mode_label\":\"Monitoring only\",\"meter_online\":false,"
        "\"command\":{\"percent\":45,\"in_force\":false,\"blocked_by\":\"commissioning\"}}";
    screen_live_snapshot_t s;
    assert(screen_api_parse_live_json(json, &s));
    assert(s.valid);
    assert(!s.has_grid_kw);
    assert(s.has_solar_kw && s.solar_kw == 81.25);
    assert(s.has_requested_pv_kw && s.requested_pv_kw == 45.0);
    assert(!s.has_applied_pv_kw);
    assert(!s.control_enabled);
    assert(strcmp(s.mode_label, "Monitoring only") == 0);
    assert(s.has_command_percent && s.command_percent == 45.0);
    assert(!s.command_in_force);
    assert(strcmp(s.command_blocked_by, "commissioning") == 0);
}

static void test_status_fail_closed_source_and_alarm_names(void)
{
    const char *json =
        "{\"network_online\":true,\"rssi\":-61,\"firmware_version\":\"abc123\","
        "\"meter_online\":true,\"meter_has_data\":true,\"meter_stale\":false,"
        "\"alarms\":3,\"alarm_names\":[\"Meter offline\",\"Meter data stale\"],"
        "\"source\":{\"attributed_to\":\"unknown\"},"
        "\"controller\":{\"uptime_ms\":123456,\"state\":\"healthy\","
        "\"last_reboot_unexpected\":false},"
        "\"control_authority\":{\"mode_label\":\"Monitoring only\","
        "\"inhibit_reason\":\"automatic control disabled\"}}";
    screen_status_snapshot_t s;
    assert(screen_api_parse_status_json(json, &s));
    assert(s.network_online);
    assert(s.rssi == -61);
    assert(strcmp(s.source_attributed_to, "unknown") == 0);
    assert(!s.meter_stale);
    assert(s.alarm_name_count == 2U);
    assert(strcmp(s.alarm_names[0], "Meter offline") == 0);
    assert(strcmp(s.alarm_names[1], "Meter data stale") == 0);
}

static void test_meter_rows_preserve_null(void)
{
    const char *json =
        "{\"configured_count\":2,\"summary\":{\"enabled\":2,\"online\":1,"
        "\"stale_or_unavailable\":1,\"initialization_failed\":0},\"meters\":["
        "{\"index\":0,\"name\":\"Grid\",\"enabled\":true,\"role_name\":\"grid\","
        "\"runtime\":{\"online\":true,\"stale\":false,\"state\":\"online\","
        "\"active_power_kw\":301.5,\"data_age_ms\":220}},"
        "{\"index\":1,\"name\":\"Aux\",\"enabled\":true,\"role_name\":\"auxiliary\","
        "\"runtime\":{\"online\":false,\"stale\":true,\"state\":\"stale\","
        "\"active_power_kw\":null,\"data_age_ms\":null}}]}";
    screen_meters_snapshot_t s;
    assert(screen_api_parse_meters_json(json, &s));
    assert(s.row_count == 2U);
    assert(s.rows[0].has_power_kw && s.rows[0].power_kw == 301.5);
    assert(!s.rows[1].has_power_kw);
    assert(!s.rows[1].has_data_age_ms);
    assert(s.stale_or_unavailable_count == 1U);
}

static void test_inverter_rows(void)
{
    const char *json =
        "{\"configured_count\":2,\"summary\":{\"enabled\":2,\"online\":1,"
        "\"initialization_failed\":0,\"enabled_rated_kw\":200,"
        "\"commandable_rated_kw\":100},\"inverters\":["
        "{\"index\":0,\"name\":\"INV-1\",\"enabled\":true,\"telemetry_supported\":true,"
        "\"measured_power_kw\":79.2,\"measured_age_ms\":400,"
        "\"runtime\":{\"state\":\"last_write_ok\",\"commanded_percent\":55}},"
        "{\"index\":1,\"name\":\"INV-2\",\"enabled\":true,\"telemetry_supported\":true,"
        "\"measured_power_kw\":null,\"measured_age_ms\":null,"
        "\"runtime\":{\"state\":\"not_tested\",\"commanded_percent\":null}}]}";
    screen_inverters_snapshot_t s;
    assert(screen_api_parse_inverters_json(json, &s));
    assert(s.row_count == 2U);
    assert(s.rows[0].has_measured_power_kw && s.rows[0].measured_power_kw == 79.2);
    assert(!s.rows[1].has_measured_power_kw);
    assert(strcmp(s.rows[1].state, "not_tested") == 0);
}

static void test_telemetry_readiness(void)
{
    const char *json =
        "{\"network\":{\"online\":true,\"rssi\":-55},"
        "\"grid_meter\":{\"state\":\"online\",\"active_power_kw\":278.3},"
        "\"meters\":{\"configured\":1,\"enabled\":1,\"online\":1,\"initialization_failed\":0},"
        "\"inverters\":{\"configured\":2,\"enabled\":2,\"initialization_failed\":0},"
        "\"availability\":{\"monitoring_ready\":true,\"command_path_ready\":true,"
        "\"automatic_control_active\":false}}";
    screen_telemetry_snapshot_t s;
    assert(screen_api_parse_telemetry_json(json, &s));
    assert(s.monitoring_ready);
    assert(s.command_path_ready);
    assert(!s.automatic_control_active);
    assert(s.has_grid_power_kw && s.grid_power_kw == 278.3);
}

static void test_events_and_alarms(void)
{
    const char *events =
        "{\"summary\":{\"active_critical\":1,\"active_warning\":0,\"stored_events\":3},"
        "\"events\":[{\"sequence\":3,\"age_ms\":500,\"active\":true,"
        "\"severity\":\"critical\",\"kind\":\"alarm\",\"state\":\"active\","
        "\"title\":\"Grid measurement unavailable\",\"detail\":\"No fresh meter data\","
        "\"recommended_action\":\"Check communications\"}]}";
    screen_events_snapshot_t e;
    assert(screen_api_parse_events_json(events, &e));
    assert(e.active_critical == 1U && e.row_count == 1U);
    assert(e.rows[0].active);

    const char *alarms =
        "{\"summary\":{\"active\":1,\"unacknowledged\":1,\"primary_active\":1,"
        "\"consequential_active\":0},\"alarms\":[{\"code\":2,\"id\":\"MTR-002\","
        "\"title\":\"Meter offline\",\"severity\":\"critical\",\"priority\":\"high\","
        "\"state\":\"unacknowledged\",\"role\":\"primary\",\"caused_by\":null,"
        "\"present\":true,\"acknowledged\":false,\"stale\":false,\"shelved\":false,"
        "\"suppressed_by_design\":false,\"out_of_service\":false,"
        "\"recommended_action\":\"Check meter path\"}]}";
    screen_alarms_snapshot_t a;
    assert(screen_api_parse_alarms_json(alarms, &a));
    assert(a.primary_active_count == 1U && a.row_count == 1U);
    assert(a.rows[0].present && !a.rows[0].acknowledged);
    assert(strcmp(a.rows[0].id, "MTR-002") == 0);
}

int main(void)
{
    test_live_unknown_is_not_zero();
    test_status_fail_closed_source_and_alarm_names();
    test_meter_rows_preserve_null();
    test_inverter_rows();
    test_telemetry_readiness();
    test_events_and_alarms();
    return 0;
}
