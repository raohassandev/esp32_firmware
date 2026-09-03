#!/usr/bin/env python3
"""HTTP route capacity must stay ahead of the routes actually registered.

esp_http_server refuses to register more than `config.max_uri_handlers` routes,
and every registration site in this component propagates the failure with
ESP_RETURN_ON_ERROR. This contract derives the requirement from the registered
route declarations and requires reserve headroom for the next feature.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB_SERVER_DIR = ROOT / "components" / "web_server"
ROUTE_PATTERN = re.compile(r"^\s*\{?\s*\.uri\s*=", re.MULTILINE)
REQUIRED_HEADROOM = 4
failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


routes = 0
per_file = {}
for source in sorted(WEB_SERVER_DIR.glob("*.c")):
    count = len(ROUTE_PATTERN.findall(source.read_text(encoding="utf-8", errors="replace")))
    if count:
        per_file[source.name] = count
        routes += count

server = (WEB_SERVER_DIR / "web_server.c").read_text(encoding="utf-8", errors="replace")
match = re.search(r"config\.max_uri_handlers\s*=\s*(\d+)", server)
require(match is not None, "web_server.c does not set config.max_uri_handlers")

if match is not None:
    capacity = int(match.group(1))
    require(
        capacity >= routes,
        f"{routes} routes are registered but max_uri_handlers is {capacity}; web_server start would fail",
    )
    require(
        capacity >= routes + REQUIRED_HEADROOM,
        f"only {capacity - routes} spare handler slot(s) for {routes} routes; keep at least {REQUIRED_HEADROOM} in reserve",
    )

for name in sorted(per_file):
    text = (WEB_SERVER_DIR / name).read_text(encoding="utf-8", errors="replace")
    if "register_uri_handler" not in text:
        continue
    require(
        "ESP_RETURN_ON_ERROR" in text or "ESP_ERROR_CHECK" in text or "!= ESP_OK" in text or "err" in text,
        f"{name} registers routes but appears to ignore the registration result",
    )

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(
    f"URI handler capacity contract passed ({routes} routes registered, "
    f"capacity {match.group(1)}, {int(match.group(1)) - routes} spare)"
)
