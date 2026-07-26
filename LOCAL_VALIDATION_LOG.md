# Local Hardware Validation Log

Physical validation performed on the bench by the local execution agent. ChatGPT owns architecture
and implementation; this document records only what was executed on this Windows machine and the
physical ESP32-S3, so those results can be reviewed without access to the bench.

**Board:** ESP32-S3 DevKitC-1 N16R8, chip rev v0.2, 16 MB flash · **Port:** COM5
**Toolchain:** ESP-IDF v6.0.1, target `esp32s3` · **Node:** v24.15.0 · **npm:** 11.12.1
**Controller network:** SSID `Rao`, currently `192.168.0.105` (DHCP; it has moved during the
engagement, so it is rediscovered from serial after every restart rather than assumed)

**Last updated:** 2026-07-26. Qualification of `fix/pvdg-reconnect-response-delivery` FAILED at the
late-duplicate and drain-extension phases; see §6. Nothing was committed or pushed.


---

## 1. Branch and commit ledger

| Branch | Commit | Local status |
| --- | --- | --- |
| `feature/pvdg-wifi-commissioning` | `fe0dd68` | Batch 2 line, validated except the item in §6 |
| `fix/pvdg-wifi-reconnect-race` | `e7018b9` | Pushed, validated, folded into the Batch 2 line |
| `fix/pvdg-provisioning-safety` | `fe0dd68` | Pushed, validated |
| `fix/pvdg-reconnect-response-delivery` | `a3cdb8a` → local WIP | **Not committed** — qualification in progress |
| `feature/pvdg-device-telemetry` | `1e0d830` | Untouched, never checked out |
| `feature/pvdg-operational-telemetry` | `aca1b27` | Untouched, never checked out |

Isolated worktrees used, so the dirty SolTrix tree and the protected branches were never disturbed:
`esp32_firmware-wifi-race-fix`, `esp32_firmware-provisioning-safety`,
`esp32_firmware-reconnect-response`, `esp32_firmware-docs`.

`D:\Working\FUXA SADA\FUXA-1.3.2` and `D:\Working\SolTrix-ESP-Lab-Validation` were not modified at
any point.

---

## 2. Root causes found on hardware

These were found by physical execution, not by reading code, and are the reason several batches
needed rework.

### 2.1 Reboot loop — main-task stack overflow

`app_config_t` is ~2.5 KB. The configuration load path held **two** live at once (loaded plus
read-back verification), roughly 5 KB, against `CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584`. Startup also
aborted on recoverable errors, turning one failure into an endless loop.

Measured proof after the fix: `Bootstrap stack headroom: 10492 bytes of 12288` — peak usage across
the whole NVS + Wi-Fi + HTTP init path is only ~1,796 bytes, so the fix was the removal of the stack
objects and not a larger stack.

### 2.2 Embedded assets served one byte too long

`EMBED_TXTFILES` appends a NUL terminator and `web_assets.c` returned `end - start`, so every asset
was served with a trailing `0x00`. HTML and CSS parsers tolerate it; JavaScript does not. `app.js`
is 651 lines and `node --check` passes on the source, but the **served** bytes failed at line 652 —
the phantom line holding the NUL — so the SPA never initialised and every route rendered identically.
Confirmed by reproducing the browser error with `node --check` on the downloaded file.

### 2.3 Recovery AP activated by a scan/reconnect race

`POST /api/wifi/scan` immediately followed by `POST /api/wifi/rescan` briefly activated the recovery
AP. Both the disconnect event handler and the manager task owned the same operator reconnect, so the
handler consumed the retry budget and reached `start_fallback_ap()` while a usable profile was still
pending.

A first repair attempt still produced **10 AP activations in 10 cycles**: `rescan` and the
disconnect-ack both signalled `CONNECT_REQUEST_BIT`, separated by the reconnect backoff so they never
coalesced, and the resulting second sweep ran mid-connect, failed its scan with `ESP_ERR_WIFI_STATE`
and fell through to the AP. Only serial evidence caught this — per-cycle API polling reported
`fallback_ap_active=false` because it sampled after the ~2 s window had closed. **API polling is not
sufficient to qualify this behaviour; serial is authoritative.**

### 2.4 Build provisioning silently re-provisioned a commissioned device

A build from a fresh worktree has no `sdkconfig` (gitignored), so it was regenerated from Kconfig
defaults — `PVDG_PRIMARY_WIFI_SSID="Rao-EXT"`, `PVDG_WIFI_PROVISION_ID=0`. Because that id merely
*differed* from the stored one, `apply_build_provisioning()` overwrote the stored credentials and
`config_manager_init()` persisted them. The controller dropped to the recovery AP unable to find
`Rao-EXT`. No POST was involved — the firmware wrote NVS itself.

Recovered by restoring the protected `sdkconfig`, rebuilding and reflashing; `/api/config` returned
byte-identical to the pre-test backup. **Any build from a fresh worktree will re-provision a device
unless provisioning is opt-in**, which is what `fix/pvdg-provisioning-safety` addresses.

### 2.5 Fixed request-start grace is load-sensitive

With a 500 ms timer started at request arrival, complete-response latency reached **476.5 ms** while
a Wi-Fi scan was in flight — about 5% margin — and **2 of 30** race cycles lost one or both
responses. Idle reconnects completed in 52–87 ms. This is why the fixed timer was rejected for
release and replaced with the completion-aware design under test in §6.

---

## 3. Measurement pitfalls worth knowing

Recorded because two of them produced wrong conclusions before being caught.

- **Whole-partition NVS hashing is not a valid invariant.** Two consecutive reads with **no flash in
  between** differ. Per-page hashing localises it: only pages 2–3 move, and those hold the ESP-IDF
  `nvs.net80211` namespace plus `misc`/PHY data, rewritten on every boot — and each `read-flash`
  resets the board, so every measurement causes one. Page 0, holding the `pvdg`/`config` blob, was
  identical across all four reads (`ec9be71af390150e`). The application-level invariant to assert is
  page 0 / `/api/config`, not the partition hash.
- **NVS is unencrypted, so any partition dump is credential-bearing.** A diagnostic string extraction
  printed the PSK in cleartext. All raw dumps were deleted and only hashes retained. NVS encryption
  and credential rotation remain open security actions.
- **Windows `netsh wlan show networks` is unreliable here.** It reported a single SSID while the ESP
  concurrently saw 9–15, and it never showed the ESP's own recovery AP even when the driver log
  proved the AP was up (`sta + softAP`, beacon buffers, DHCP server on 192.168.4.1). An earlier
  "AP not broadcasting" conclusion drawn from it was wrong.
- **Screenshot viewports can be silently mis-scaled.** `--window-size=390,844` produced a 504 CSS-px
  layout rendered into a 390 px image, which looked like mobile overflow. Measuring through CDP
  `Emulation.setDeviceMetricsOverride` showed no page overflow at all. An earlier overflow report was
  a false alarm.
- **`getComputedStyle(el, ':focus-visible')` does not work.** Use real Tab key events plus
  `el.matches(':focus-visible')`.
- **A reconnect destroys the transport carrying its own HTTP response.** Before the completion-aware
  change, 202/409 status codes were unobservable over Wi-Fi; only serial ack counts showed that
  duplicates were rejected. This is precisely what §6 exists to fix.
- **The local GCC intermittently ICEs** on `esp_lcd_panel_rgb.c:741` (unrelated to project code).
  Retrying, or building at reduced parallelism (`ninja -j 2`), clears it. `idf.py` has no `-j` option.

---

## 4. Batch 1 — embedded web application (validated)

Base `4b0a256` → `2c75142`.

- Clean build, 921,536 bytes, 71% partition free, no project warnings; all assets embedded.
- Flashed COM5, hashes verified; no erase-flash, no NVS erase.
- 75 s runtime: 1 intentional reset, 0 panics, 0 stack overflows.
- After the §2.2 fix: served lengths equal source exactly (19,272 / 17,397 / 32,558), no trailing
  NUL, `node --check` clean on the served bundle.
- Headers correct on all three assets: proper MIME, `Cache-Control: no-store`,
  `X-Content-Type-Options: nosniff`.
- All six SPA routes activate the correct page, nav item, title and breadcrumb; six distinct DOMs;
  status populated from `/api/status`; **0** JavaScript errors.
- Desktop 1440×900 and mobile 390×844: no page-level horizontal overflow (390/390 doc and body). The
  12 elements extending past the viewport are all inside `.status-strip`, its intended internal
  scroll.
- Read-only controls confirmed `disabled` in the live DOM: `meterPoll`, `meterTimeout`,
  `controlStaleTimeout`, `saveJsonButton`.
- Passwords masked in `/api/config`; all three password inputs render blank.

---

## 5. Batch 2 — Wi-Fi commissioning (validated)

Base `2c75142` → `e544633` → `e7018b9` → `fe0dd68`.

- Build 959,520 bytes, 69% free, no project warnings; all 14 embedded symbols present.
- `wifi-utils test passed`; 52 static `byId` references resolved, 0 missing. Six IDs are created at
  runtime by `wifi.js installScanPanel()` and were verified present after browser init.
- Asset streaming byte-exact: served `app.js` = `app.js + wifi-utils.js + wifi-guard.js + wifi.js`,
  served `app.css` = `app.css + wifi.css`, verified with `cmp`. Chunked transfer, no truncation.
- Scan API: initial idle, `POST` → 202, immediate second `POST` → 409, completion in 2,892 ms,
  10 networks, unique non-empty SSIDs, strict RSSI ordering, `Rao` flagged connected and primary.
  Record fields are exactly `ssid, rssi, channel, auth_mode, security, configured_primary,
  configured_fallback, connected` — no BSSID, MAC, password or credential field, and no MAC pattern
  anywhere in the payload.
- Ten-scan stress: 10/10 accepted, generation +1 each, 0 duplicate SSIDs, 0 reachability failures,
  0 allocation failures, control disabled throughout.
- **20/20 negative configuration tests rejected with HTTP 400 and zero configuration mutation**
  (identical SSIDs, disabled primary, empty SSID, bad IPv4, non-contiguous netmask, gateway off
  subnet, IP==gateway, IP==network, IP==broadcast, bad DNS, short password, empty recovery SSID,
  short recovery password, retries 0 and 21, backoff 100 and 60001, malformed JSON, oversize body,
  missing profile objects).
- Password preservation: posting the unchanged Wi-Fi object with `********` returned
  `saved/persisted/restart_required`, config stayed byte-identical, no password blanked, link kept.
- Full UI save → restart → restore workflow: only `reconnect_backoff_ms` changed (2000→2500), clean
  `rst:0xc`, then restored **byte-identical** to the original backup.
- Deterministic synthetic security test through a mock harness serving the real files: WEP,
  WPA2-Enterprise, WAPI-PSK and WPA3-Enterprise-192 stay visible with labels and **both** buttons
  disabled plus explanatory title; Open and OWE are selectable, leave the password blank, and the
  generated payload carries `"password":""` with `"clear_password":true`. Not posted to the device.
- Scan-state sequence via MutationObserver: `Not scanned` → **`Starting…` (2.2 ms)** → `Scanning…`
  (633 ms) → `7 found` (3,398 ms). An earlier "Starting missing" note was a polling artefact.
- Keyboard focus: 38/38 tab stops satisfy `:focus-visible` with a 2 px solid `rgb(76,167,255)`
  outline; screenshots captured for nav link, scan button, SSID input and save button.

### 5.1 Provisioning safety (`fe0dd68`)

`PVDG_APPLY_BUILD_WIFI_PROVISIONING` bool, default `n`, tested with `#ifdef` because a disabled
Kconfig bool has no macro. Application requires a strictly higher **positive** id than the stored
one; a blank compiled SSID neither provisions nor consumes the id.

Both regression builds behaved correctly on hardware — the fresh-default build (the previously
destructive case) and the production-sdkconfig build each produced `Applied build provisioning: 0`,
`Rao-EXT mentions: 0`, `safe defaults: 0`, reconnected to `Rao`, and left `/api/config`
byte-identical.

### 5.2 Reconnect race repair (`e7018b9`)

Single-owner transition with `s_operator_reconnect_pending` guarded by `s_lock`; the disconnect
handler acknowledges the intentional disconnect without competing; exactly one wakeup reaches the
manager task. Validated: 10 baseline reconnects and 25 scan/rescan race cycles produced **zero**
recovery-AP activations in serial, no panic, watchdog or reset, configuration byte-identical.

---

## 6. Batch 2 open item — reconnect response delivery (IN PROGRESS)

Branch `fix/pvdg-reconnect-response-delivery`. **Not committed and not pushed.** Batch 2 and PR #6
must stay draft until this closes.

The fixed 500 ms request-start timer was rejected for release (§2.5). Replaced with a
completion-aware two-phase interface:

- `network_manager_operator_reconnect_response_begin(bool *accepted)` — increments an in-flight
  counter and atomically claims the pending flag under `s_lock`, returning `accepted=true` only to
  the owning request.
- `network_manager_operator_reconnect_response_complete(bool accepted, esp_err_t send_result)` —
  decrements exactly once with an underflow guard, stamps the completion tick, arms the transition
  for the accepted request, logs an undelivered acknowledgement without exposing credentials, and
  never touches the radio.
- `wifi_rescan_post()` order: begin → decide → send → capture send result → complete → return the
  send result. **0** `esp_wifi_*` calls remain in `web_api.c`.
- Gate opens only when pending **and** armed **and** in-flight is zero **and** ≥500 ms has elapsed
  since the *most recent* completion, recomputed on each recheck, so a late duplicate extends rather
  than races the deadline.
- Fixed an event-bit hazard in the prior implementation: `xEventGroupWaitBits` with clear-on-exit
  consumed both bits and the operator branch's `continue` silently discarded an unrelated
  `CONNECT_REQUEST_BIT`. It is now re-set before servicing the reconnect.
- `xTaskCreate` count remains 1 — no additional worker task.

Changed files: `network_manager.c`, `network_manager.h`, `web_api.c` (125 insertions, 27 deletions).
Build exit 0, 0xea880, 69% free, no project warnings. Flashed without erase; runtime clean — 1
reset, 0 panic/watchdog, 0 provisioning, 0 safe-default load, `/api/config` byte-identical.

### Results — QUALIFICATION FAILED, not committed, not pushed

| Phase | Result |
| --- | --- |
| Single reconnect ×10 | **Pass** — 10/10 complete 202, valid bodies, max latency 63.4 ms |
| Simultaneous ×50 | **Pass** — 50 × 202 and 50 × 409, 0 missing, 0 HTTP 500, max latency 337 ms, 50/50 cycles |
| Late duplicate 300 ms ×20 | **Pass** — 20 × 202 and 20 × 409, 0 missing, max latency 363 ms, 20/20 cycles |
| Late duplicate 450 ms ×10 | **FAIL** — 202 10/10, but **409 only 2/10; 8 duplicates received no response** |
| Five sockets ×20 | **FAIL** — 202 20/20, but **409 79/80; 1 duplicate lost**; 19/20 cycles |
| Drain-extension timing probe ×8 @450 ms | **FAIL** — see below |
| Active-scan stress ×100 | **NOT EXECUTED** — driver stopped deliberately; `scan100.txt` is 0 bytes and no result is claimed |
| UI reconnect ×5 | **NOT EXECUTED** — §17 stops qualification once a phase fails |

### The failure

A dedicated probe measured how long the STA link survives *after the duplicate's own response
completed*, which is the property §12 actually requires (≥500 ms). The discriminator is sharp: with
the duplicate sent at +450 ms, a request-start timer leaves roughly **0 ms**, whereas a
completion-aware drain must leave roughly **500 ms**.

| Cycle | Duplicate completed at | Last successful probe | Survival after duplicate | Meets ≥500 ms |
| --- | --- | --- | --- | --- |
| 2 | t0+557.5 ms | t0+711.0 ms | **153.5 ms** | no |
| 4 | t0+526.9 ms | t0+864.5 ms | **337.5 ms** | no |
| 1, 3, 5, 6, 7, 8 | duplicate unanswered | — | — | no |

**0 of 8 cycles met the drain requirement.** The two answered duplicates completed at t0+527 ms and
t0+557 ms — essentially *at* the gate-opening instant (accepted response completes ~60 ms, plus the
500 ms drain ≈ 560 ms) rather than a full drain before it.

Reading the state machine against this data, the in-flight counter protects a duplicate only when its
`begin()` lands **before** the manager task's final gate evaluation. A duplicate whose `begin()`
arrives a few milliseconds *after* that read is unprotected: the manager has already called
`begin_operator_reconnect()` and disconnected, and the 409 then races the radio teardown. The gate
read and the `begin()` claim are not atomic with respect to each other, so the 450 ms sub-test lands
exactly in that window. Judging by these numbers this is a narrow but real ordering gap, not merely
machine load — although load is a genuine confounder (see caveats).

**Caveats on the probe, stated so the numbers are not over-read:** it treats any failed `/api/status`
fetch as link death, so socket pressure can produce a false "dead" reading and `lastAlive` is a lower
bound with up to ~100 ms of polling error. It also cannot distinguish "ESP disconnected early" from
"my request arrived late" for the six unanswered cycles. The two *answered* cycles are the trustworthy
evidence, and both fall well short of 500 ms.

**Machine-load context:** an unrelated SolTrix `enduser-FE` vitest run (~18 node processes) started
on this PC at 16:03 and CPU sat around 47% during these phases. That inflates client-side jitter and
is a real confounder for the six unanswered cycles. It does not explain the two measured survival
intervals, which are ESP-side.

### What did hold

Across **110 reconnect cycles** executed today, serial shows **0** recovery-AP activations
(`setup AP`, `Recovery AP serving` and `stopping recovery AP` all absent), **0** panics, **0**
watchdogs, **0** unexpected resets, and **0** "response could not be delivered". Ack spacing is
uniform at 12.2–13.0 s with exactly one reconnect per cycle, so there are no duplicate reconnect
sequences. The recovery-AP regression from §2.3 is thoroughly closed and the firmware never wedges.

`/api/config` is byte-identical to the pre-test backup; SSID `Rao`, meter `192.168.0.200:502` unit 1
address 58 scale 0.01 poll 250 timeout 500; control and inverter disabled; no configuration POST, no
NVS erase, no raw NVS read, no credential exposure.

### Handed back to ChatGPT

Per §17 the source was not modified to make the test pass, nothing was committed and nothing was
pushed. `fix/pvdg-reconnect-response-delivery` remains at `a3cdb8a` on the remote with the two-phase
work present only as an uncommitted local diff. Batch 2 and PR #6 stay draft.

The design question for ChatGPT: make the gate decision and the `begin()` claim atomic with respect
to each other, so a duplicate arriving at the deadline cannot slip past the gate read. Options worth
considering are evaluating the gate under the same `s_lock` acquisition that would admit a new
`begin()`, or having the accepted request publish an explicit "no further duplicates" latch before
the manager is allowed to proceed.

---

## 7. Standing safety state

Verified after every phase to date:

- Connected to `Rao`; recovery AP inactive.
- Production meter unchanged: `192.168.0.200:502`, unit 1, active-power address **58**, scale 0.01,
  poll 250 ms, timeout 500 ms.
- **Automatic control disabled and no inverter enabled at any point.** No inverter Modbus command was
  ever issued.
- No full-flash erase and no NVS erase, ever.
- Passwords masked in every API response; no plaintext credential in any committed log.

### Open engineering question for ChatGPT

The production meter still uses `active_power_address: 58`, but the firmware sends the configured
address verbatim (`modbus_tcp.c`, no ±1 conversion anywhere in `components/`), and the SolTrix
EM-500 lab run proved **57** is the correct PDU for this firmware. The stored 58 looks like a
leftover FUXA `+1` value. It was restored exactly as found rather than silently changed — this is a
decision for ChatGPT.

---

## 8. SolTrix EM-500 simulator validation (separate repository, read-only)

Validated in a detached worktree at `d566c37`; the dirty `dev` tree was never touched.

- `npm test` exit 0: legacy self-test plus the ESP firmware lab test, no dependencies installed.
- Read-only lab simulator: Huawei unit 1 and EM-500 unit 10, both `writes=disabled`.
- Huawei FC03 PDU 32080 → 74.901 kW; EM-500 FC03 PDU 57 → −4.901 kW export; plant balance exact
  (70 − 74.901 = −4.901). Import energy PDU 6943 → 12,500.00 kWh.
- Unknown unit 99 → exception 0x04; undefined register 9999 → 0x02; FC06 write rejected 0x02 in
  read-only mode.
- ESP-to-PC proof: TCP `192.168.0.110:51569 → 192.168.0.109:1502` established, meter online, 30 s
  stable, 0 errors.
- Loss and recovery: offline at 2.2 s, stale at 6.5 s, last valid value retained but flagged;
  recovery ≤2.3 s without restarting the ESP.
- Curtailment: 50% → 50.000 kW / grid +20.000 kW; 70% → 70.000 kW / grid 0.000; 100% → 84.504 kW /
  grid −14.504 kW. EM-500 write rejected 0x02 even with writes enabled.
- ESP configuration restored **byte-identical** afterwards; the ZLAN gateway is powered down, which
  is external and confirmed unreachable from this PC too.

---

## 9. What remains

1. Finish the §6 qualification phases and report every cycle.
2. If all pass: commit `Make reconnect response completion-aware` and push only
   `fix/pvdg-reconnect-response-delivery`. No PR, no protected-branch changes.
3. If any cycle fails: no commit, no push, exact evidence returned instead.
4. Separate security actions, not blocking: NVS encryption and Wi-Fi credential rotation.
5. Open decision: meter PDU address 58 vs 57 (§7).
