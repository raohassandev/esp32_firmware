# Implementation Report — Startup Stabilization and Production Wi-Fi Manager

**Project:** Automatrix PV-DG Controller
**Branch:** `agent/minimal-pvdg-foundation`
**Board:** ESP32-S3 DevKitC-1 N16R8 — chip rev v0.2, 16 MB flash, USB Serial/JTAG on COM5
**Toolchain:** ESP-IDF v6.0.1, target `esp32s3`
**Status:** Complete and validated on hardware. One external item outstanding (ZLAN gateway powered down).

---

## 1. Executive summary

The controller was stuck in a continuous reboot loop and could not be commissioned. The cause was
a **main-task stack overflow produced by placing whole `app_config_t` objects on the stack**,
compounded by startup aborting on recoverable errors so a single failure repeated forever.

The configuration system was moved entirely to checked heap allocations, startup was layered by
criticality so a non-essential subsystem can fail without taking the device down, and the Wi-Fi
manager was completed to production behaviour (primary-first selection, bounded backoff, recovery
AP, credential masking).

The board now boots reliably, associates with the configured network, obtains DHCP, serves its
web UI and API, survives restarts with its configuration intact, and holds automatic inverter
control disabled throughout. Two further defects found during validation were fixed: a blocking
Modbus `connect()` that ignored its timeout, and the absence of any way to commission a device
that already held a stored configuration.

---

## 2. Original failure

Two symptoms were reported:

1. `app_core: Initializing configuration`, then configuration init returning `ESP_ERR_INVALID_ARG`.
2. After an attempted correction, `ERROR: A stack overflow in task main has been detected`,
   followed by an endless reboot loop.

The serial monitor additionally reported missing or mismatched ELF files, because build
directories were being changed while a monitor session was attached.

---

## 3. Root cause

### 3.1 The overflow

`app_config_t` is large: with `APP_MAX_METERS = 4`, `APP_MAX_INVERTERS = 12`, and a 64-byte host
string inside every `modbus_endpoint_t`, it is roughly **2.5–2.8 KB**. The configuration load path
held **two** of them live simultaneously — the loaded copy plus the read-back verification copy —
i.e. about **5–5.6 KB of stack**.

`app_main()` runs on the ESP-IDF main task, sized by `CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584` bytes.
A single load therefore overran the stack. That is precisely the reported
`stack overflow in task main`.

### 3.2 Why it looped instead of stopping

- `config_manager_init()` returned its persistence error to the caller and `app_core_init()`
  aborted the entire startup on it, so one transient NVS write problem prevented the device from
  ever coming up.
- The earlier `ESP_ERR_INVALID_ARG` came from `config_manager_save()` rejecting a configuration
  that failed `valid()`. `defaults()` derived `wifi.fallback.enabled` from
  `CONFIG_PVDG_DEFAULT_WIFI_SSID`, which was empty (`""`). A fallback marked enabled with an empty
  SSID is rejected by `profile_valid()`, so the very first save of freshly generated defaults
  could fail and take startup down with it.

### 3.3 Explicitly ruled out

Checked and found **not** to be contributors: recursion, stack corruption, oversized local Wi-Fi
scan arrays (`scan_configured_networks()` already used `calloc`/`free`), NVS blob-size mismatch
handling, buffer overruns from `strlcpy` sizing, stale build artifacts, and sdkconfig target or
partition-table mismatch. There is **no `ESP_ERROR_CHECK` anywhere in the application
components**, so the loop was not an `ESP_ERROR_CHECK` abort.

### 3.4 Measured proof the fix is real

Startup now runs on a dedicated 12,288-byte bootstrap task that reports its own worst-case usage:

```
I (550) app_main: Bootstrap stack headroom: 10492 bytes of 12288; free heap 249632 bytes (min 249632)
```

Peak usage across the whole NVS + Wi-Fi + HTTP-server init path is only **~1,796 bytes**. This
confirms the fix is the removal of the large stack objects, **not** a blind stack increase — the
same path would now fit even in the original 3,584-byte main task.

---

## 4. Files changed

| File | Change |
| --- | --- |
| `main/app_main.c` | Startup on a dedicated bootstrap task; logs stack high-water mark and heap; halts safely rather than looping on unrecoverable failure. |
| `main/Kconfig.projbuild` | Primary SSID/password made build-time configurable; provisioning id; fallback SSID `Rao1`; ZLAN host `192.168.0.200`. |
| `sdkconfig.defaults` | Pins the corrected defaults for a fresh configure. |
| `components/app_core/app_core.c` | Config and network are required; every other subsystem is optional and degrades with a logged error so the device stays reachable. |
| `components/config_manager/config_manager.c` | Heap-based load; active config published before persistence; persistence failure logged, not fatal; NVS rewritten only when content differs; schema-2→3 migration; one-shot build provisioning; explicit logging of why defaults/migration were used. |
| `components/config_manager/include/config_types.h` | Schema 3 with a trailing `wifi_provision_id`. |
| `components/network_manager/network_manager.c` | Primary-first selection even when the scan cannot see the primary; per-profile retry accounting with clear transitions; idempotent recovery AP kept up across STA attempts; bounded exponential backoff; operator rescan forces a primary attempt; hostname; profile summary logging. |
| `components/modbus_tcp/modbus_tcp.c` | Non-blocking `connect()` bounded by `select()`; failure classification (DNS / no-route / TCP timeout / protocol); removed an unused log tag. |
| `components/meter_manager/meter_manager.c` | Heap-based snapshot; rate-limited failure logging; cross-subnet diagnosis printing local IP, netmask and gateway; recovery message. |
| `components/meter_manager/CMakeLists.txt` | `PRIV_REQUIRES lwip`. |
| `components/inverter_manager/inverter_manager.c` | Heap-based snapshot; per-inverter error reporting instead of early abort. |
| `components/control_engine/control_engine.c` | Heap-based snapshot; **no inverter write issued at all while control is disabled**. |
| `components/safety_manager/safety_manager.c` | Heap-based snapshot. |
| `components/web_server/web_api.c` | Status reports meter staleness, age, error count, control-enabled flag, and **alarm names** instead of a bare hex mask. |
| `components/web_server/CMakeLists.txt` | `esp_timer` dependency. |
| `web/index.html` | Alarm names; distinguishes online / stale / unavailable instead of showing a stale reading as a valid `0.00 kW`. |
| `.gitignore` | Ignores generated `managed_components/`, transient build logs, venv and editor workspace files. |

---

## 5. Architecture changes

- **One authoritative configuration source.** `config_manager` owns `s_cfg`; every other component
  takes a heap snapshot, copies the small slice it needs, and frees it immediately. No component
  retains a second full copy.
- **No large objects on any stack.** Every `app_config_t` is `malloc`/`calloc`-backed, the result
  checked, freed on all paths including errors.
- **Startup layered by criticality.** Configuration and network are required; profile, meter,
  inverter, safety, control and web are optional and degrade with a logged error.
- **Wi-Fi is primary-first with bounded backoff.** The primary profile is always attempted before
  the fallback. Once the recovery AP is serving, sweeps back off 15 s → 30 → 60 → 120 → 240 s
  (capped) so a visible-but-unusable SSID cannot monopolise the radio or flood the log.
- **One-shot provisioning.** A build carrying a new `CONFIG_PVDG_WIFI_PROVISION_ID` applies its
  compiled-in credentials exactly once and records the id; thereafter the stored configuration
  wins, so a reflash never overwrites operator changes made in the web UI.
- **Modbus gated on network readiness** via an event-group `READY_BIT`; meter tasks block on it and
  never poll before an IP exists.
- `WIFI_STORAGE_RAM` is used, so ESP-IDF's internally cached credentials cannot override the
  application configuration.

---

## 6. Defects found during validation

### 6.1 Blocking Modbus connect ignored its timeout

The meter reported only 2 errors in 42 seconds and logged none of them. `ensure_connected()` used
a **blocking `connect()`**, which ignores `endpoint.timeout_ms` and stalls for the entire TCP SYN
retry period when the gateway is unplugged — delaying the offline status and starving the
throttled reporting. `connect()` is now non-blocking, bounded by `select()`.

After the fix the meter fails within ~510 ms of network-ready, honouring the 500 ms timeout, and
throttling behaves exactly as designed (first failure, then every thirtieth):

```
I (13451) wifi_manager: Ready: SSID=Rao IP=192.168.0.110 GW=192.168.0.1 MASK=255.255.255.0 RSSI=-56
W (13961) meters: Grid Meter: TCP timeout, no response (ESP_ERR_TIMEOUT) reading 192.168.0.200:502 [failure 1]
W (36001) meters: ... [failure 30]
W (58801) meters: ... [failure 60]
W (81601) meters: ... [failure 90]
```

### 6.2 No way to commission a device holding a stored configuration

Erasing NVS was not permitted in this environment, and the device had no network over which to
receive a configuration — a genuine chicken-and-egg. Resolved with the one-shot provisioning id
described in §5. The credential itself lives only in the gitignored `sdkconfig`; `git grep`
confirms it appears in **no** tracked file and **no** committed log.

### 6.3 Correction to an earlier interim finding

I initially reported that the recovery AP was not broadcasting, based on this PC's Wi-Fi scan.
**That conclusion was wrong.** The driver log shows the AP genuinely running —
`sta + softAP (…:8e:1d)`, beacon buffers allocated, DHCP server bound to 192.168.4.1, radio mode
3 (`WIFI_MODE_APSTA`). The PC's `netsh wlan show networks` was the unreliable instrument: it
reported a single SSID while the controller concurrently saw 9–15. The bounded backoff added
while investigating remains a genuine improvement and is retained.

---

## 7. Validation results

All performed on the physical board over COM5 and over the LAN.

| Check | Result |
| --- | --- |
| Clean build, ESP-IDF v6.0.1 | **Pass** — no warnings from project code; app 0xd21c0 bytes, 73% of partition free |
| Flash to COM5 | **Pass** — `Hash of data verified` on all four images |
| Main-task stack overflow | **Pass** — 0 occurrences |
| Bootstrap-task stack overflow | **Pass** — 10,492 of 12,288 bytes free |
| Configuration init failure | **Pass** — none |
| No reboot loop ≥ 60 s | **Pass** — 240 s capture, 1 intentional reset, 0 panics |
| Primary attempted before fallback | **Pass** |
| Connected SSID logged | **Pass** — `Rao` |
| IP / gateway / netmask logged | **Pass** — 192.168.0.110 / 192.168.0.1 / 255.255.255.0, RSSI −56 |
| Wi-Fi password never printed | **Pass** — 0 occurrences in any capture |
| Config survives restart | **Pass** — API-triggered restart, stored config reused, no re-provisioning |
| Meter waits for network-ready | **Pass** — 0 meter polls while offline |
| Meter failures rate-limited | **Pass** — failures 1, 30, 60, 90 |
| Web server responds | **Pass** — `GET /` 200, 11,427 bytes |
| API reports correct SSID/IP | **Pass** |
| API masks passwords | **Pass** — verified over the wire |
| Automatic inverter control disabled | **Pass** |
| Runtime logs in repository | **Pass** |
| Implementation report | **Pass** (this document) |

### 7.1 First boot and association

```
I (390) app_core: Initializing configuration
I (410) app_core: Initializing network
I (410) wifi_manager: Profiles: primary 'Rao', fallback 'Rao1', recovery AP 'Automatrix-PVDG-Setup'
I (550) app_main: Bootstrap stack headroom: 10492 bytes of 12288; free heap 249616 bytes
I (560) app_main: PV-DG controller started
I (5050) wifi_manager: Scan found 3 APs; primary 'Rao' visible, fallback 'Rao1' not visible
I (5050) wifi_manager: Connecting to primary SSID 'Rao' using DHCP
I (9650) wifi_manager: Ready: SSID=Rao IP=192.168.0.110 GW=192.168.0.1 MASK=255.255.255.0 RSSI=-56
```

### 7.2 Restart and persistence

`POST /api/system/restart` returned `{"restarting":true}` and produced a clean
`rst:0xc (RTC_SW_CPU_RST)`. The next boot reused the stored configuration with **no** defaults
warning and **no** re-provisioning, reconnecting to `Rao` on the same IP in 9.6 s.

### 7.3 Migration preserved operator settings

Applying the new credentials also exercised the migration path. The stored schema-2 blob migrated
to schema 3 with the operator's settings intact — `/api/config` still reports
`active_power_address: 58`, `scale: 0.01`, `poll_ms: 250`, `grid_import_target_kw: 50`, none of
which are defaults. Only the Wi-Fi credentials changed.

### 7.4 Web API, over the LAN

- `GET /` → `200`, 11,427 bytes, `text/html; charset=utf-8`
- `GET /api/status` → correct SSID, IP, gateway, netmask, RSSI; `using_fallback_sta:false`,
  `fallback_ap_active:false`, `control_enabled:false`,
  `alarm_names:["Meter offline","Meter data stale"]`
- `GET /api/config` → `schema:3`, `primary.password:"********"`,
  `fallback_ap_password:"********"`; the empty fallback password correctly stays `""`
- `POST /api/system/restart` → `{"restarting":true}`

`meter_stale:true` with `meter_has_data:false` means the UI shows **Unavailable** rather than a
misleading `0.00 kW`, as required.

---

## 8. Safety status

- **Automatic inverter control remains disabled.** `defaults()` sets `control.enabled = false`,
  migration forces it false, and `control_task()` now issues **no**
  `inverter_manager_set_total_power_kw()` call at all while disabled — previously it wrote a
  clamped value every cycle even in the disabled state.
- Stale or offline meter data raises `SAFETY_ALARM_METER_STALE` / `SAFETY_ALARM_METER_OFFLINE` and
  forces the limit to 0 kW.
- **The full flash was never erased**, and the NVS partition was not erased either. The existing
  stored configuration was preserved and migrated, which is what made the persistence check
  meaningful.
- Repeated meter failures are rate-limited, so a disconnected gateway cannot flood the console.
- No Wi-Fi password is written to the log, the API, or any tracked file.

---

## 9. Remaining external blocker

**The ZLAN Modbus TCP gateway at `192.168.0.200:502` is powered down.** This is confirmed
external, not a firmware fault: it is unreachable from the engineering PC as well as from the
controller (`Test-NetConnection` → `False`), matching the operator's note.

Everything on the firmware side of that link is implemented and exercised — the failure is
classified as `TCP timeout, no response`, rate-limited, and surfaced as offline/stale in both the
API and the UI, with no inverter commands issued. The meter should read without any firmware
change once the gateway is powered up. If it is later placed on a different subnet, the
cross-subnet diagnostic will print the destination alongside the local IP, netmask and gateway
being relied upon.

---

## 10. Repository hygiene

169 previously unhandled files were resolved:

- **166 files under `managed_components/`** — fetched by the IDF component manager from
  `idf_component.yml` + `dependencies.lock`. Now gitignored, per Espressif practice.
- **`dependencies.lock`** — committed, so component versions and hashes are pinned and builds are
  reproducible.
- **`.clangd`** — committed; a 2-line project-wide config that strips ESP-specific flags so IDE
  clangd works for everyone.
- **`esp32_firmware PVDG.code-workspace`** — gitignored via `*.code-workspace`, consistent with the
  existing `.vscode/` policy for editor-local files. The file itself is left in place untouched.
- `build-log.txt` / `flash-log.txt` were untracked and gitignored: they are regenerated on every
  build and flash and would otherwise leave the working tree permanently dirty. The durable
  evidence lives in `verified-runtime.log` and `verified-runtime-restart.log`, which remain
  committed.

Untracked file count is now **0**. No pre-existing user work was modified or deleted.

---

## 11. Commits

| Hash | Subject |
| --- | --- |
| `0a347f1` | fix: stabilize startup and implement production WiFi manager |
| `00cf149` | docs: record validated commit hash in implementation report |
| `d26e954` | feat: add one-shot WiFi provisioning and bound Modbus connect timeout |

Branch `agent/minimal-pvdg-foundation`. The firmware currently running on the board was built from
this source.

---

## 12. Recommended follow-ups

1. **Power up the ZLAN gateway** and confirm the meter reads; this is the only outstanding item.
2. **Commission the real deployment credentials** by incrementing `CONFIG_PVDG_WIFI_PROVISION_ID`
   and setting the SSID/password in the local `sdkconfig`, or simply by using the web UI once the
   device is on the network.
3. **Keep automatic inverter control disabled** until the meter has been reading stable, correct
   values for a sustained period; the control loop and safety limits are implemented but have not
   been exercised against live meter data.
4. Consider raising `CONFIG_ESP_MAIN_TASK_STACK_SIZE` as defence in depth, though it is no longer
   required by this code path.
