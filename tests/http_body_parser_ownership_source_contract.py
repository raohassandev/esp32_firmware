#!/usr/bin/env python3
"""Lock bounded ownership of HTTP request-body receive and JSON parsing.

Ordinary JSON APIs must use the shared bounded helper. Engineering auth keeps a
small specialized credential reader because it wipes secret plaintext. Secure
OTA is the only reviewed binary-stream exception: it must retain strict inactive
slot size admission, one cumulative upload deadline and a fixed-size buffer.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "components/web_server"
HTTP_JSON = (WEB / "http_json.c").read_text(encoding="utf-8")
HTTP_JSON_H = (WEB / "include/http_json.h").read_text(encoding="utf-8")
AUTH = (WEB / "engineering_auth.c").read_text(encoding="utf-8")
OTA = (WEB / "ota_api.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


recv_owners = []
parse_owners = []
for path in sorted(WEB.glob("*.c")):
    source = path.read_text(encoding="utf-8")
    if "httpd_req_recv(" in source:
        recv_owners.append(path.name)
    if "cJSON_Parse(" in source or "cJSON_ParseWithLength(" in source:
        parse_owners.append(path.name)

require(recv_owners == ["engineering_auth.c", "http_json.c", "ota_api.c"],
        f"unreviewed HTTP receive-loop owner(s): {recv_owners}")
require(parse_owners == ["engineering_auth.c", "http_json.c"],
        f"unreviewed direct JSON parser owner(s): {parse_owners}")

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
require("(size_t)request->content_len > maximum_body_bytes" in HTTP_JSON,
        "shared request reader no longer rejects oversized bodies")
require("if (!http_json_depth_valid(body, maximum_depth))" in HTTP_JSON,
        "shared JSON parser must depth-check before cJSON parsing")
require(HTTP_JSON.index("http_json_depth_valid(body, maximum_depth)") <
        HTTP_JSON.index("cJSON_ParseWithLength(body"),
        "shared JSON helper must depth-check before parsing")

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

# OTA is not JSON and therefore cannot use http_json. Its private receive loop is
# allowed only while all of these bounded streaming invariants remain visible.
for token in (
    "OTA_UPLOAD_CHUNK_BYTES 4096U",
    "OTA_UPLOAD_DEADLINE_MS 600000U",
    "const uint64_t deadline = now_ms() + OTA_UPLOAD_DEADLINE_MS",
    "image_size > update->size",
    "receive_exact(request, prefix, sizeof(prefix), deadline)",
    "malloc(OTA_UPLOAD_CHUNK_BYTES)",
    "receive_exact(request, buffer, chunk, deadline)",
    "HTTPD_SOCK_ERR_TIMEOUT",
    "if (now_ms() >= deadline_ms) return ESP_ERR_TIMEOUT",
    "application/octet-stream",
):
    require(token in OTA, f"reviewed OTA streaming bound lost: {token}")
require("malloc(image_size)" not in OTA and "malloc(request->content_len)" not in OTA,
        "OTA binary stream regressed to whole-image allocation")
require("cJSON_Parse(" not in OTA and "cJSON_ParseWithLength(" not in OTA,
        "OTA API must not become a private JSON parser owner")

for path in sorted(WEB.glob("*_api.c")):
    source = path.read_text(encoding="utf-8")
    if path.name != "ota_api.c":
        require("httpd_req_recv(" not in source,
                f"{path.name} introduced a private receive loop")
    require("cJSON_Parse(" not in source and "cJSON_ParseWithLength(" not in source,
            f"{path.name} introduced direct JSON parsing")
    if path.name == "ota_api.c":
        continue
    if "HTTP_POST" in source or "HTTP_PUT" in source or "HTTP_PATCH" in source:
        if "content_len" in source or "http_json_" in source:
            require("http_json.h" in source and "http_json_" in source,
                    f"{path.name} write body is not routed through shared bounded JSON")

print("HTTP request body/parser ownership contract passed")
