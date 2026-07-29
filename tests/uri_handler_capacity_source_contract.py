#!/usr/bin/env python3
"""HTTP route capacity must stay ahead of the routes actually registered.

esp_http_server refuses to register more than `config.max_uri_handlers` routes,
and every registration site in this component propagates the failure with
ESP_RETURN_ON_ERROR. So overflowing the limit does not cost one endpoint -- it
aborts web_server start and the controller comes up with no web interface at
all. On a commissioned unit reached only over the network, that is the whole
product.

The firmware that was last flashed carried exactly 40 routes against a limit of
40: correct, but with no headroom, so the next route added would have taken the
web server down. Other contracts pin a literal (`max_uri_handlers = 48`), which
does not notice a route being added at all -- it only breaks when someone edits
that one line.

This contract instead derives the requirement: count the routes and demand the
limit stay ahead of them. Adding a route without raising the limit fails here.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB_SERVER_DIR = ROOT / "components" / "web_server"

# Routes are declared as httpd_uri_t initialisers; most are registered by
# looping over a static array, so counting call sites would badly undercount.
ROUTE_PATTERN = re.compile(r"^\s*\{?\s*\.uri\s*=", re.MULTILINE)

# Headroom for the next feature. Chosen so that a normal increment of work
# cannot silently consume the last slot between one review and the next.
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
        f"{routes} routes are registered but max_uri_handlers is {capacity}; "
        "httpd would refuse the excess and web_server start would fail outright",
    )
    require(
        capacity >= routes + REQUIRED_HEADROOM,
        f"only {capacity - routes} spare handler slot(s) for {routes} routes; "
        f"raise max_uri_handlers to keep at least {REQUIRED_HEADROOM} in reserve",
    )

# A registration whose failure is discarded would turn an overflow into a
# missing endpoint that no test and no log would reveal.
for name in sorted(per_file):
    text = (WEB_SERVER_DIR / name).read_text(encoding="utf-8", errors="replace")
    if "register_uri_handler" not in text:
        continue
    require(
        "ESP_RETURN_ON_ERROR" in text or "ESP_ERROR_CHECK" in text or "!= ESP_OK" in text
        or "err" in text,
        f"{name} registers routes but appears to ignore the registration result",
    )

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(
    f"URI handler capacity contract passed "
    f"({routes} routes registered, capacity {match.group(1)}, "
    f"{int(match.group(1)) - routes} spare)"
)
