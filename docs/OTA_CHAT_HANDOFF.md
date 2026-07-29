# Handoff to the OTA implementation chat

**Date:** 2026-07-29
**Affected branch:** `feature/secure-web-ota` (tip `5c3c321`)
**Fix location:** `dev` (`46df5a2`), commit `e000b3e`

## Summary

`feature/secure-web-ota` was branched from `d71cb03`, which contains a defect that makes
**every OTA endpoint permanently unreachable**. This is not an OTA bug — the cause is in
`components/web_server/engineering_auth.c`, a file the OTA work never touched.

Nothing is wrong with the OTA design. It will simply never authenticate until the fix is rebased in.

## What is broken

`httpd_resp_set_hdr()` retains the pointer it is given and does not copy the value, so the buffer
must remain valid until the response is sent. `set_session_cookie()` built the header into a
**stack-local** array that was destroyed when the function returned — before `send_json()` sent
the response.

```c
static void set_session_cookie(httpd_req_t *request, const char *token_hex)
{
    char header[160];                                  /* dies on return */
    snprintf(header, sizeof(header), AUTH_COOKIE_NAME "=%s; ...", token_hex);
    httpd_resp_set_hdr(request, "Set-Cookie", header); /* stores the pointer only */
}
```

Observed on hardware before the fix:

```
POST /api/engineering/login  ->  200 {"authenticated":true, ...}
Set-Cookie:                       <-- empty
GET  /api/engineering/session ->  {"authenticated":false, ...}
```

Login reports success, no session is ever created, and every protected endpoint stays at
`401 engineering_password_setup_required`.

`clear_session_cookie()` passes a string literal, which has static storage — so logout worked
correctly and login did not. That asymmetry is what hid the defect.

## Why this blocks OTA specifically

The OTA endpoints are correctly protected, which is exactly why they are unreachable.

`components/web_server/CMakeLists.txt` force-includes `engineering_auth.h` into every file in
`WEB_SERVER_C_SOURCES` except `engineering_guard.c` and `operational_api.c`:

```cmake
set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "-include;engineering_auth.h")
```

That header macro-redirects registration:

```c
#define httpd_register_uri_handler engineering_register_uri_handler
```

`ota_api.c` is in that list, so its three handlers are registered through the authorization
gateway. `public_uri()` allows only `/api/status`, `/api/telemetry` and `/api/engineering/*`, so
`/api/ota/status`, `/api/ota/upload` and `/api/ota/reboot` are all `GATEWAY_MODE_PROTECTED`.

Result: **no session can exist, therefore all three OTA endpoints return 401 forever.**
Upload will appear to fail for reasons that have nothing to do with OTA.

## Required action

Rebase `feature/secure-web-ota` onto `dev`:

```bash
git fetch origin
git checkout feature/secure-web-ota
git rebase origin/dev
```

`dev` is 6 commits ahead; the OTA branch carries 26 of its own. The **only** file both sides
touch is `sdkconfig.defaults` — `dev` changed the default grid meter address to
`192.168.100.200`, the OTA branch adds OTA partition settings. Keep both. No other conflict is
expected; `engineering_auth.c` is not modified by the OTA branch.

## Verification after rebasing

Confirm the session cookie is actually issued — do not rely on the login response body, which
reported success even while broken:

```bash
curl -i -X POST http://<controller>/api/engineering/login \
     -H 'Content-Type: application/json' -d '{"password":"<engineering password>"}'
# Set-Cookie must be non-empty:
# Set-Cookie: eng_session=<64 hex chars>; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800

curl -s -H "Cookie: eng_session=<token>" http://<controller>/api/engineering/session
# must report "authenticated":true

curl -s -H "Cookie: eng_session=<token>" http://<controller>/api/ota/status
# must return OTA status, not 401
```

Note: `curl -c/-b` cookie jars do **not** persist this cookie, because a jar only stores cookies
with an `Expires` attribute and this one uses `Max-Age`. Capture the header and pass it with
`-H "Cookie: ..."` instead. This cost real debugging time.

## Applies to the OTA work generally

Never pass a stack-local buffer to `httpd_resp_set_hdr()`. The buffer must be owned by the
request handler so it outlives the response. If OTA adds any custom response header, it must
follow the same rule.

## Standing project rules that apply to OTA

- Never erase NVS or the whole flash. Commissioned Wi-Fi credentials and configuration must
  survive an update. There is a real commissioned controller in the field.
- OTA must not be able to brick a remote unit: verify the image before switching the boot
  partition, and keep a rollback path.
- The repository is **public**. Never commit a credential, signing key or token.
- CI fails on any compiler warning. The full contract suite must stay green.
- A passing build is not physical qualification. OTA must be verified on real hardware,
  including a failed/interrupted upload, before it is called done.
