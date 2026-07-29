#!/usr/bin/env python3
"""Executable host HTTP contract for the Engineering session cookie flow.

This is deliberately a host-side contract, not an ESP32 device test. It first
checks the production C sources for the session-token size, caller-owned cookie
header, Max-Age cookie format, and Engineering URI registration gateway. It then
runs a local HTTP fixture and exercises the exact curl header flow required for
Max-Age-only cookies: capture Set-Cookie and pass it explicitly as Cookie.
"""

from __future__ import annotations

import http.server
import re
import secrets
import socketserver
import subprocess
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTH = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
AUTH_H = (ROOT / "components/web_server/include/engineering_auth.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


session_bytes_match = re.search(r"#define\s+AUTH_SESSION_BYTES\s+(\d+)u", AUTH)
require(session_bytes_match is not None, "AUTH_SESSION_BYTES is missing")
SESSION_BYTES = int(session_bytes_match.group(1))
require(SESSION_BYTES == 32, "Engineering session token must be 32 bytes")
require("AUTH_SESSION_HEX_BYTES (AUTH_SESSION_BYTES * 2u)" in AUTH,
        "session token must be rendered as two hex characters per byte")
require("char cookie_header[AUTH_COOKIE_HEADER_BYTES]" in AUTH,
        "login handler must own the Set-Cookie buffer until the response is sent")
require("set_session_cookie(request, cookie_header, sizeof(cookie_header), cookie)" in AUTH,
        "login handler must pass the caller-owned cookie buffer")
require('AUTH_COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800"' in AUTH,
        "production cookie attributes changed unexpectedly")
require("#define httpd_register_uri_handler engineering_register_uri_handler" in AUTH_H,
        "normal URI registration must pass through the Engineering auth gateway")
require('source STREQUAL "engineering_guard.c"' in CMAKE and 'source STREQUAL "operational_api.c"' in CMAKE,
        "Engineering gateway compile policy is missing")
require("return ota_api_register(s_server);" in SERVER,
        "OTA API registration is missing from the guarded web server")

COOKIE_NAME = "eng_session"
EXPECTED_COOKIE_ATTRIBUTES = "Path=/; HttpOnly; SameSite=Strict; Max-Age=1800"
VALID_TOKEN = ""


class ContractHandler(http.server.BaseHTTPRequestHandler):
    server_version = "EngineeringAuthContract/1.0"

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def _send_json(self, status: int, body: str, cookie: str | None = None) -> None:
        encoded = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        if cookie is not None:
            self.send_header("Set-Cookie", cookie)
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        global VALID_TOKEN
        if self.path != "/api/engineering/session":
            self._send_json(404, '{"error":"not_found"}')
            return
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length:
            self.rfile.read(content_length)
        VALID_TOKEN = secrets.token_hex(SESSION_BYTES)
        cookie = f"{COOKIE_NAME}={VALID_TOKEN}; {EXPECTED_COOKIE_ATTRIBUTES}"
        self._send_json(200, '{"authenticated":true}', cookie)

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/api/ota/status":
            self._send_json(404, '{"error":"not_found"}')
            return
        supplied = self.headers.get("Cookie", "")
        if supplied != f"{COOKIE_NAME}={VALID_TOKEN}" or not VALID_TOKEN:
            self._send_json(401, '{"authenticated":false}')
            return
        self._send_json(200, '{"state":"idle","authenticated":true}')


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def run_curl(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["curl", "--silent", "--show-error", *args],
        check=True,
        text=True,
        capture_output=True,
    )


def main() -> None:
    with ThreadedTCPServer(("127.0.0.1", 0), ContractHandler) as server:
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{port}"

        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            login_headers = temp / "login.headers"
            login_body = temp / "login.body"
            status_headers = temp / "status.headers"
            status_body = temp / "status.body"

            run_curl(
                "--dump-header", str(login_headers),
                "--output", str(login_body),
                "--request", "POST",
                "--header", "Content-Type: application/json",
                "--data", '{"password":"host-contract-only"}',
                f"{base}/api/engineering/session",
            )

            login_header_text = login_headers.read_text(encoding="utf-8")
            cookie_match = re.search(
                rf"(?im)^Set-Cookie:\s*({COOKIE_NAME}=([0-9a-f]{{64}}));\s*"
                rf"{re.escape(EXPECTED_COOKIE_ATTRIBUTES)}\s*$",
                login_header_text,
            )
            require(cookie_match is not None,
                    "Set-Cookie did not contain a 64-character lowercase hex session token")
            cookie_pair = cookie_match.group(1)
            token = cookie_match.group(2)
            require(len(token) == 64 and re.fullmatch(r"[0-9a-f]{64}", token) is not None,
                    "captured session token is not exactly 64 hex characters")

            run_curl(
                "--dump-header", str(status_headers),
                "--output", str(status_body),
                "--header", f"Cookie: {cookie_pair}",
                f"{base}/api/ota/status",
            )

            status_header_text = status_headers.read_text(encoding="utf-8")
            require(re.search(r"(?m)^HTTP/\S+ 200\b", status_header_text) is not None,
                    "authenticated OTA status request did not return HTTP 200")

            print("=== LOGIN RESPONSE HEADERS ===")
            print(login_header_text.strip())
            print("=== LOGIN RESPONSE BODY ===")
            print(login_body.read_text(encoding="utf-8").strip())
            print(f"SESSION_HEX_LENGTH={len(token)}")
            print(f'EXPLICIT_COOKIE_HEADER=Cookie: {cookie_pair}')
            print("=== OTA STATUS RESPONSE HEADERS ===")
            print(status_header_text.strip())
            print("=== OTA STATUS RESPONSE BODY ===")
            print(status_body.read_text(encoding="utf-8").strip())
            print("Engineering auth host HTTP contract passed")

        server.shutdown()
        thread.join(timeout=2)


if __name__ == "__main__":
    main()
