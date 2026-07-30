import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def code(text: str) -> str:
    """Executable source only; ownership comments name identifiers on purpose."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"(?m)^\s*//.*$", "", text)
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

# The enhancement layer must not decide anything by reading user-visible text.
# It chose which sections to leave expanded by matching the rendered heading
# against a hardcoded list of English section names, so a wording change
# silently collapsed the values an engineer opens this view to read. The module
# that renders the panel states the default on the element instead.
# Every occurrence of textContent must be an assignment. A read -- anything not
# immediately followed by `=` -- means the module is deciding from rendered
# copy again. This is a property, so it survives renaming the sections.
reads = re.findall(r"textContent(?!\s*=[^=])", code(ENHANCEMENTS))
require(not reads, "UI enhancements must not read rendered text to decide behaviour")
require("querySelector('h3')" not in ENHANCEMENTS and 'querySelector("h3")' not in ENHANCEMENTS,
        "UI enhancements must not locate a section by its heading element")
require("collapsedByDefault" in ENHANCEMENTS,
        "collapse defaults must be read from the renderer's declaration")
require("dataset.collapsedByDefault" in EM500,
        "em500-core must declare the collapse default on each panel it renders")
require("collapsedByDefault = true" in EM500,
        "panels must default to collapsed unless the renderer says otherwise")
# Re-decorating unchanged content must produce no mutations, and the observer
# must not be able to see the writes it caused.
require("collapsibleReady" in ENHANCEMENTS,
        "decoration must be idempotent")
require("takeRecords()" in ENHANCEMENTS,
        "the collapsible observer must discard records its own writes caused")
require("pollTimer" in EM500 and "refreshActive(true)" in EM500,
        "EM500 core must remain the single owner of live analyser refresh")

print("UI/runtime consistency source contract passed")
