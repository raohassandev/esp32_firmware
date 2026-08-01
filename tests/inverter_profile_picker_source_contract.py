from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

root = Path(__file__).resolve().parents[1]
js = (root / "web/inverter-profiles.js").read_text(encoding="utf-8")
cmake = (root / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
assets_h = (root / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
assets_c = (root / "components/web_server/web_assets.c").read_text(encoding="utf-8")
server = (root / "components/web_server/web_server.c").read_text(encoding="utf-8")

required_js = [
    "/api/inverter-profiles",
    "/api/inverter-profile-assignment",
    "inverterProfileChannel",
    "inverterManufacturer",
    "inverterModelFamily",
    "write_allowed",
    "Live writes remain locked",
    "inverterProfileApply",
    "restart_required",
]
for token in required_js:
    assert token in js, f"missing picker behavior token: {token}"

assert "method: 'POST'" in js
assert "automatic control is disabled" in js.lower()
bundle.require_delivered("inverter-profiles.js")
# Delivery is asserted above, once; see the bundle order files.

print("inverter profile picker source contract: PASS")
