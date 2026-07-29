# Physical qualification — 2026-07-29

Branch `feature/multibrand-inverter-profiles`, HEAD `d71cb03c1183faf90fc20b1ec6f294f5139a136f`
(verified against the expected gate SHA before any work started).

Hardware: ESP32-S3 DevKitC-1 N16R8 on COM5. ESP-IDF v6.0.1 (same version as the CI container).

## 1. Local verification suite — PASS (76/76)

| Group | Result |
|---|---|
| `node --check` browser modules + simulator (34) | PASS |
| Node unit tests (`wifi-utils`, `devices-utils`, `em500-utils`) | PASS |
| SolTrix Modbus simulator self-test + test suite | PASS |
| Host C: `source_mode_unit_test` | PASS |
| Host C: `solar_grid_integration_test` | PASS |
| Host C (via contracts): `power_control_policy`, `inverter_command_policy`, `network_wifi_copy` | PASS |
| Python source contracts (37) + production release gate | PASS |

Notes on reproducing this locally on Windows:

- CI invokes `python3`; this host exposes the interpreter as `python`.
- CI invokes `gcc`, which is not installed here and is **not** provided by
  `esp-clang` (it ships no host libc — a trivial `#include <stdio.h>` fails).
  A portable MinGW-w64 GCC 16.1.0 (UCRT) toolchain was fetched to the scratchpad
  and SHA-256 verified. Without it the five gcc-dependent items cannot run at all;
  they must not be reported as passing from the Python exit code alone, because
  three of the contract scripts shell out to `gcc` and fail with `WinError 2`.

## 2. Build — PASS

`idf.py set-target esp32s3` and `idf.py build` both exit 0.

- `build/automatrix_pvdg.bin` — **1,461,232 bytes** (`0x164bf0`), 54% of the app partition free
- **Compiler warnings: 0** (CI fails the build on any `warning:` line)
- SHA-256: `b074c1efbbc21d0ae8b38ee7caa4de08f7453a3102110d15e236a91f96b80a37`

Two environment blockers were fixed; neither was a code defect:

1. The ESP-IDF installation had **unchecked-out submodules** — `set-target` aborted with
   `components/mbedtls/mbedtls/include is not a directory`. Repaired with
   `git submodule update --init --recursive` in the IDF tree.
2. A stale non-CMake `build/` directory made `set-target` refuse to clean. It was moved
   aside (not deleted) to `d:\Working\_esp32_build_backup\`.

### Safety finding: stale sdkconfig would have overwritten commissioned Wi-Fi

The untracked local `sdkconfig` was a leftover from an earlier provisioning session:

```
CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING=y
CONFIG_PVDG_WIFI_PROVISION_ID=1
CONFIG_PVDG_PRIMARY_WIFI_SSID="Rao"
CONFIG_PVDG_PRIMARY_WIFI_PASSWORD=...
```

Flashing that build would have replaced the stored NVS credentials. The tracked
`sdkconfig.defaults` carries no provisioning entries, so `set-target` regenerated a clean
config with `PVDG_APPLY_BUILD_WIFI_PROVISIONING=n`. The rebuild was therefore the
safety-correcting action. The old file is backed up in the scratchpad.

## 3. Flash — PASS, NVS preserved

`idf.py -p COM5 -b 460800 flash` — exit 0.

- Wrote 1,461,232 bytes at `0x00020000` (app partition `ota_0`), **hash of data verified**
- Bootloader + partition table + app only. **No erase command of any kind was run.**
- `nvs` partition remains at offset `0x9000`, length `0x6000`, untouched

## 4. Boot evidence — all acceptance criteria met

Pre-flash the device ran app version `ed765b0`; post-flash it reports `d71cb03`.

| Criterion | Result |
|---|---|
| No panic / watchdog / stack overflow | 0 occurrences |
| No reboot loop | exactly 1 boot banner in a 45 s capture |
| Configuration loads | `app_core: Initializing configuration` → no error |
| Solar-Grid configuration loads | initialised, no error |
| Control fail-closed | `control: Automatic Solar-Grid control remains fail-closed: explicit grid availability and breaker evidence are not configured` |
| Web server starts | serving; HTTP 200 on `/api/status`, `/api/config`, `/api/meters` |
| Meter polling starts | `meter_read_jobs: bounded background read-job queue started` |
| No unexpected inverter command | `requested_pv_kw: 0`, `applied_pv_kw: 0` |
| NVS / Wi-Fi not erased | stored SSID survived the flash (see below) |
| Bootstrap stack | headroom 10,204 of 12,288 bytes; free heap 173,896 |

Identity: STA MAC `3c:0f:02:d9:8e:1c`, recovery-AP BSSID `3c:0f:02:d9:8e:1d`.

## 5. Wi-Fi state — Automatrix-4G, achieved without any NVS erase

No credential change was made by this work. The sequence observed:

1. Pre-flash the device was commissioned to `Tenda_69B540` and held `192.168.0.100`,
   with the grid meter at `192.168.0.200` **online** (`meter_age_ms: 4`, quality 50%).
2. The Tenda access point then went off the air. The controller correctly fell back to its
   recovery AP — `Automatrix-PVDG-Setup` was observed broadcasting at 93% signal from the
   device's own BSSID. This is a field validation of the recovery-AP path.
3. The stored primary profile is now `Automatrix-4G`, and it **survived the flash**, which is
   direct evidence that NVS was preserved.
4. Post-flash: `Ready: SSID=Automatrix-4G IP=192.168.100.14 GW=192.168.100.1 RSSI=-52`.

Build-time provisioning was **not** used, and no NVS partition was erased.

## 6. Critical defect found and fixed: engineering session cookie was never issued

While attempting the engineering-gated backup, `/api/engineering/login` returned
`{"authenticated":true}` but the response carried an **empty `Set-Cookie` header**, so no
session could ever be established and every engineering endpoint stayed at
`401 engineering_password_setup_required`. Engineering authentication was completely
unusable on the device.

Cause, in `components/web_server/engineering_auth.c`:

```c
static void set_session_cookie(httpd_req_t *request, const char *token_hex)
{
    char header[160];                                  /* stack local */
    snprintf(header, sizeof(header), ...);
    httpd_resp_set_hdr(request, "Set-Cookie", header); /* stores the POINTER */
}
```

`httpd_resp_set_hdr()` retains the pointer it is given and does not copy the value, so the
buffer must outlive the response. `header` died when the function returned, well before
`send_json()` sent the response. `clear_session_cookie()` passes a string literal (static
storage), which is why logout worked and login did not — masking the bug.

Fix: the header buffer is now owned by the request handler (`login_post`, `password_post`),
so it stays valid until httpd has sent the response. Verified on hardware after reflashing:

```
Set-Cookie: eng_session=<TOKEN>; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800
{"authenticated":true, ... "security_state":"Engineering session authenticated"}
```

Also fixed a false positive in `tests/production_release_gate.py`: `macro_value()` returns
`None` when `AUTH_TEMPORARY_FIELD_BYPASS` is absent, and `None != 0` reported a fully removed
bypass as "enabled". Absence is the safe state, so the check now accepts `{0, None}`, matching
how `kconfig_default_for()` already treats `None`.

## 7. Commissioning changes applied on 2026-07-29

- **Grid meter re-addressed to `192.168.100.200:502`** (unit 1, FC03) via `/api/meters/config`
  → `{"saved":true,"persisted":true,"restart_required":true}`, then a controlled restart.
  Result: `meter_online: true`, `meter_has_data: true`, `meter_age_ms: 58`,
  `grid_power_kw: 271.44`, **alarms cleared**. Blocker B1 is resolved.
  The build-time default (`sdkconfig.defaults`, `main/Kconfig.projbuild`) was updated to match.
- **Permanent Engineering password set**, so `setup_required` is now false and the one-time
  serial setup code is cleared from the device.
- **Development-only password prefill** added to the Engineering sign-in field as a single
  named constant `DEV_DEFAULT_ENGINEERING_PASSWORD` in `web/product-mode.js`.
  `tests/production_release_gate.py` now **blocks any production release while it is non-empty**,
  so the credential cannot reach a release image. Set it to `''` to clear the block.

Automatic control remained disabled and fail-closed throughout; no inverter write was issued.

### Open question for commissioning

`grid_power_kw` reads ~271 kW (it read ~293 kW on the previous network). The boot log shows
`Grid Meter legacy EM500 scale 0.01000000 normalized to 0.00001000 kW/raw`. This magnitude has
**not** been cross-checked against the meter's own display and must be verified against the
physical instrument before it is trusted as a control input.

## 8. Remaining blockers

**B1 — RESOLVED.** The grid meter was re-addressed to `192.168.100.200:502` and is now online
with fresh data and no alarms (section 7).

**B2 — RESOLVED.** The engineering session-cookie defect was fixed (section 6) and a permanent
Engineering password is now set, so the engineering endpoints unlock normally.

**B3 — Pre-flash backup is partial, and cannot now be completed retrospectively.**
`/api/config` and the operator endpoints were captured, but the device serves `/api/config` in
operator view with Wi-Fi credentials redacted, and the engineering-gated endpoints were
unreachable at the time because of the B2 defect. Configuration was preserved across every
flash (NVS untouched), so nothing was lost — but the pre-change snapshot is incomplete and
should not be described as a full backup.

**B4 — Grid power magnitude is unverified.** See the open question in section 7. No
meter-derived value may be trusted as a control input until it is checked against the physical
instrument.

**B5 — The development password prefill must be cleared before any release.** Set
`DEV_DEFAULT_ENGINEERING_PASSWORD` to `''` in `web/product-mode.js`. Until then
`tests/production_release_gate.py` blocks a production release by design.

## 9. Status

A passing build and a clean boot are **not** physical qualification. Automatic control remains
disabled and fail-closed. No inverter write was issued through any manufacturer profile. FAT/SAT,
meter-connected endurance testing and inverter qualification all remain outstanding.

Evidence: `evidence/postflash-backup-2026-07-29/` (API captures and redacted serial logs).
The one-time engineering setup code is redacted from all committed logs.
