from pathlib import Path

root = Path(__file__).resolve().parents[1]
js = (root / "web/inverter-profiles.js").read_text(encoding="utf-8")
cmake = (root / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
assets_h = (root / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
assets_c = (root / "components/web_server/web_assets.c").read_text(encoding="utf-8")
server = (root / "components/web_server/web_server.c").read_text(encoding="utf-8")

required_js = [
    "/api/inverter-profiles",
    "inverterManufacturer",
    "inverterModelFamily",
    "write_allowed",
    "Live writes remain locked",
    "inverterProfileApply",
]
for token in required_js:
    assert token in js, f"missing picker behavior token: {token}"

assert 'disabled title="Profile persistence is not enabled' in js
assert 'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/inverter-profiles.js"' in cmake
assert '"${CMAKE_CURRENT_BINARY_DIR}/inverter-profiles.js"' in cmake
assert "web_assets_inverter_profiles_js" in assets_h
assert "_binary_inverter_profiles_js_start" in assets_c
assert "web_assets_inverter_profiles_js," in server

print("inverter profile picker source contract: PASS")
