# ESP32-S3 local firmware validation — 2026-09-15

**Status:** PARTIAL. The current `dev` firmware built, flashed and booted on the connected controller. Station networking and the Rozwel EM-500 meter were not qualified because the saved station SSID was unavailable at this location. This is a diagnostic run, not FAT, SAT, HIL or a production release qualification.

## Identity and build

- Repository: `raohassandev/esp32_firmware`; tested branch/source: `origin/dev` at `ac436b18f1f5e0437a2ad523c352ac8fac0591c6`. The local `main` checkout was clean at `aade4c68089e4a7688ff9c86460c15546d76e835` before execution.
- Controller: positively probed ESP32-S3 rev 0.2, MAC `9c:13:9e:af:c3:b0`, `/dev/cu.usbmodem11101`. The COM5 reference in older Windows documentation does not apply to this macOS session. Project configuration selects the N16R8 class: 16 MB flash, 8 MB octal PSRAM, `esp32s3`, custom `partitions.csv`, OTA slots of 3 MB at `0x20000` and `0x320000`.
- Toolchain: ESP-IDF v6.0.1, `idf.py build`, fresh worktree and generated `sdkconfig` from `sdkconfig.defaults`. Bootloader compile time reported by the board: 2026-09-15 10:05:56 local (Asia/Karachi). Build PASS; no compiler `warning:` or `error:` lines. ESP-IDF emitted Kconfig parser notes about invalid bool defaults in NimBLE/FATFS and duplicate Bluetooth rename mappings. Application size check: `0x1acdd0`, with `0x153230` (44%) free in the smallest app slot.
- Final `build/automatrix_pvdg.bin`: 1,756,624 bytes, SHA-256 `796a522862b07bb320ecf2f7ad34b406af078b0589fac847c9c1d01ab211e2e3`. ELF: `build/automatrix_pvdg.elf`, SHA-256 `539cb91ea92c55ce632cb7fc42ae03cb784efda8f2caf7b67f074481453e262a`.

## Flash and runtime

- `idf.py -p /dev/cu.usbmodem11101 flash` PASS. Bootloader at `0x0`, partition table at `0x8000`, initial OTA metadata at `0xf000`, application at `0x20000`; all four writes reported `Hash of data verified`, followed by RTS reset. Neither full-flash erase nor NVS erase was run. The retained logs normalize serial carriage returns and trim progress-line whitespace without changing the reported measurements or results. [Flash log](../evidence/physical-2026-09-15/flash.log).
- A serial reset-to-runtime capture at 115200 baud lasted 55 seconds. Reset reason was `USB_UART_CHIP_RESET` from the deliberate serial reset. The bootloader loaded `ota_0` at `0x20000`, detected 8 MB PSRAM, and the application reached `PV-DG controller started`. The recorded period shows no boot loop, panic, watchdog reset, brownout or PSRAM failure. Bootstrap stack headroom was 10,196/12,288 bytes; free heap was 8,411,792 bytes at application start. [Runtime log](../evidence/physical-2026-09-15/runtime.log).
- Saved schema 6 configuration was loaded without a persistence error. Grid/source evidence was uncommissioned; automatic Solar-Grid control remained fail-closed. This is a limited boot/runtime PASS, not proof of control accuracy or endurance.

## Network and meter

- Saved primary STA SSID `Rao` was absent from the scan. Five STA association attempts ended with reason 201. The controller then enabled `Automatrix-PVDG-Setup`; its recovery AP DHCP server reported `192.168.4.1`. No STA IP, gateway or station link was acquired. Ethernet is not configured in this observed firmware run.
- The saved meter endpoints in the log were `192.168.0.102:1502` for Grid Meter, GEN-1 and GEN-2. Their initial polls returned `ESP_ERR_INVALID_STATE` because the station network had no route. No TCP connection or valid Modbus response was observed. The host also routed `192.168.0.102` through gateway `192.168.100.1`; a read-only TCP connect probe to port 1502 timed out. The endpoint's identity as ZLAN and the unit IDs/register decoding were not physically confirmed in this run. Rozwel EM-500 values, scaling and sign therefore remain unverified. No Modbus control writes were made.

## Diagnostic sequence and preservation

The initial clean `main` image (schema 5) built and flashed successfully but rejected the board's stored configuration and logged `Configuration persistence failed (ESP_ERR_INVALID_ARG)`. A read-only NVS backup established that the existing `pvdg/config` blob had magic `0x50564447`, schema 6 and 2,524 bytes. This was a firmware/configuration version mismatch, not evidence of corrupt NVS. The NVS partition was not erased; the newer `dev` image was built and flashed, after which the saved schema 6 profile loaded. The NVS backup was kept outside Git because it may contain credentials. The [initial flash log](../evidence/physical-2026-09-15/main-flash.log) and [initial runtime log](../evidence/physical-2026-09-15/main-runtime.log) retain the diagnostic evidence. No firmware source was changed.

## Verification and remaining work

Software checks PASS: `config_import_safety_source_contract.py`, `meter_role_source_contract.py`, `modbus_runtime_safety_source_contract.py`, `modbus_connection_modes_source_contract.py`, `ota_update_behavior_contract.py` and `production_release_gate.py`. The last check explicitly reports that production inverter approval is still blocked by design. These tests do not qualify physical meter communication, site mappings or control.

**NETWORK / SITE CONFIGURATION:** make the commissioned station network `Rao` available to this controller, or commission an approved reachable site profile through the supported interface. Then capture actual STA IP/routing, identify the ZLAN endpoint and unit ID from site configuration, and obtain valid read-only Modbus request/response plus decoded EM-500 values. **HARDWARE / RELEASE:** the frozen Waveshare candidate, generator transition, site source mapping, inverter profiles, OTA, FAT/endurance and SAT remain governed by the live release TODO and require their own exact-image physical evidence. This 55-second DevKitC diagnostic cannot close those gates.
