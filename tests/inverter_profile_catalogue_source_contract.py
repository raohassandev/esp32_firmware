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
require("huawei.sun2000.pending" in SOURCE, "Huawei picker entry missing")
require("goodwe.commercial.pending" in SOURCE, "GoodWe picker entry missing")
require("solis.commercial.pending" in SOURCE, "Solis picker entry missing")
require("foxess.commercial.pending" in SOURCE, "FoxESS/Knox picker entry missing")

for profile_id in [
    "huawei.sun2000.pending",
    "goodwe.commercial.pending",
    "solis.commercial.pending",
    "foxess.commercial.pending",
]:
    start = SOURCE.index(f'.id = "{profile_id}"')
    block = SOURCE[start:SOURCE.find("    },", start)]
    require(".has_power_limit = false" in block,
            f"{profile_id} must stay write-locked until manual extraction and qualification")

print("Inverter profile catalogue safety contract passed")
