# Release readiness — Automatrix PV-DG Controller

**Commit:** `1282af8` on `phase1-fix`
**Assessed:** 2026-07-30
**Target hardware:** ESP32-S3-DevKitC-1 N16R8 (16 MB flash, 8 MB octal PSRAM), ESP-IDF v6.0.1

This document records what has been demonstrated on physical hardware, what has
only been demonstrated in software, and what has not been demonstrated at all.
It exists so that a release decision is made against evidence rather than
against a passing build.

---

## 1. The decisive constraint

**No manufacturer profile is write-qualified. The controller cannot command a
real inverter, and this is by design, not by omission.**

| # | Manufacturer | Profile | Qualification |
|---|---|---|---|
| 1 | SolTrix Simulator | `soltrix.sim.huawei.v1` | Simulator verified |
| 2 | SolTrix Simulator | `soltrix.sim.goodwe.v1` | Simulator verified |
| 3 | SolTrix Simulator | `soltrix.sim.solis.v1` | Simulator verified |
| 4 | Huawei | `huawei.sun2000.pending` | Documented only |
| 5 | GoodWe | `goodwe.commercial.pending` | Documented only |
| 6 | Solis | `solis.commercial.pending` | Documented only |
| 7 | FoxESS / Knox | `foxess.commercial.pending` | Documented only |

Write-qualified or production-approved profiles: **0**.

"Documented" means the register map was transcribed from a manual and has never
been exercised against the physical equipment. Promoting a profile requires the
exact manual, a model-specific mapping, simulator evidence, a bench test and a
physical readback qualification — in that order.

**Consequence for this release:** it is a *monitoring, commissioning and
protection* release. It is not a *closed-loop control* release. Automatic
control is structurally inhibited and will remain so until a profile is
qualified against real hardware.

## 2. Demonstrated on physical hardware

Flashed to the board over COM5, hash-verified, and observed live at
192.168.100.14 on `Automatrix-4G`.

| Area | Evidence |
|---|---|
| Boot stability | 13 min continuous uptime, no crash, no reboot loop |
| Wi-Fi | Associated, IP 192.168.100.14, RSSI −49 dBm |
| Meter acquisition | EM500 slave 3 online, quality good |
| **Acquisition latency** | Grid data age **8–123 ms** across repeated samples; control cycle age 75 ms |
| Source detection | Resolved **generator via tariff 2** with 220 V applied to the tariff port |
| Fail-closed control | `control_enabled:false`, `inhibit_reason:"No inverter is enabled."` |
| Alarm state model | `rtn_unacknowledged` observed live on NET-001 and MTR-003 (ISA-18.2 gap A1) |
| Root-cause grouping | `active 2` reduced to `primary_active 1` (gap A5) |
| Nuisance suppression | `suppressed_transitions 1` — on/off delay absorbing chatter (gap A4) |
| Shelving authorisation | Unauthenticated shelve returns **401** |
| Engineering gateway | Commissioning gate, write-confirmation, identity, audit-log all **401** unauthenticated |
| Operator history | 200 with 24.8 KB payload (PSRAM-dependent; previously failed with 500) |

## 3. Demonstrated in software only

| Area | Evidence | Not yet shown |
|---|---|---|
| Full contract suite | **54** Python source contracts, 0 failures | — |
| Executable unit tests | 5 gcc-compiled tests (source mode, source detection, Solar-Grid integration, commissioning gate, write confirmation) | — |
| Browser modules | 3 JS suites, syntax checks on all edited modules | Rendered layout |
| Build | Clean ESP-IDF build, **zero warnings**; app 1,686,144 bytes, 46% of the 3 MB partition free | — |
| Inverter command path | Simulator scenarios incl. rollback, timeout, comm-lost | Any physical inverter |
| Alarm journal durability | Host-compiled ring test: wrap, corruption (exactly one record lost), sequence continuity across reopen | Durability on real SPIFFS |
| Commissioning gate | Nine prerequisites, fail-closed on unreadable state | Payload inspection (needs Engineering password) |
| UI contrast | WCAG arithmetic on parsed token values, 32 pairs, 0 failures | Visual rendering |

## 4. Not demonstrated

1. **Alarm journal durability on hardware.** Confirmed *inert* on the board
   (`storage_ready:false`) because the `storage` partition had never been
   formatted. Provisioning is implemented, tested and built — **but not yet
   flashed**, because COM5 stopped enumerating. Until it is flashed and the
   journal reports `storage_ready:true` with records surviving a restart, gap
   A2 is unverified.
2. **Any physical inverter write.** See section 1.
3. **Write confirmation against real equipment.** The readback evaluator is
   unit-tested, but `INVERTER_CONFIRMATION_SETTLE_MS = 500` and
   `DEADLINE_MS = 5000` are **firmware-side values chosen without a manual** and
   need site validation.
4. **Protected endpoint payloads.** Correctly returning 401; contents
   uninspected pending the Engineering password.
5. **Visual rendering of the UI.** The last visual audit run was invalid (37 of
   60 runs, adapter suspended mid-run) and has not been repeated.
6. **Grid/generator synchronisation interlock.** `fleet_synchronised()` exists
   but is not wired into the control engine, because it needs per-manufacturer
   inverter status registers that have not been supplied.
7. **FAT / SAT.** Not started.

## 5. Open decisions

These are product decisions, deliberately not made unilaterally.

| # | Decision | Current behaviour |
|---|---|---|
| D1 | Should a **disabled generator ramp** block commissioning? | It blocks. "No rate limit" is treated as unsafe rather than inherited from a default. |
| D2 | Should **one unqualified inverter** block the whole plant? | It blocks. Every enabled inverter must be write-qualified and readback-capable. |
| D3 | Was deleting `inverter_command_policy.{c,h}` correct? | Deleted. It decided the same question for a synchronous path that no longer exists; two competing confirmation policies in safety firmware is worse. Reversible. |
| D4 | Settle 500 ms / deadline 5000 ms | Invented values, in force, documented in source as needing validation. |
| D5 | Repeated-mismatch policy | An inverter that mismatches then confirms a safe zero rejoins the fleet. `mismatch_count` is retained but there is no "N strikes and out" latch, so a marginal inverter will cycle. |
| D6 | `.eyebrow` brand orange at **2.23:1** on light background, 10 px | Left untouched, colour and size, pending a brand decision. Pinned by contract so it cannot be silently half-fixed. |

## 6. Inputs still required

- Generator ratings: rated kW, minimum loading %, reserve kW, reverse-power margin
- GoodWe manual (and any other manual intended for write qualification)
- Per-manufacturer inverter **status** registers, for the synchronisation interlock
- Engineering password, to verify protected endpoint payloads

## 7. Recommendation

Release as **monitoring, commissioning and protection firmware**, with automatic
control documented as inhibited pending profile qualification. Do not describe
this build as a closed-loop PV-DG synchronisation controller until at least one
manufacturer profile has passed physical readback qualification and the items in
section 4 are closed.

The two items that would most change this assessment, in order:

1. Flash the provisioning build and confirm the journal survives a restart.
2. Qualify one real inverter profile end to end, on real equipment.
