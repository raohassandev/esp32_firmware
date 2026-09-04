#!/usr/bin/env python3
"""Fail closed if the live-controller browser resilience fixes regress.

The July live UI audit proved that one Chromium tab could consume the default
HTTP socket pool and make the controller unreachable, while large operator JSON
responses were operating close to internal-heap exhaustion. These are product
UX failures even though the control engine remains fail-closed.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
SDK = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")


def integer(pattern: str, text: str, label: str) -> int:
    match = re.search(pattern, text)
    if not match:
        raise AssertionError(f"missing {label}")
    return int(match.group(1))


http_sockets = integer(r"config\.max_open_sockets\s*=\s*(\d+)\s*;", SERVER, "HTTP socket budget")
lwip_sockets = integer(r"^CONFIG_LWIP_MAX_SOCKETS=(\d+)$", SDK, "lwIP socket pool")

# esp_http_server reserves three additional sockets internally. Keep enough
# browser capacity for a normal six-connection origin while preserving sockets
# for Modbus TCP, DHCP/DNS and recovery traffic.
assert http_sockets >= 8, f"HTTP client capacity regressed to {http_sockets}"
assert http_sockets + 3 <= lwip_sockets, "httpd socket budget exceeds lwIP pool"
assert lwip_sockets - (http_sockets + 3) >= 3, "fewer than three non-httpd sockets remain"
assert "config.lru_purge_enable = true;" in SERVER, "stale keep-alive sockets must be reclaimable"

# The N16R8 target has octal PSRAM. Large browser/API allocations may spill to
# PSRAM while small/ISR-safe allocations stay internal. Do not silently return
# to the pre-audit configuration where the 8 MB device ran with PSRAM disabled.
for token in (
    "CONFIG_SPIRAM=y",
    "CONFIG_SPIRAM_MODE_OCT=y",
    "CONFIG_SPIRAM_SPEED_80M=y",
    "CONFIG_SPIRAM_USE_MALLOC=y",
    "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y",
):
    assert token in SDK, f"browser resilience PSRAM setting missing: {token}"

threshold = integer(r"^CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=(\d+)$", SDK, "PSRAM internal threshold")
assert 1024 <= threshold <= 16384, f"unexpected always-internal threshold: {threshold}"
assert "# CONFIG_SPIRAM is not set" not in SDK

print(
    "browser resilience contract: PASS "
    f"(httpd={http_sockets}, lwip={lwip_sockets}, spare={lwip_sockets-http_sockets-3}, "
    f"always_internal={threshold})"
)
