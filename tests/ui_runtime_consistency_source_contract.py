from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
METER = (ROOT / "components/meter_manager/meter_manager.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
THEME = (ROOT / "web/theme.css").read_text(encoding="utf-8")
ENHANCEMENTS = (ROOT / "web/ui-enhancements.js").read_text(encoding="utf-8")
EM500 = (ROOT / "web/em500-core.js").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("legacy_em500_scale_fingerprint" in METER, "legacy EM500 scaling fingerprint missing")
require("0.00001f" in METER and "0.01f" in METER,
        "exact divide-by-1000 compatibility normalization missing")
require("active_power_address == 57U" in METER and "active_power_address == 58U" in METER,
        "normalization must remain restricted to the observed EM500 total-power addresses")
require("sample_is_fresh" in METER and "METER_FRESH_GRACE_MS" in METER,
        "transient poll failures must preserve fresh-data health")
require("next.online = sample_is_fresh" in METER,
        "poll failures still force an immediate contradictory offline state")

component_pos = max(SERVER.index("web_assets_devices_css"), SERVER.index("web_assets_em500_css"))
theme_pos = SERVER.index("web_assets_theme_css")
require(theme_pos > component_pos, "theme overrides must load after component CSS")
require("web_assets_ui_enhancements_js" in SERVER and "ui-enhancements.js" in CMAKE,
        "commissioning enhancement module is not embedded and served")

for token in [
    'html[data-theme="light"] .device-runtime-card',
    'html[data-theme="light"] .em500-summary-card',
    '@media (max-width: 1200px)',
    '@media (max-width: 860px)',
    '@media (max-width: 620px)',
    '.power-flow { grid-template-columns: 1fr; }',
]:
    require(token in THEME, f"responsive/light-theme rule missing: {token}")

require("MutationObserver" in ENHANCEMENTS and "em500-collapsible-body" in ENHANCEMENTS,
        "long meter parameter groups are not collapsible")
require("/api/meters/em500/snapshot" not in ENHANCEMENTS,
        "UI enhancements must not start a second expensive EM500 snapshot poller")
require("pollTimer" in EM500 and "refreshActive(true)" in EM500,
        "EM500 core must remain the single owner of live analyser refresh")

print("UI/runtime consistency source contract passed")
