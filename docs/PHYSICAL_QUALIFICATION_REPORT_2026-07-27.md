# ESP32 PV-DG Physical Qualification Report - 2026-07-27

## 1. Executive result

FINAL CLASSIFICATION: `BLOCKED - Phase 2B complete, flashing approval pending`

Reason: Phase 1 source checks passed, Phase 2A pre-flash backups were captured, and Phase 2B serial capture plus authoritative ESP-IDF v6.0.1 build completed. No flash has been performed in this report. The next safety gate is explicit authorization for Phase 3 controlled non-destructive flash.

This report does not qualify the release for automatic PV-DG control.

## 2. Repository branch and commit

- Branch: `feature/pvdg-batch3-consolidated`
- Current commit: `46cb36dad7c30462ff180bb784072e6bdbf5fe8d`
- Shipped commissioning merge commit: `fc5cbf59196f016226bbe1584c718fd32bfeb1b2`
- Remote: `https://github.com/raohassandev/esp32_firmware.git`
- Initial local state before fetch/fast-forward:
  - Branch was `feature/pvdg-batch3-consolidated`
  - HEAD was `7ebf5ed Publish auditable firmware build metrics`
  - Working tree had untracked `build-tenda/`
  - Expected commit `fc5cbf59196f016226bbe1584c718fd32bfeb1b2` was not present locally
- Action taken: `git fetch --all --prune`, then `git merge --ff-only origin/feature/pvdg-batch3-consolidated`
- Result: branch fast-forwarded first to expected PR #9 merge commit, then later to `46cb36d`, which adds the local execution roadmap (deleted as spent on 2026-08-11; recoverable from git history).

## 3. Hardware details

- Target requested: ESP32-S3 DevKitC-1 N16R8
- Serial discovery:
  - `COM12`: Intel(R) Active Management Technology - SOL
  - `COM5`: USB Serial Device, PNP ID `USB\VID_303A&PID_1001&MI_00\7&15970D79&0&0000`
- Interpretation: `COM5` is the likely ESP32-S3 serial port.

## 4. ESP-IDF version

- `idf.py` was not initially on PATH.
- ESP-IDF v5.5.4 export worked using `IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env`.
- Build version: `ESP-IDF v5.5.4`.
- ESP-IDF v5.3.1 was present but blocked by Python package constraint mismatches in its environment.
- Authoritative ESP-IDF v6.0.1 path: `C:\esp\v6.0.1\esp-idf`
- ESP-IDF v6.0.1 tag: `v6.0.1`
- ESP-IDF v6.0.1 commit: `8c19b156084a0753687347cca1f5355782893533`
- ESP-IDF v6.0.1 tool versions: Python `3.14.4`, GCC `15.2.0`, CMake `4.0.3`, Ninja `1.12.1`

## 5. Flash method

No flash was performed.

Phase 3 flash is not authorized until Phase 2B is reviewed and the next instruction explicitly permits controlled non-destructive flash.

## 6. Configuration backup hashes

Pre-flash backups are saved under `evidence/2026-07-27/preflash/`.

- SHA256 file: `evidence/2026-07-27/preflash/SHA256SUMS.txt`
- `/api/config` SHA256: `FE1372ACEE69A32A40DD615760C079C093E3143FF0D59C7848FACE97183FF0F0`
- Serial boot log SHA256: `50A019D1B131FBD9B2C2A74BC730D43551C837BCCC2C6E00030E7A6E93B66762`

## 7. Build metrics

- Command: `idf.py set-target esp32s3`
- Command: `idf.py build`
- Rebuild evidence log: `build/phase1_idf_build_verify.log`
- Application SHA/version: `fc5cbf5`
- Application binary: `build/automatrix_pvdg.bin`
- Application binary size: `0x106940` bytes, 1,075,520 bytes
- Smallest app partition: `0x300000` bytes
- Application partition headroom: `0x1f96c0` bytes, 2,070,208 bytes, 66% free
- Bootloader binary: `build/bootloader/bootloader.bin`
- Bootloader size: `0x5160` bytes, 20,832 bytes
- Bootloader headroom: `0x2ea0` bytes, 11,936 bytes, 36% free
- Compiler warning count from verify log: 0
- Application SHA256: `CEA22BF7F955DFC632858629FBB1D01FF1E7D157B99721E4B5AF98B1378E71CA`
- Bootloader SHA256: `A676E9988E2617A1280F5EF0092A92116FFA868A6F3617C22AE3619536B46694`

Authoritative ESP-IDF v6.0.1 build:

- Build directory: `build-idf601`
- Evidence directory: `evidence/2026-07-27/build-idf601/`
- Command: `idf.py -B build-idf601 set-target esp32s3`
- Command: `idf.py -B build-idf601 build`
- Application binary size: `0x106c10` bytes, 1,076,240 bytes
- Smallest app partition: `0x300000` bytes
- Application partition headroom: `0x1f93f0` bytes, 2,069,488 bytes, 66% free
- Bootloader binary size: `0x5240` bytes, 21,056 bytes
- Bootloader headroom: `0x2dc0` bytes, 11,712 bytes, 36% free
- Partition table size: 3,072 bytes
- Compiler warning count: 0
- Application SHA256: `E3A9D7C82D8F37A844DA983753707D4828F53D4695B27D98EE51193A4B668ED3`
- Bootloader SHA256: `E81EC612FBB01BEBD8BC476D08512A13B4646BB2B1FD14ECDACCD3CA39B078DA`
- Partition table SHA256: `A718889E6C239BACF1E4D512710CF9C638E9EEACEDE2E8425812C3B2E2112EAC`
- `dependencies.lock` was restored from HEAD before the v6.0.1 build and has no tracked diff.

## 8. Browser test results

Not performed against hardware.

Source-level asset delivery checks passed:

- Required EM500 frontend files present:
  - `web/em500.css`
  - `web/em500-utils.js`
  - `web/em500-core.js`
  - `web/em500-profiles.js`
  - `web/em500-plan.js`
  - `web/tests/em500-utils.test.js`
- Required web server source files present:
  - `components/web_server/em500_api.c`
  - `components/web_server/em500_history_api.c`
  - `components/web_server/em500_settings_api.c`
  - `components/web_server/em500_settings_plan_api.c`
- `components/web_server/web_server.c` delivers `/app.css` and `/app.js`.
- `components/web_server/CMakeLists.txt` embeds `em500-utils.js`, `em500-core.js`, `em500-profiles.js`, and `em500-plan.js` in the required order.

## 9. API test results

Phase 2A pre-flash read-only API backup completed against ESP32 hardware at `192.168.0.102`.

- `GET /api/status`: HTTP 200, control disabled, recovery AP inactive, network online
- `GET /api/config`: HTTP 200, `device_name = automatrix-pvdg`, `control.enabled = false`
- `GET /api/meters`: HTTP 200, one configured grid meter at `192.168.0.200:502`, unit `1`
- `GET /api/inverters`: HTTP 200, one configured inverter, disabled, `has_command = false`
- `GET /api/telemetry`: HTTP 200

Phase 2B recheck after build confirmed the device remained reachable with control disabled, recovery AP inactive, and inverter non-commandable.

Read-only discovery probes:

- `http://192.168.0.188/api/status`: failed with TLS/trust relationship error
- `http://192.168.0.188/`: failed with TLS/trust relationship error
- `http://192.168.0.200/api/status`: connection closed unexpectedly
- `http://192.168.0.200/`: HTTP 200, zero-length response

Later positive identification established `192.168.0.102` as the ESP32 controller.

## 10. Meter identity table

Not performed.

| Slave | Model | Evidence | FC03/FC04 | Address base | Role | Confidence |
| ----- | ----- | -------- | --------- | ------------ | ---- | ---------- |
| 1 | Not tested | Physical meter identification not run | Unknown | Unknown | Unknown | None |
| 2 | Not tested | Physical meter identification not run | Unknown | Unknown | Unknown | None |
| 3 | Not tested | Physical meter identification not run | Unknown | Unknown | Unknown | None |

## 11. Measurement verification

Not performed.

## 12. Register `0x2160` results

Not performed.

No conclusions were made about source detection, source-dependent behavior, transition latency, stability, or chatter.

## 13. 30-minute soak-test statistics

Not performed.

## 14. Wi-Fi reconnect-admission results

Not performed.

`docs/WIFI_RECONNECT_ADMISSION_GATE.md` physical suite was not executed.

## 15. Safety evidence

- No NVS erase was performed.
- No `erase-flash` command was run.
- No full-chip erase was run.
- No firmware flash was run.
- No physical meter setup write request was sent.
- No inverter command was sent.
- No automatic PV-DG control was enabled.
- No source-dependent control based on register `0x2160` was implemented or enabled.
- Pre-flash serial boot log shows reset reason `USB_UART_CHIP_RESET`, Wi-Fi association to `Tenda_69B540`, assigned IP `192.168.0.102`, and meter back online after initial network-unavailable polls.
- Serial capture plus API recheck found no panic, Guru Meditation, watchdog, spontaneous reboot loop, automatic-control enable, or inverter command evidence.

## 16. Failures and unresolved issues

- Phase 2A IP discovery blocker resolved: ESP32 is `192.168.0.102`.
- Phase 2B serial blocker resolved: COM5 became available and serial boot plus 100-second runtime evidence was captured.
- TOOLCHAIN NOTE: ESP-IDF v6.0.1 had been installed at `C:\esp\v6.0.1\esp-idf`; the official install script was run to repair the missing Python environment/constraint file expected by `export.ps1`.
- WORKTREE NOTE: `dependencies.lock` was modified earlier by ESP-IDF v5.5.4 component manager, preserved as `build/dependencies.lock.generated-by-idf-5.5.4`, then restored from HEAD. Pre-existing untracked `build-tenda/` remains untouched.
- STOP GATE: no flash authorized until Phase 3 is explicitly permitted.

## 17. Evidence file paths

- Build verification log: `build/phase1_idf_build_verify.log`
- Application binary: `build/automatrix_pvdg.bin`
- Bootloader binary: `build/bootloader/bootloader.bin`
- Pre-flash evidence: `evidence/2026-07-27/preflash/`
- Authoritative v6.0.1 build evidence: `evidence/2026-07-27/build-idf601/`
- Phase 2B serial log: `build/phase2b_preflash_serial.log`
- Phase 2B device recheck: `build/phase2b_device_recheck.json`
- Report: `docs/PHYSICAL_QUALIFICATION_REPORT_2026-07-27.md`

## 18. Go/no-go recommendation

GO/NO-GO: BLOCKED before flashing.

Phase 2B passed. Next safe action is Phase 3 controlled non-destructive flash only after explicit authorization. Do not erase NVS or enable control/inverter writes.
