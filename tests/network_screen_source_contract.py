"""Regression coverage for the on-device Wi-Fi manager (Network settings page).

Added per an explicit product requirement: the panel must be able to join a
Wi-Fi network from its own touchscreen -- no phone app, no laptop, no visiting
the recovery access point from another device -- and must always show a
signal-strength indicator, not only on the Network page.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCREEN_DIR = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
NETWORK_SCREEN_H = (SCREEN_DIR / "pages/network_screen.h").read_text(encoding="utf-8")
NETWORK_SCREEN_C = (SCREEN_DIR / "pages/network_screen.c").read_text(encoding="utf-8")
SCREEN_APP_H = (SCREEN_DIR / "screen_app.h").read_text(encoding="utf-8")
SCREEN_APP_C = (SCREEN_DIR / "screen_app.c").read_text(encoding="utf-8")
WIDGETS_H = (SCREEN_DIR / "components/screen_widgets.h").read_text(encoding="utf-8")
WIDGETS_C = (SCREEN_DIR / "components/screen_widgets.c").read_text(encoding="utf-8")
CMAKE = (SCREEN_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
BACKEND_C = (SCREEN_DIR / "product_800x480/main/local_network_backend.c").read_text(encoding="utf-8")
MAIN_C = (SCREEN_DIR / "product_800x480/main/main.c").read_text(encoding="utf-8")
MAIN_CMAKE = (SCREEN_DIR / "product_800x480/main/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# --- the page exists and is wired into the app's page enum/build ---
require("SCREEN_PAGE_NETWORK" in SCREEN_APP_H, "Network page must be a real navigable page")
require('"pages/network_screen.c"' in CMAKE, "network_screen.c must be compiled into the screen component")
require('"local_network_backend.c"' in MAIN_CMAKE, "the local network backend must be compiled")

# --- Engineering gate: Wi-Fi credentials are a commissioning action, not a
#     casual control, and must go through the same auth pattern as source
#     commissioning (unlock/lock backend hooks, not an always-open form) ---
for token in ("network_screen_auth_result_t (*unlock)", "void (*lock)(void *context)"):
    require(token in NETWORK_SCREEN_H, f"backend must expose {token}")
require("engineering_auth_verify_local_credential" in BACKEND_C,
        "Wi-Fi provisioning must reuse the same Engineering credential gate as "
        "the other commissioning backends, not invent a separate weaker one")

# --- scanning and connecting go through the backend, never touch esp_wifi
#     directly from the screen layer (same decoupling as source_commissioning) ---
require("esp_wifi" not in NETWORK_SCREEN_C, "the screen layer must stay decoupled from esp_wifi; go through the backend")
for token in ("request_scan", "read_scan", "connect", "restart_controller"):
    require(f"(*{token})" in NETWORK_SCREEN_H, f"backend must expose {token}")

# --- connecting persists through config_manager and requires a restart to
#     apply -- it must never bypass the existing Wi-Fi config validation path
#     or silently reconnect without the operator's confirmation ---
require("config_manager_save(config)" in BACKEND_C,
        "Wi-Fi credentials must be persisted through config_manager, the same "
        "store every other commissioning screen uses")
require("restart_required = true" not in BACKEND_C or "result_set(result, true, true," in BACKEND_C,
        "a successful connect save must report restart_required so the operator "
        "knows the new network only takes effect after a restart")
require("password_length < 8U" in BACKEND_C,
        "a non-empty Wi-Fi password shorter than 8 characters must be rejected, "
        "matching the web API's own Wi-Fi config validation")

# --- the signal indicator is persistent (nav bar), not confined to the
#     Network page -- that was the explicit point of the request ---
require("nav_signal" in SCREEN_APP_C, "a persistent nav-bar signal indicator must exist")
require("screen_ui_wifi_bars" in SCREEN_APP_C, "the nav-bar indicator must use the shared bars helper")
require("const char *screen_ui_wifi_bars(bool online, int rssi)" in WIDGETS_C,
        "the shared Wi-Fi bars helper must exist in one place, not be duplicated per page")
require("screen_ui_wifi_bars" in WIDGETS_H, "the shared helper must be declared for other pages to reuse")

# --- ASCII only: a missing Unicode glyph in the compiled-in LVGL font must
#     never silently render as a blank indicator ---
non_ascii = [ch for ch in WIDGETS_C if ord(ch) > 127]
require(not non_ascii, "screen_widgets.c must stay ASCII-only in UI strings so a missing font "
                       "glyph cannot silently blank the signal indicator")

# --- the Network page has its own poll loop; it must not depend on the
#     app-level telemetry/status refresh cadence to see scan results ---
require("lv_timer_create(poll_timer_cb" in NETWORK_SCREEN_C,
        "the Network page must poll scan/status on its own LVGL timer")
require("SCREEN_PAGE_NETWORK" in MAIN_C, "main.c's refresh dispatch must account for the Network page")

print("Network screen (on-device Wi-Fi manager) source contract passed")
