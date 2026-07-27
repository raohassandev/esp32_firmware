from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("inverter_profiles.c" in CMAKE, "profile catalogue must be compiled")
require("INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" in HEADER,
        "catalogue must expose an explicit production approval gate")
require("inverter_profile_allows_write" in HEADER and "inverter_profile_allows_write" in SOURCE,
        "catalogue must expose a centralized write-eligibility decision")
require("profile->has_power_limit" in SOURCE,
        "write eligibility must require a documented command register")
require("profile->has_power_limit_readback" in SOURCE,
        "write eligibility must require command readback")
require("profile->qualification == INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" in SOURCE,
        "only production-approved profiles may write")
require("!profile->simulator_only" in SOURCE,
        "simulator-only profiles must never pass the production write gate")
require("profile->simulator_only" in SOURCE and
        "INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED" in SOURCE,
        "simulator-only profiles must have an explicit read qualification path")

for profile_id in [
    "soltrix.sim.huawei.v1",
    "soltrix.sim.goodwe.v1",
    "soltrix.sim.solis.v1",
]:
    require(profile_id in SOURCE, f"{profile_id} simulator profile missing")

require("40125" in SOURCE and "10.0f" in SOURCE and "0.1f" in SOURCE,
        "Huawei simulator percent_x10 command/readback contract is missing")

for profile_id in [
    "huawei.sun2000.pending",
    "goodwe.commercial.pending",
    "solis.commercial.pending",
    "foxess.commercial.pending",
]:
    require(profile_id in SOURCE, f"{profile_id} picker entry missing")
    start = SOURCE.index(f'.id = "{profile_id}"')
    block = SOURCE[start:SOURCE.find("    },", start)]
    require(".has_power_limit = true" not in block,
            f"{profile_id} must stay write-locked until manual extraction and qualification")
    require("PRODUCTION_APPROVED" not in block,
            f"{profile_id} must not claim production approval")

print("Inverter profile catalogue safety contract passed")
