# Waveshare 800x480 black-screen restore — 2026-09-15

**Incident:** The connected module is Waveshare ESP32-S3-Touch-LCD-5 (800x480), as identified by the user after the screen went black. The previous local flash put the repository's root controller firmware `ac436b18f1f5e0437a2ad523c352ac8fac0591c6` on this module. That image boots Product Core but does not contain the board-local RGB LCD/LVGL/touch product app. Serial boot success therefore did not establish display operation. The earlier record incorrectly inferred a DevKitC from a generic ESP32-S3 USB serial device; it is corrected in `LOCAL_PHYSICAL_VALIDATION_2026-09-15.md`.

**Physical status:** Firmware restore flash PASS; LCD/touch initialization and 75-second serial runtime PASS; visible panel/touch verdict PENDING operator observation. This short restore is not the issue #174 full physical acceptance or the required four-hour soak.

## Exact restored image

- Frozen PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`; GitHub Actions artifact ID `10293685030`, `industrial-ui-waveshare-800x480-candidate`, created 2026-09-12 and not expired when fetched.
- Artifact `SHA256SUMS.txt` verified. Product application `automatrix_pvdg_waveshare_800x480.bin`: 2,208,048 bytes, SHA-256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`. Bootloader SHA-256 `b973208118b14c0d7f78d40dd1dd52c08636d7f3b57c9692e7d5744c6c6f0f62`. Partition-table SHA-256 `a718889e6c239bacf1e4d512710cf9c638e9eeacede2e8425812c3b2e2112eac`.
- The candidate targets Waveshare ESP32-S3-Touch-LCD-5 at 800x480, ESP-IDF 6.0.1, ESP32-S3, 16 MB flash and 8 MB octal PSRAM. It uses configuration schema 6 and the same NVS/OTA offsets as the preserved board configuration. This is the immutable candidate package, not a local rebuilt substitute.

## Hardware, flash and serial evidence

- Actual macOS serial device `/dev/cu.usbmodem1101` (renumbered since the previous session); safe chip probe reported ESP32-S3 rev 0.2, MAC `9c:13:9e:af:c3:b0`, matching the earlier controller. The generic USB chip descriptor alone did not identify the LCD model; the user supplied that physical identification.
- Esptool wrote the exact artifact bootloader at `0x0`, partition table at `0x8000`, initial OTA metadata at `0xf000` and application at `0x20000`. All four writes reported `Hash of data verified`, followed by hard reset. Neither full flash nor the NVS partition was erased. [Actual flash log](../evidence/waveshare-restore-2026-09-15/flash.log).
- A reset-to-runtime serial capture at 115200 baud lasted 75 seconds. Reset reason `USB_UART_CHIP_RESET` was from the deliberate serial reset. The board loaded the app at `0x20000`, passed the PSRAM memory test and detected 8 MB PSRAM. It reserved native LCD DMA before Product Core, reported `RGB 800x480`, detected GT911 touch, initialized LVGL, registered the display and touch input, created the visible Overview, logged `backlight on`, and reached `Native LCD/LVGL/touch ready`. Product Core and in-process screen read models started. [Actual runtime log](../evidence/waveshare-restore-2026-09-15/runtime.log).
- The 61-second `Screen soak` sample reported free heap 4,500,940 bytes, PSRAM free 4,476,696 bytes, internal DMA free 51,535 bytes and screen-refresh stack headroom 7,596 bytes. No panic, watchdog reset, brownout, boot loop or display-task crash appears in the captured 75-second period. This proves software reached the native display path; serial logs cannot prove what the human eye sees or whether touch responds.

## Remaining observations and scope

- A panel operator must confirm that Overview is visibly rendered and that touch navigation responds. If the display remains black despite these initialization lines, investigate Waveshare backlight/panel power, CH422G output and RGB scanout on the physical board; do not record a visual PASS from logs alone.
- The saved station SSID `Rao` was absent from the scan. Five association retries ended in recovery AP mode; no station IP or meter route was acquired. Grid/GEN-1/GEN-2 polls to the saved `192.168.0.102:1502` endpoint had no network route. No valid ZLAN/EM-500 Modbus response or value was recorded, and no control write was issued. Automatic control remained fail-closed because explicit grid/source evidence was not commissioned.
- PR #179's issue #174 still requires the exact-image native panel/touch/roles matrix, real board/backend Modbus evidence and uninterrupted >=4 h / >=240-sample acceptance. Generator, site, inverter, OTA, FAT and SAT gates remain unchanged.

**Code changes:** None. The corrective action was to flash the right frozen hardware product image and correct the earlier physical evidence/governance record.
