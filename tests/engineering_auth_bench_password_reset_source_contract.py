"""Regression coverage for the bench-only Engineering password reset endpoint.

Unlike CONFIG_PVDG_BENCH_ENGINEERING_AUTH_BYPASS (which removes the
authentication check entirely), this flag keeps the check in place and only
adds a way back in when a bench unit's password is lost -- the network-facing
surface is a recovery mechanism, not a hole. The new setup code must never be
returned over HTTP; it must only reach the serial console, the same as a
fresh-from-factory boot.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH_C = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("config PVDG_BENCH_ENGINEERING_PASSWORD_RESET" in KCONFIG,
        "the reset endpoint must be a named Kconfig option")
config_tail = "\n".join(
    KCONFIG.split("PVDG_BENCH_ENGINEERING_PASSWORD_RESET")[1].splitlines()[:3])
require("default n" in config_tail, "the reset endpoint must default off")

require(
    '"/api/engineering/bench-reset"' in AUTH_C,
    "the bench reset route must exist",
)
route_idx = AUTH_C.index('"/api/engineering/bench-reset"')
guard_idx = AUTH_C.rfind(
    "#if defined(CONFIG_PVDG_BENCH_ENGINEERING_PASSWORD_RESET)", 0, route_idx)
require(guard_idx != -1 and guard_idx < route_idx,
        "the route registration must be compiled out unless the flag is set")

require(
    "static void bench_reset_password(void)" in AUTH_C,
    "the reset logic must exist as its own function, not inlined into the handler",
)
reset_start = AUTH_C.index("static void bench_reset_password(void)")
reset_body = AUTH_C[reset_start:reset_start + 1800]
require("nvs_erase_key(handle, AUTH_RECORD_KEY)" in reset_body,
        "reset must actually erase the persisted credential record")
require("s_password_configured = false" in reset_body,
        "reset must clear the in-memory authenticated state, not just NVS")
require("s_failed_attempts = 0" in reset_body and "s_lockout_until_ms = 0" in reset_body,
        "reset must also clear any active lockout, or a locked-out bench unit "
        "could not use its own reset")

# --- the new setup code must reach the serial log, and go nowhere else ---
require('ESP_LOGW(TAG, "One-time Engineering setup code: %s", setup_code)' in reset_body,
        "the new setup code must be logged to the serial console, matching the "
        "existing fresh-boot setup-code convention")

handler_start = AUTH_C.index("static esp_err_t bench_reset_post")
handler_body = AUTH_C[handler_start:handler_start + 600]
require("setup_code" not in handler_body,
        "the HTTP response must never contain the setup code -- it is serial-console only")
require('cJSON_AddStringToObject(root, "message"' in handler_body,
        "the response should say where to find the code, not repeat it")

print("Engineering password bench-reset source contract passed")
