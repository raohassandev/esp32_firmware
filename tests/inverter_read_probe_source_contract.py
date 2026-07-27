from pathlib import Path

root = Path(__file__).resolve().parents[1]
manager = (root / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
header = (root / "components/inverter_manager/include/inverter_manager.h").read_text(encoding="utf-8")
api = (root / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
ui = (root / "web/inverter-profiles.js").read_text(encoding="utf-8")

assert "inverter_manager_probe_read_only" in header
assert "inverter_manager_probe_read_only" in manager
assert "read_profile_block" in manager
assert "modbus_tcp_read_registers" in manager.split("static esp_err_t read_profile_block", 1)[1].split(
    "static esp_err_t verify_identity", 1
)[0]
probe_body = manager.split("esp_err_t inverter_manager_probe_read_only", 1)[1].split(
    "bool inverter_manager_get_data", 1
)[0]
assert "read_profile_block" in probe_body
assert "modbus_tcp_write_single" not in probe_body
assert "modbus_tcp_write_multiple" not in probe_body
assert "inverter_profile_allows_read" in probe_body
assert "identity_matches" in probe_body

assert '"/api/inverter-probe"' in api
assert '"read_only", true' in api
assert '"writes_issued", false' in api
assert "inverter_manager_probe_read_only" in api

assert "Test connection (read-only)" in ui
assert "'/api/inverter-probe'" in ui
assert "no Modbus writes will be sent" in ui
assert "Writes issued:" in ui

print("inverter read-only probe source contract: PASS")
