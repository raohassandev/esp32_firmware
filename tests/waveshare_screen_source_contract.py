#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "boards" / "waveshare_esp32_s3_touch_lcd_5" / "screen"


def read(path: Path) -> str:
    assert path.exists(), f"missing: {path.relative_to(ROOT)}"
    return path.read_text(encoding="utf-8")


def main() -> None:
    root_cmake = read(ROOT / "CMakeLists.txt")
    screen_cmake = read(SCREEN / "CMakeLists.txt")
    api_h = read(SCREEN / "api" / "screen_api.h")
    api_c = read(SCREEN / "api" / "screen_api.c")
    overview = read(SCREEN / "pages" / "overview_screen.c")
    readiness = read(SCREEN / "pages" / "readiness_screen.c")
    commissioning_ui = read(SCREEN / "pages" / "commissioning_screen.c")
    commissioning_h = read(SCREEN / "pages" / "commissioning_screen.h")
    source_ui = read(SCREEN / "pages" / "source_commissioning_screen.c")
    source_h = read(SCREEN / "pages" / "source_commissioning_screen.h")
    app = read(SCREEN / "screen_app.c")
    runtime = read(SCREEN / "screen_runtime.c")
    profile_h = read(SCREEN / "drivers" / "waveshare_display_profile.h")
    profile_c = read(SCREEN / "drivers" / "waveshare_display_profile.c")
    display_port_h = read(SCREEN / "drivers" / "waveshare_display_port.h")
    display_port_c = read(SCREEN / "drivers" / "waveshare_display_port.c")
    product_main_cmake = read(SCREEN / "product_800x480" / "main" / "CMakeLists.txt")
    product_provider_path = SCREEN / "product_800x480" / "main" / "local_backend_provider.c"
    commissioning_backend_path = SCREEN / "product_800x480" / "main" / "local_commissioning_backend.c"
    source_backend_path = SCREEN / "product_800x480" / "main" / "local_source_commissioning_backend.c"
    product_provider = read(product_provider_path)
    commissioning_backend = read(commissioning_backend_path)
    source_backend = read(source_backend_path)
    source_attribution = read(ROOT / "components" / "source_detection" / "source_attribution.c")
    auth_h = read(ROOT / "components" / "web_server" / "include" / "engineering_auth.h")

    # The site-tested root build stays unchanged; the exact-board product opts
    # into this isolated component explicitly.
    assert "waveshare_esp32_s3_touch_lcd_5/screen" not in root_cmake
    assert "intentionally NOT added to root EXTRA_COMPONENT_DIRS" in screen_cmake

    # Every operational read contract still points at a route the existing backend owns.
    backend_sources = "\n".join(
        read(p) for p in [
            ROOT / "components" / "web_server" / "live_api.c",
            ROOT / "components" / "web_server" / "web_api.c",
            ROOT / "components" / "web_server" / "device_api.c",
            ROOT / "components" / "web_server" / "operational_api.c",
        ]
    )
    required_paths = [
        "/api/live", "/api/status", "/api/meters", "/api/inverters",
        "/api/telemetry", "/api/operator/events", "/api/operator/alarms",
    ]
    for path in required_paths:
        assert path in api_h, f"screen contract missing {path}"
        assert path in backend_sources, f"backend does not own {path}"

    # UI/runtime/parsers stay backend-agnostic. Three exact-board boundary adapters
    # are deliberate exceptions: cached read models, general Engineering writes,
    # and the narrow Solar-Grid source-evidence write surface.
    forbidden_headers = {
        "control_engine.h", "safety_manager.h", "meter_manager.h",
        "inverter_manager.h", "inverter_profile_store.h", "inverter_profiles.h",
        "config_manager.h", "commissioning_gate.h", "network_manager.h",
        "solar_grid_config.h", "engineering_auth.h", "modbus_tcp.h",
    }
    boundary_adapters = {
        product_provider_path.resolve(), commissioning_backend_path.resolve(),
        source_backend_path.resolve(),
    }
    for source in SCREEN.rglob("*.c"):
        if source.resolve() in boundary_adapters:
            continue
        text = read(source)
        includes = set(re.findall(r'#include\s+[<\"]([^>\"]+)[>\"]', text))
        overlap = forbidden_headers & includes
        assert not overlap, f"{source.relative_to(ROOT)} bypasses backend boundary: {sorted(overlap)}"

    # Operational provider remains strictly read-only cached-state projection.
    for forbidden in [
        "esp_http_client", "http://127.0.0.1", "WIFI_AP_DEF",
        "meter_manager_read_registers", "inverter_manager_set_total_power_kw",
        "control_engine_set_enabled", "control_engine_force_disable",
        "config_manager_save", "config_manager_import_json",
        "config_manager_restore_defaults", "httpd_register_uri_handler",
        "HTTP_POST", "HTTP_PUT", "HTTP_DELETE",
    ]:
        assert forbidden not in product_provider, f"read provider gained forbidden authority: {forbidden}"
    assert "source_detection_attributed_to(&source)" in product_provider
    assert "source_detection_attributed_to" in source_attribution
    assert "status->configured" in source_attribution
    assert "status->evidence_fresh" in source_attribution
    assert "!status->conflict" in source_attribution
    assert 'return "unknown"' in source_attribution
    assert "socket/TCP self-transport removed" in product_provider

    # General commissioning adapter is an authenticated, validated in-process
    # boundary. It must not perform synchronous Modbus transactions or self-HTTP.
    for forbidden in [
        "esp_http_client", "http://127.0.0.1", "WIFI_AP_DEF",
        "meter_manager_read_registers", "inverter_manager_probe_read_only",
        "inverter_manager_set_total_power_kw", "httpd_register_uri_handler",
        "HTTP_POST", "HTTP_PUT", "HTTP_DELETE",
    ]:
        assert forbidden not in commissioning_backend, f"commissioning backend bypasses safe service boundary: {forbidden}"
    assert "engineering_auth_verify_local_credential" in commissioning_backend
    assert "engineering_auth_verify_local_credential" in auth_h
    assert "control_engine_force_disable();" in commissioning_backend
    assert "next->control.enabled = false" in commissioning_backend
    assert "config_manager_save" in commissioning_backend
    assert "solar_grid_config_valid" in commissioning_backend
    assert "solar_grid_config_save" in commissioning_backend
    assert "inverter_profile_store_set" in commissioning_backend
    assert "control_engine_set_enabled" in commissioning_backend
    assert "commissioning_gate_summary" in commissioning_backend
    assert "LOCAL_ENGINEERING_SESSION_MS" in commissioning_backend

    # Source evidence is its own narrow authenticated boundary because those
    # persisted Solar-Grid fields were the last web-only commissioning inputs.
    # It may save only through shared Core validators/persistence and may not do
    # direct Modbus I/O or invent a source verdict.
    for forbidden in [
        "esp_http_client", "http://127.0.0.1", "WIFI_AP_DEF",
        "meter_manager_read_registers", "inverter_manager_probe_read_only",
        "inverter_manager_set_total_power_kw", "httpd_register_uri_handler",
        "HTTP_POST", "HTTP_PUT", "HTTP_DELETE", "source_detection_attributed_to",
    ]:
        assert forbidden not in source_backend, f"source commissioning boundary gained forbidden authority: {forbidden}"
    assert "engineering_auth_verify_local_credential" in source_backend
    assert "solar_grid_config_get_snapshot" in source_backend
    assert "solar_grid_config_valid(&next)" in source_backend
    assert "control_engine_force_disable();" in source_backend
    assert "app.control.enabled = false" in source_backend
    assert "config_manager_save(&app)" in source_backend
    assert "solar_grid_config_save(&next)" in source_backend
    for field in [
        "grid_available", "grid_breaker_closed", "evidence_poll_interval_ms",
        "evidence_stale_timeout_ms", "grid_loss_trip_ms", "grid_recovery_stable_ms",
    ]:
        assert field in source_backend, f"source commissioning missing Core field: {field}"

    # There are no local HTTP handlers or write clients anywhere in the screen
    # workspace. Commissioning goes through validated in-process Core services.
    all_c = "\n".join(read(p) for p in SCREEN.rglob("*.c"))
    assert "httpd_register_uri_handler" not in all_c
    assert "HTTP_POST" not in all_c
    assert "HTTP_PUT" not in all_c
    assert "HTTP_DELETE" not in all_c
    assert "esp_http_client" not in all_c

    # UI pages get callbacks/DTOs only; neither commissioning page may name Core managers.
    for ui_name, ui in [("commissioning", commissioning_ui), ("source", source_ui)]:
        for forbidden in [
            "config_manager", "control_engine", "meter_manager", "inverter_manager",
            "solar_grid_config", "engineering_auth", "httpd_", "esp_http_client",
        ]:
            assert forbidden not in ui, f"{ui_name} page gained Core authority: {forbidden}"
    assert "screen_commissioning_backend_t" in commissioning_h
    assert "source_commission_backend_t" in source_h
    assert "Engineering credential" in commissioning_ui
    assert "Modbus TCP only" in commissioning_ui
    assert "ARM automatic control" in commissioning_ui
    assert "DISARM automatic control" in commissioning_ui
    assert "parse_ulong(s_ui.control_interval, 20U, 3600000U" in commissioning_ui, "touchscreen commissioning must accept the Core shipped 20 ms loop cadence"
    assert "uint8_t plant_page" in commissioning_ui
    assert "Save plant section" in commissioning_ui
    assert "Plant commissioning is split into lightweight sections" in commissioning_ui
    assert "Grid available evidence" in source_ui
    assert "Grid breaker closed evidence" in source_ui
    assert "Enable source evidence" in source_ui
    assert "uint8_t page; /* 0 grid available, 1 breaker closed, 2 timing/enable */" in source_ui
    assert "Save grid-available section" in source_ui
    assert "Save breaker section" in source_ui
    assert "Save timing / enable pair" in source_ui
    assert "if (s_ui.enabled) config->evidence_enabled = checked(s_ui.enabled);" in source_ui

    # No hidden screen scheduler/task: board integration owns cadence and calls
    # bounded refresh lanes under the qualified LVGL locking model.
    assert "xTaskCreate" not in runtime
    assert "vTaskDelay" not in runtime
    assert "screen_runtime_refresh_fast" in runtime
    assert "screen_runtime_refresh_status" in runtime
    assert "screen_runtime_refresh_devices" in runtime
    assert "screen_runtime_refresh_operations" in runtime
    for macro in [
        "SCREEN_API_LIVE_PATH", "SCREEN_API_STATUS_PATH", "SCREEN_API_METERS_PATH",
        "SCREEN_API_INVERTERS_PATH", "SCREEN_API_TELEMETRY_PATH",
        "SCREEN_API_EVENTS_PATH", "SCREEN_API_ALARMS_PATH",
    ]:
        assert macro in runtime

    # Touch callbacks belong only to shell navigation and explicit Engineering forms.
    callback_files = sorted(
        source.relative_to(SCREEN).as_posix()
        for source in SCREEN.rglob("*.c")
        if "lv_obj_add_event_cb" in read(source)
    )
    assert callback_files == [
        "pages/commissioning_screen.c",
        "pages/source_commissioning_screen.c",
        "screen_app.c",
    ], callback_files

    # Fail-closed source attribution: overview never renders raw live.source.
    assert re.search(r"snapshot->source(?!_)", overview) is None
    assert "source_attributed_to" in overview
    assert 'strcmp(status->source_attributed_to, "unknown")' in readiness

    # Null numerics are represented by has_* flags and unavailable display text.
    assert "has_grid_kw" in api_c
    assert "has_solar_kw" in api_c
    assert "cJSON_IsNumber" in api_c
    assert '"-- kW"' in all_c
    assert "no zero" in all_c.lower() or "no zero" in read(SCREEN / "README.md").lower()

    # All UI/profile/physical-port sources are in the isolated component manifest.
    for source in [
        "pages/overview_screen.c", "pages/grid_screen.c", "pages/solar_screen.c",
        "pages/alarms_screen.c", "pages/readiness_screen.c",
        "pages/commissioning_screen.c", "pages/source_commissioning_screen.c",
        "components/screen_widgets.c", "drivers/waveshare_display_profile.c",
        "drivers/waveshare_display_port.c", "screen_app.c", "screen_runtime.c",
    ]:
        assert f'"{source}"' in screen_cmake, f"CMake missing {source}"
    assert '"local_source_commissioning_backend.c"' in product_main_cmake

    # Both exact vendor resolutions exist and no symbol silently picks a default.
    assert "WAVESHARE_DISPLAY_800X480" in profile_h
    assert "WAVESHARE_DISPLAY_1024X600" in profile_h
    assert ".width = 800" in profile_c and ".height = 480" in profile_c
    assert ".width = 1024" in profile_c and ".height = 600" in profile_c
    assert "WAVESHARE_DISPLAY_DEFAULT" not in profile_h
    assert "DEFAULT_WAVESHARE_DISPLAY" not in profile_h

    # IDF6 port uses the new master-bus API, shared-bus injection and vendor
    # CH422G touch-reset sequence.
    assert '"driver/i2c_master.h"' in display_port_h
    assert '"driver/i2c.h"' not in display_port_c
    assert "i2c_new_master_bus" in display_port_c
    assert "i2c_master_bus_add_device" in display_port_c
    assert "i2c_master_transmit" in display_port_c
    assert "config->i2c_bus" in display_port_c
    assert "CH422G_TOUCH_RESET_LOW" in display_port_c
    assert "CH422G_TOUCH_RESET_HIGH" in display_port_c
    assert "esp_lcd_touch_new_i2c_gt911" in display_port_c

    # Existing operator areas remain intact; Engineering commissioning is added,
    # not substituted for read-only readiness/operator pages.
    for label in ["Overview", "Grid", "Solar", "Alarms", "Ready", "Commission", "Source"]:
        assert f'"{label}"' in app

    print("waveshare screen source contract: PASS")


if __name__ == "__main__":
    main()
