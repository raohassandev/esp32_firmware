# Waveshare Screen Completion Checklist

Scope: dedicated Waveshare ESP32-S3-Touch-LCD-5 800x480 product target integrated with the current shared Product Core in PR #179.

Status vocabulary:
- `[x]` = implemented and covered by current software/build evidence.
- `[ ]` = still requires physical evidence or an external dependency.
- Software/build PASS never substitutes for physical acceptance.

## 1. Current-Core integration

- [x] Start integration from live `dev` rather than stale local state.
- [x] Keep the historical Waveshare board source as a reviewed hardware/UI baseline only; do not inherit its physical PASS.
- [x] Port native provider/commissioning adapters to current APP_CONFIG_VERSION 6 and Solar-Grid v4 contracts.
- [x] Remove references to retired commissioning-gate/runtime-enable APIs.
- [x] Do not revive retired meter/inverter/load-sharing/base-load/urgent-ramp schema fields in Core.
- [x] Retired compatibility DTO fields remain neutral and backend guards reject non-default legacy values rather than guessing translations.
- [x] Preserve Core source/electrical semantics; the LCD never guesses source attribution or breaker evidence.

## 2. Native data/read models

- [x] Bounded screen-owned models/parsers for live/status/meters/inverters/telemetry/events/alarms.
- [x] Preserve null/unknown/unavailable values rather than coercing them to measured zero.
- [x] Use current Core source attribution and control/inhibit evidence.
- [x] Native events use the same authoritative Core event ring as the web operator route.
- [x] Native alarms use the same authoritative Core alarm lifecycle table as the web operator route.
- [x] Provider is in-process; no second backend or loopback HTTP authority is created.
- [x] Refresh lanes are page-aware and bounded; large snapshots are kept off the LVGL task stack.

## 3. Native operator surfaces

- [x] Overview implemented.
- [x] Grid/meters implemented.
- [x] Solar/inverters implemented.
- [x] Alarms/events implemented.
- [x] Runtime Readiness implemented.
- [x] Lazy page creation prevents all seven page trees consuming boot-time memory before first frame.
- [x] Alarms page has bounded `All / Active / Unack` filtering.
- [x] Alarms page has bounded `Priority / State / ID` sorting.
- [x] Per-row alarm acknowledgement is available for outstanding alarms.
- [x] Alarm acknowledgement mutates only the shared authoritative Core lifecycle.
- [x] Runtime Readiness explicitly separates runtime command authority from production qualification.

## 4. Engineering and commissioning

- [x] Commissioning uses the same Engineering setup-code/permanent-password authority and shared lockout state as the protected web workspace.
- [x] No second local PIN/credential authority exists.
- [x] Production Kconfig exposes no board-local compile-time Engineering credential-prefill option.
- [x] Meter/inverter/site/plant configuration writes go through current Core validation/persistence.
- [x] Configuration writes force running automatic control disabled before persistent changes.
- [x] ARM persists automatic control for the next restart; current runtime remains disabled.
- [x] After restart current Core starts fail-safe at zero PV command and requires current evidence gates before command authority.
- [x] Source-evidence commissioning preserves existing Core source semantics and does not invent signal mapping.
- [x] Source-evidence writes require Engineering unlock.
- [x] Alarm acknowledgement requires an unlocked local Engineering session.

## 5. Board/display/runtime integration

- [x] Reviewed Waveshare upstream baseline retained for hardware reference.
- [x] 800x480 RGB/touch product target compiles against ESP-IDF 6.0.1.
- [x] LVGL `9.5.0` exact-pinned.
- [x] `esp_lvgl_adapter 0.6.2` exact-pinned.
- [x] GT911 `1.2.0` exact-pinned.
- [x] Product `esp_flash_dispatcher 1.0.3` exact-pinned.
- [x] Bootloader application rollback enabled.
- [x] Build-time Wi-Fi provisioning disabled and compiled STA credentials empty in candidate CI.
- [x] Board-specific PSRAM/LVGL/internal-DMA policy is checked by exact-candidate CI.
- [x] Product runtime emits periodic heap/minimum-heap/PSRAM/DMA/stack evidence for soak validation.
- [x] Dedicated exact-head diagnostic workflow emits build logs on failure.

## 6. Exact candidate software/build gates

Before selecting a physical image, all items below must be green on the same exact head:

- [x] Industrial UI v1 gate.
- [x] Browser resilience release gate.
- [x] Web spinlock safety checks.
- [x] HTTP body parser ownership checks.
- [x] Project app-config stack safety checks.
- [x] Secure OTA always-on regression gate.
- [x] Industrial UI physical-evidence tooling gate.
- [x] Root Firmware/Web checks.
- [x] Waveshare diagnostic build.
- [x] Waveshare exact-candidate build/package.
- [x] Candidate package records exact source SHA/tree, toolchain, generated dependency lock, compile commands, effective sdkconfig and ELF/BIN/UF2 hashes.
- [x] Candidate tar is deterministic and package SHA256 manifest is independently verifiable.

These checkmarks describe the last verified software checkpoint. Any source change after that checkpoint requires a fresh exact-head run before physical testing.

## 7. Physical acceptance — issue #174

The following remain intentionally open until performed on one exact immutable final candidate:

- [ ] Flash the exact candidate package/UF2 without rebuilding or substituting source/config.
- [ ] Cold boot to usable native dashboard with no corruption/sweep/reload/flicker failure.
- [ ] Verify native 800x480 Overview/Grid/Solar/Alarms/Readiness layout and touch targets.
- [ ] Verify Engineering unlock, Commission and Source flows on the physical touchscreen.
- [ ] Verify Operator cannot perform protected mutations.
- [ ] Verify alarm filter/sort interaction on the physical touchscreen.
- [ ] Verify alarm acknowledgement is refused while Engineering is locked.
- [ ] Verify an outstanding alarm can be acknowledged after Engineering unlock and remains active until the plant clears the condition when applicable.
- [ ] Verify current embedded browser UI/operator/Engineering hierarchy, including light/dark readability.
- [ ] Verify unrelated API/status/history/events remain responsive during normal HMI/browser use.
- [ ] Record periodic heap/minimum heap/largest block/internal DMA/PSRAM/stack evidence.
- [ ] Confirm no WDT, panic, Guru Meditation, `NO_MEM`, unexpected reboot, LVGL wedge or unrecovered backend/browser failure.
- [ ] Complete one uninterrupted `>=4 h` run with `>=240` one-minute soak samples on the same exact image.
- [ ] Validate the final serial + human observations with `tools/industrial_ui_physical_acceptance.py` against exact candidate SHA/tree/artifact/application identity.

Partial runs are not additive. Any source/config/binary change creates a new identity and invalidates prior affected physical evidence.

## 8. Promotion boundary

- [ ] Issue #174 complete PASS on one immutable final candidate.
- [ ] Re-check PR head/base/CI after physical PASS.
- [ ] Keep PR #179 Draft until physical evidence genuinely passes.
- [ ] Promote/merge only through the governed PR path; never direct-write to `dev`.

Current lifecycle: **SOFTWARE IMPLEMENTATION COMPLETE PENDING FRESH FINAL-HEAD CI/PACKAGE; PHYSICAL ACCEPTANCE STILL REQUIRED**.
