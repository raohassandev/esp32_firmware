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
    app = read(SCREEN / "screen_app.c")
    runtime = read(SCREEN / "screen_runtime.c")
    profile_h = read(SCREEN / "drivers" / "waveshare_display_profile.h")
    profile_c = read(SCREEN / "drivers" / "waveshare_display_profile.c")
    display_port_h = read(SCREEN / "drivers" / "waveshare_display_port.h")
    display_port_c = read(SCREEN / "drivers" / "waveshare_display_port.c")

    # The current site-tested build stays unchanged until the hardware/LVGL gate.
    assert "waveshare_esp32_s3_touch_lcd_5/screen" not in root_cmake
    assert "intentionally NOT added to root EXTRA_COMPONENT_DIRS" in screen_cmake

    # Every screen contract must point at a route the existing backend already owns.
    backend_sources = "\n".join(
        read(p) for p in [
            ROOT / "components" / "web_server" / "live_api.c",
            ROOT / "components" / "web_server" / "web_api.c",
            ROOT / "components" / "web_server" / "device_api.c",
            ROOT / "components" / "web_server" / "operational_api.c",
        ]
    )
    required_paths = [
        "/api/live",
        "/api/status",
        "/api/meters",
        "/api/inverters",
        "/api/telemetry",
        "/api/operator/events",
        "/api/operator/alarms",
    ]
    for path in required_paths:
        assert path in api_h, f"screen contract missing {path}"
        assert path in backend_sources, f"backend does not own {path}"

    # Local HMI must not grow a second product/backend implementation.
    forbidden_headers = {
        "control_engine.h",
        "safety_manager.h",
        "meter_manager.h",
        "inverter_manager.h",
        "config_manager.h",
        "commissioning_gate.h",
        "network_manager.h",
        "modbus_tcp.h",
    }
    for source in SCREEN.rglob("*.c"):
        text = read(source)
        includes = set(re.findall(r'#include\s+[<\"]([^>\"]+)[>\"]', text))
        overlap = forbidden_headers & includes
        assert not overlap, f"{source.relative_to(ROOT)} bypasses backend boundary: {sorted(overlap)}"

    # There are no local backend HTTP handlers or local HTTP write clients.
    all_c = "\n".join(read(p) for p in SCREEN.rglob("*.c"))
    assert "httpd_register_uri_handler" not in all_c
    assert "HTTP_POST" not in all_c
    assert "HTTP_PUT" not in all_c
    assert "HTTP_DELETE" not in all_c

    # No hidden screen scheduler/task: the board integration owns cadence and
    # must call the bounded refresh lanes under the qualified LVGL locking model.
    assert "xTaskCreate" not in runtime
    assert "vTaskDelay" not in runtime
    assert "screen_runtime_refresh_fast" in runtime
    assert "screen_runtime_refresh_status" in runtime
    assert "screen_runtime_refresh_devices" in runtime
    assert "screen_runtime_refresh_operations" in runtime
    for macro in [
        "SCREEN_API_LIVE_PATH",
        "SCREEN_API_STATUS_PATH",
        "SCREEN_API_METERS_PATH",
        "SCREEN_API_INVERTERS_PATH",
        "SCREEN_API_TELEMETRY_PATH",
        "SCREEN_API_EVENTS_PATH",
        "SCREEN_API_ALARMS_PATH",
    ]:
        assert macro in runtime

    # The only LVGL event callbacks in this milestone are navigation callbacks.
    callback_files = sorted(
        source.relative_to(SCREEN).as_posix()
        for source in SCREEN.rglob("*.c")
        if "lv_obj_add_event_cb" in read(source)
    )
    assert callback_files == ["screen_app.c"], callback_files

    # Fail-closed source attribution: overview must never render the raw
    # screen_live_snapshot_t.source field. The negative lookahead deliberately
    # permits snapshot->source_attributed_to from the status snapshot.
    assert re.search(r"snapshot->source(?!_)", overview) is None
    assert "source_attributed_to" in overview
    assert 'strcmp(status->source_attributed_to, "unknown")' in readiness

    # Null numerics are represented by has_* flags and unavailable display text.
    assert "has_grid_kw" in api_c
    assert "has_solar_kw" in api_c
    assert "cJSON_IsNumber" in api_c
    assert '"-- kW"' in all_c
    assert "no zero" in all_c.lower() or "no zero" in read(SCREEN / "README.md").lower()

    # All parity/runtime/profile/physical-port sources are part of the isolated component manifest.
    for source in [
        "pages/overview_screen.c",
        "pages/grid_screen.c",
        "pages/solar_screen.c",
        "pages/alarms_screen.c",
        "pages/readiness_screen.c",
        "components/screen_widgets.c",
        "drivers/waveshare_display_profile.c",
        "drivers/waveshare_display_port.c",
        "screen_app.c",
        "screen_runtime.c",
    ]:
        assert f'"{source}"' in screen_cmake, f"CMake missing {source}"

    # Both exact vendor resolutions exist and there is no code symbol that
    # silently selects one as the physical target.
    assert "WAVESHARE_DISPLAY_800X480" in profile_h
    assert "WAVESHARE_DISPLAY_1024X600" in profile_h
    assert ".width = 800" in profile_c and ".height = 480" in profile_c
    assert ".width = 1024" in profile_c and ".height = 600" in profile_c
    assert "WAVESHARE_DISPLAY_DEFAULT" not in profile_h
    assert "DEFAULT_WAVESHARE_DISPLAY" not in profile_h

    # IDF6 port must use the new master-bus API, preserve shared-bus injection,
    # and keep the vendor CH422G touch-reset sequence visible in board-local code.
    assert '"driver/i2c_master.h"' in display_port_h
    assert '"driver/i2c.h"' not in display_port_c
    assert "i2c_new_master_bus" in display_port_c
    assert "i2c_master_bus_add_device" in display_port_c
    assert "i2c_master_transmit" in display_port_c
    assert "config->i2c_bus" in display_port_c
    assert "CH422G_TOUCH_RESET_LOW" in display_port_c
    assert "CH422G_TOUCH_RESET_HIGH" in display_port_c
    assert "esp_lcd_touch_new_i2c_gt911" in display_port_c

    # The shell exposes only the existing operator product areas in this milestone.
    for label in ["Overview", "Grid", "Solar", "Alarms", "Ready"]:
        assert f'"{label}"' in app

    print("waveshare screen source contract: PASS")


if __name__ == "__main__":
    main()
