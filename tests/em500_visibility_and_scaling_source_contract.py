from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "web/em500-core.js").read_text(encoding="utf-8")
QUALITY = (ROOT / "web/em500-quality.js").read_text(encoding="utf-8")
PROFILES = (ROOT / "web/em500-profiles.js").read_text(encoding="utf-8")
CACHE_API = (ROOT / "components/web_server/em500_cache_api.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("Complete meter parameters" in CORE,
        "meter workspace must clearly expose the complete parameter set")
require("intro.after(root)" in CORE,
        "complete meter workspace must appear before runtime diagnostics")
for section in ("Voltage", "Current", "Active power", "Reactive power",
                "Apparent power", "Power factor", "Power quality"):
    require(f"measurementSection('{section}'" in CORE,
            f"live meter workspace is missing {section}")
for tab in ("Live measurements", "Energy", "History", "Settings M01–M18"):
    require(tab in CORE, f"meter workspace is missing {tab} tab")

for token in (
    "'/api/meters/em500/cache'",
    "Acquisition quality",
    "Stale last-good analyser values",
    "STALE LAST-GOOD — excluded from control",
    "They are not treated as current evidence and are excluded from automatic control.",
    "Response time",
    "Success rate",
    "scan_in_progress",
    "MAX_WARM_RETRIES = 8",
    "WARM_RETRY_MS = 1500",
    "QUALITY_REFRESH_MS = 2500",
    "state.scanController" if False else "local.controller?.abort()",
    "document.addEventListener('visibilitychange'",
    "window.addEventListener('beforeunload'",
):
    require(token in QUALITY, f"analyser quality workspace is missing: {token}")
require("window.setInterval(" not in QUALITY,
        "analyser quality refresh must be cancellable rather than an unbounded interval")
require("core.state.activeTab === 'history'" in QUALITY,
        "historical job warming must not trigger snapshot refresh retries")
require("local.warmAttempts >= MAX_WARM_RETRIES" in QUALITY,
        "cache-warming retries must be bounded")

for token in (
    '"stale"', '"age_ms"', '"response_ms"', '"success_percent"',
    '"scan_in_progress"', '"modbus_io_in_http_handler"',
):
    require(token in CACHE_API, f"cache quality API field is missing: {token}")
require('"modbus_io_in_http_handler", false' in CACHE_API,
        "cache quality endpoint must remain non-blocking")

require("requiresKwCorrection" in PROFILES,
        "EM500 wrong-scale detection is missing")
require("Math.abs(scale - 0.01)" in PROFILES,
        "wrong-scale detection must target the observed 0.01 scale")
require("profile.scale = Number(profile.scale) / 1000" in PROFILES,
        "correction must divide the saved scale by exactly 1000")
require("Correct power scaling (÷1000)" in PROFILES,
        "operator correction action is missing")
require("Automatic control will be forced disabled" in PROFILES,
        "scale persistence must retain the automatic-control safety warning")
require("restart" in PROFILES.lower(),
        "scale changes must disclose restart requirement")
require("m->active_power_scale = 0.00001f" in CONFIG,
        "safe default EM500 kW scale must remain 0.00001")

print("EM500 visibility, cache quality, stale-data and scaling source contract passed")
