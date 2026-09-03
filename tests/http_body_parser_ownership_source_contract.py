#!/usr/bin/env python3
"""Lock bounded ownership of HTTP request-body receive and JSON parsing.

Ordinary JSON APIs must use the shared bounded helper. Engineering auth keeps a
small specialized credential reader because it wipes secret plaintext, but it
must retain its own body-size, cumulative-time and JSON-depth limits.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "components/web_server"
HTTP_JSON = (WEB / "http_json.c").read_text(encoding="utf-8")
HTTP_JSON_H = (WEB / "include/http_json.h").read_text(encoding="utf-8")
AUTH = (WEB / "engineering_auth.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# There are exactly two permitted owners of raw request-body reception in the
# web component: the shared helper and Engineering auth's secret-wiping reader.
recv_owners = []
parse_owners = []
for path in sorted(WEB.glob("*.c")):
    source = path.read_text(encoding="utf-8")
    if "httpd_req_recv(" in source:
        recv_owners.append(path.name)
    if "cJSON_Parse(" in source or "cJSON_ParseWithLength(" in source:
        parse_owners.append(path.name)

require(recv_owners == ["engineering_auth.c", "http_json.c"],
        f"unreviewed HTTP receive-loop owner(s): {recv_owners}")
require(parse_owners == ["engineering_auth.c", "http_json.c"],
        f"unreviewed direct JSON parser owner(s): {parse_owners}")

# Shared JSON reader: body length is capped, receive uses one cumulative
# deadline, socket timeouts do not restart that deadline, depth is bounded
# before parsing, and parsing is length-aware.
for token in (
    "http_body_read_bounded",
    "http_json_depth_valid",
    "http_json_parse_bounded",
    "const uint64_t deadline",
    "now_ms() >= deadline",
    "HTTPD_SOCK_ERR_TIMEOUT",
    "cJSON_ParseWithLength",
):
    require(token in HTTP_JSON or token in HTTP_JSON_H,
            f"shared bounded JSON helper lost {token}")
require("content_length > body_limit" in HTTP_JSON,
        "shared request reader no longer rejects oversized bodies")
require("if (!http_json_depth_valid(body, max_depth))" in HTTP_JSON,
        "shared JSON parser must depth-check before cJSON parsing")

# Engineering credentials intentionally do not flow through a generic helper
# because plaintext is explicitly wiped. Its specialized path must therefore
# independently keep all equivalent bounds.
for token in (
    "AUTH_BODY_LIMIT 1024u",
    "AUTH_BODY_DEADLINE_MS 3000ULL",
    "AUTH_JSON_MAX_DEPTH 6u",
    "request->content_len > AUTH_BODY_LIMIT",
    "uint64_t deadline = now_ms() + AUTH_BODY_DEADLINE_MS",
    "if (now_ms() >= deadline)",
    "HTTPD_SOCK_ERR_TIMEOUT",
    "json_depth_valid(body)",
    "memset(body, 0, strlen(body))",
):
    require(token in AUTH, f"Engineering auth body safety lost {token}")
require(AUTH.index("json_depth_valid(body)") < AUTH.index("cJSON_Parse(body)"),
        "Engineering auth must depth-check credentials before parsing JSON")

# No ordinary API may grow a private body-loop/parser. Those APIs must use the
# shared helper; this catches future files automatically instead of maintaining
# a hand-curated path list.
for path in sorted(WEB.glob("*_api.c")):
    source = path.read_text(encoding="utf-8")
    require("httpd_req_recv(" not in source,
            f"{path.name} introduced a private receive loop")
    require("cJSON_Parse(" not in source and "cJSON_ParseWithLength(" not in source,
            f"{path.name} introduced direct JSON parsing")
    if "HTTP_POST" in source or "HTTP_PUT" in source or "HTTP_PATCH" in source:
        # A write API may have endpoints with no body, so only require the shared
        # helper when it actually references request content length/body parsing.
        if "content_len" in source or "http_json_" in source:
            require("http_json.h" in source and "http_json_" in source,
                    f"{path.name} write body is not routed through shared bounded JSON")

print("HTTP request body/parser ownership contract passed")
