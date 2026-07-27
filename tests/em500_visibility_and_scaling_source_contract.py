from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "web/em500-core.js").read_text(encoding="utf-8")
PROFILES = (ROOT / "web/em500-profiles.js").read_text(encoding="utf-8")
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

print("EM500 visibility and scaling source contract passed")
