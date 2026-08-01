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
# The qualification requirement was removed by the owner at the plant. The
# structural rules above are what remain, and each prevents a physical outcome
# rather than expressing doubt about a transcription.
require("command_register_is_flash_backed" in SOURCE,
        "write eligibility must still refuse a flash-backed command register "
        "with no manufacturer-stated write rate: writing it continuously "
        "destroys the inverter's memory while every write reports success")
require("!profile->simulator_only" in SOURCE,
        "simulator-only profiles must never pass the production write gate")
require("profile->simulator_only" in SOURCE and
        "INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED" in SOURCE,
        "simulator-only profiles must have an explicit read qualification path")

for profile_id in [
]:
    require(profile_id in SOURCE, f"{profile_id} simulator profile missing")

require("40125" in SOURCE and "10.0f" in SOURCE and "0.1f" in SOURCE,
        "Huawei simulator percent_x10 command/readback contract is missing")

for profile_id in [
    "huawei.sun2000.pending",
    "huawei.smartlogger.plant",
    "goodwe.commercial.pending",
    "solis.commercial.pending",
    "foxess.commercial.pending",
]:
    require(profile_id in SOURCE, f"{profile_id} picker entry missing")
    start = SOURCE.index(f'.id = "{profile_id}"')
    block = SOURCE[start:SOURCE.find("    },", start)]

    # A manufacturer profile must never be production-approved without physical
    # readback evidence. This, not the absence of a register address, is what
    # keeps unqualified equipment safe: inverter_profile_allows_write() requires
    # PRODUCTION_APPROVED, and tests/inverter_write_permission_test.c EXECUTES the
    # gate over this whole catalogue and asserts that no shipped profile can
    # command production.
    require("PRODUCTION_APPROVED" not in block,
            f"{profile_id} must not claim production approval")

    # Register addresses transcribed from a manual are permitted -- validating a
    # documented map against a declared simulator is how it gets qualified before
    # anyone travels to site. Two conditions make that safe:
    if ".has_power_limit = true" in block:
        # A command register is useless and dangerous without a readback: an
        # unconfirmable setpoint on real equipment must never be possible.
        require(".has_power_limit_readback = true" in block,
                f"{profile_id} declares a command register but no readback register; "
                "a command that cannot be confirmed must not be issuable")
        # And the map must cite the document it came from, so a reviewer can check
        # it rather than trust it.
        require(".manual_reference" in block and "pending" not in
                block.split(".manual_reference", 1)[1].split(",", 1)[0],
                f"{profile_id} carries register addresses but no concrete manual "
                "reference; transcribed values must be attributable")

print("Inverter profile catalogue safety contract passed")
