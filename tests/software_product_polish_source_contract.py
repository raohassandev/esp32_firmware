#!/usr/bin/env python3
"""Source contract for the software-only product-completion slice.

This gate deliberately proves browser/reporting architecture and OTA UX coupling only.
It does not claim physical OTA interruption/rollback qualification.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORTS = (ROOT / "web/reports.js").read_text(encoding="utf-8")
REPORTS_CSS = (ROOT / "web/reports.css").read_text(encoding="utf-8")
OTA = (ROOT / "web/ota.js").read_text(encoding="utf-8")
OTA_CSS = (ROOT / "web/ota.css").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
SHELL = (ROOT / "web/product-shell-v2.js").read_text(encoding="utf-8")
SHELL_CSS = (ROOT / "web/product-shell-v2.css").read_text(encoding="utf-8")
EXPERIENCE = (ROOT / "web/product-experience-v2.js").read_text(encoding="utf-8")
EXPERIENCE_CSS = (ROOT / "web/product-experience-v2.css").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Reports are a read-only operator surface using only existing public operational APIs.
for token in (
    "#/reports",
    "/api/operator/history?range=",
    "/api/operator/events",
    "/api/operator/alarms",
    "controller_resident_window",
    "billing_grade: false",
    "long_term_historian: false",
    "sample_timestamps_are_estimates: true",
    "engineering_configuration_included: false",
    "credentials_included: false",
    "physical_qualification_claimed: false",
    "reportsHtml",
    "window.print()",
    "document.hidden",
    "AbortController",
    "Promise.allSettled",
    "data_quality",
):
    require(token in REPORTS, f"reports contract missing {token}")

for forbidden in (
    "method: 'POST'",
    'method: "POST"',
    "method: 'PUT'",
    "method: 'DELETE'",
    "/api/config",
    "/api/wifi/config",
    "/api/control",
):
    require(forbidden not in REPORTS, f"operator reports gained write/config authority: {forbidden}")

require("setInterval(" not in REPORTS,
        "reports added unconditional interval polling")
require("window.setTimeout(refresh, POLL_MS)" in REPORTS,
        "reports do not use route/visibility-aware scheduled refresh")
require("fetch('/api/operator/history" not in REPORTS,
        "history request must include an explicit bounded range")
require("state.quality.history.available" in REPORTS and "state.quality.events.available" in REPORTS,
        "reports do not preserve partial-data availability")
require("requestAnimationFrame(placeNav)" in REPORTS,
        "dynamic Reports route is not placed after the Industrial UI reorder pass")

# Reports assets must be embedded in the existing composite bundle; no URI-handler growth.
for token in (
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/reports.js"',
    'configure_file("${CMAKE_CURRENT_LIST_DIR}/../../web/reports.css"',
    '"${CMAKE_CURRENT_BINARY_DIR}/reports.js"',
    '"${CMAKE_CURRENT_BINARY_DIR}/reports.css"',
):
    require(token in CMAKE, f"reports asset not embedded: {token}")
for getter in ("web_assets_reports_js", "web_assets_reports_css"):
    require(getter in ASSETS_H and getter in ASSETS_C and getter in SERVER,
            f"reports composite getter missing: {getter}")
require("reports: 'Controller-resident trends, alarms and service evidence'" in SHELL,
        "shell has no Reports route context")
require("Operational reports" in SHELL,
        "Reports is not reachable from the compact controller menu")
require("@media print" in REPORTS_CSS and "@page" in REPORTS_CSS,
        "reports lack print-ready paged layout")
require(".reports-meta" in REPORTS_CSS,
        "reports lack visible generation/range/freshness metadata")

# Legacy shell repair assets were absorbed by their owner and must stay retired.
for retired in ("shell-current-fixes.js", "shell-current-fixes.css", "shell_current_fixes"):
    require(retired not in CMAKE and retired not in ASSETS_H and retired not in ASSETS_C and retired not in SERVER,
            f"retired shell repair layer is still embedded: {retired}")
require(not (ROOT / "web/shell-current-fixes.js").exists(), "retired shell-current-fixes.js still exists")
require(not (ROOT / "web/shell-current-fixes.css").exists(), "retired shell-current-fixes.css still exists")
require("themeToggleButton" in SHELL and "Open controller actions" in SHELL,
        "product shell did not absorb the retired repair behavior")
require("name === 'grid' ? 'meters' : name" in EXPERIENCE or "rawRoute() === 'grid'" in SHELL,
        "legacy #/grid route is not canonicalized")
require("rgba(8,24,39,.55)" not in EXPERIENCE_CSS and "rgba(16,36,58,.96)" not in EXPERIENCE_CSS,
        "product experience still contains known dark-only presentation patches")
require("max-width: 900px) and (max-height: 600px" in SHELL_CSS,
        "800x480 HMI duplicate-health reduction is missing")

# Guided OTA must preserve the mature backend safety boundary while closing UX gaps.
for token in (
    "Preflight gates",
    "Validate and stage",
    "Cancel upload",
    "Reboot into staged image",
    "Export OTA status",
    "preflightReady",
    "state.uploadXhr.abort()",
    "event.returnValue = ''",
    "monitorReboot",
    "pending first-boot validation",
    "Previous firmware restored",
    "Firmware verified",
    "physical_qualification_claimed: false",
    "The controller will not reboot automatically",
    "NVS will be preserved",
):
    require(token in OTA, f"guided OTA UX contract missing {token}")
require("setInterval(" not in OTA,
        "OTA UX added unconditional interval polling")
require("window.setTimeout(monitorReboot, REBOOT_POLL_MS)" in OTA,
        "OTA post-reboot verification is not bounded scheduled polling")
for endpoint in ("/api/ota/status", "/api/ota/upload", "/api/ota/reboot"):
    require(endpoint in OTA, f"OTA UX lost endpoint {endpoint}")
for token in (".ota-steps", ".ota-preflight", ".ota-verification", "@media (max-width: 480px)"):
    require(token in OTA_CSS, f"OTA professional responsive styling missing {token}")

print("Software product polish source contract passed")