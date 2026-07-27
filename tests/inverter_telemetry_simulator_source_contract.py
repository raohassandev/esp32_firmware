from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
SIM = (ROOT / "tools/soltrix_modbus_simulator.js").read_text(encoding="utf-8")
SCENARIOS = (ROOT / "tools/soltrix_modbus_simulator_test.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for profile_id, unit_id, identity, power_address, limit_address in [
    ("soltrix.sim.huawei.v1", "21", "0xA021", "40010", "40125"),
    ("soltrix.sim.goodwe.v1", "22", "0xA022", "41010", "41125"),
    ("soltrix.sim.solis.v1", "23", "0xA023", "42010", "42125"),
]:
    require(profile_id in PROFILES, f"missing simulator profile {profile_id}")
    require(unit_id in SIM, f"missing simulator unit {unit_id}")
    require(identity in PROFILES and identity in SIM, f"identity contract missing for {profile_id}")
    require(power_address in PROFILES and power_address in SIM, f"power map missing for {profile_id}")
    require(limit_address in PROFILES and limit_address in SIM, f"limit/readback map missing for {profile_id}")

require(".simulator_only = true" in PROFILES, "simulator profiles must be explicitly simulator-only")
require("!profile->simulator_only" in PROFILES, "simulator profiles must be blocked from production writes")
require("inverter_telemetry_task" in MANAGER and "xTaskCreate" in MANAGER,
        "periodic inverter telemetry task is missing")
require("verify_identity" in MANAGER and "identity_matches" in MANAGER,
        "profile identity verification is missing")
require("inverter_profile_decode_value" in MANAGER,
        "profile-driven active-power/readback decoding is missing")
require("update_stale_state" in MANAGER and "telemetry_stale" in MANAGER,
        "stale/offline handling is missing")
require("recompute_commandable_capacity" in MANAGER,
        "dynamic offline-capacity removal is missing")
require("inverter_profile_readback_matches" in MANAGER and "mismatch_count" in MANAGER,
        "command readback mismatch tracking is missing")
require('"/api/inverter-telemetry"' in API,
        "read-only inverter telemetry API is missing")
require('"writes_issued", false' in API and '"read_only_endpoint", true' in API,
        "telemetry endpoint must explicitly report zero writes")
for scenario in ["normal", "rollback", "timeout", "comm-lost"]:
    require(f"'{scenario}'" in SCENARIOS, f"simulator scenario {scenario} is not tested")

print("inverter telemetry + SolTrix simulator source contract passed")
