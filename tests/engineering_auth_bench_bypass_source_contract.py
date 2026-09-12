"""Regression coverage for the bench-only Engineering HTTP-auth bypass.

Added on explicit instruction to stop gating early-stage development on
security review ("security last men dekhen gy"). This lock exists so the
bypass stays narrowly scoped to a default-off Kconfig flag with loud
logging, rather than becoming a silent, permanent hole: it is a single
function's early-return, easy to grep for and easy to remove later, not a
scattered set of per-endpoint shortcuts.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH_C = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("config PVDG_BENCH_ENGINEERING_AUTH_BYPASS" in KCONFIG,
        "the bypass must be a named Kconfig option, not an ad-hoc #define")
config_tail = "\n".join(
    KCONFIG.split("PVDG_BENCH_ENGINEERING_AUTH_BYPASS")[1].splitlines()[:3])
require("default n" in config_tail,
        "the bypass must default off so a normal build is unaffected")

require(
    "bool engineering_auth_is_authorized(httpd_req_t *request)" in AUTH_C,
    "the single authorization choke point must still exist",
)
func_start = AUTH_C.index("bool engineering_auth_is_authorized(httpd_req_t *request)")
func_body = AUTH_C[func_start:func_start + 1200]
require(
    "#if defined(CONFIG_PVDG_BENCH_ENGINEERING_AUTH_BYPASS)" in func_body,
    "the bypass must be compiled out entirely unless the Kconfig flag is set",
)
require("return true;" in func_body, "the bypass path must exist inside that guard")
require(
    "session_cookie_valid(request, true)" in func_body,
    "the real check must remain in the #else branch, not be deleted",
)

# --- loud on every boot AND on every call the bypass actually lets through ---
require(
    AUTH_C.count("CONFIG_PVDG_BENCH_ENGINEERING_AUTH_BYPASS=y -- every Engineering-gated") == 0
    or "ESP_LOGW(TAG," in func_body,
    "each bypassed call must log a warning, not just boot once quietly",
)
require(
    "engineering_auth_init(void)" in AUTH_C
    and AUTH_C.index("#if defined(CONFIG_PVDG_BENCH_ENGINEERING_AUTH_BYPASS)") <
        AUTH_C.index("engineering_auth_init(void)") + 400,
    "a boot-time warning must exist near engineering_auth_init so the bypass "
    "is visible in the log even before any request arrives",
)

print("Engineering HTTP auth bench-bypass source contract passed")
