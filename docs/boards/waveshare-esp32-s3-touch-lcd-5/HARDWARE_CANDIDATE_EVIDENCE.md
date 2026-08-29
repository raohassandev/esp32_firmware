# Waveshare 800x480 — Exact Hardware Candidate Evidence

Status: **SOFTWARE CANDIDATE READY — PHYSICAL ACCEPTANCE OPEN**

This record pins the exact software content and flash image intended for Lane D physical acceptance. It is evidence of software identity/build reproducibility only; it does **not** close any physical display, touch, backend, persistence, resource, or soak row in `STABILIZATION_ACCEPTANCE.md`.

## Frozen A+B+C candidate

- Integration branch: `work/waveshare/stabilization-integration`
- Candidate commit: `ec4fb846875d67a806c2b7cc48b21d5706f91995`
- Candidate tree: `09bf66d223e91fc0a248551d0e39c02eb15a2e52`
- GitHub-tested PR #34 merge ref: `a59f39bdd326d0d7a543f3da244815ac3c4fbf37`
- Tested merge-ref tree and final integration tree are identical: `09bf66d223e91fc0a248551d0e39c02eb15a2e52`

Combined software gates for that exact tree:

- Waveshare backend parity: run `33245780288` — GREEN
- Firmware and web checks: run `33245780283` — GREEN
- Waveshare screen checks: run `33245780314` — GREEN, including screen contracts, vendor provenance, isolated screen build, real LCD/touch HIL image compile, and shared-Core `product-800x480-build`

## Exact-candidate package

Packaging workflow: `.github/workflows/waveshare-candidate-package.yml`

The workflow checks out the frozen candidate commit rather than the Lane D branch content, verifies both the commit and tree, builds with `espressif/idf:v6.0.1`, requires the complete ESP-IDF flash set, creates `SHA256SUMS.txt`, and self-verifies the package with `sha256sum -c` before upload.

First successful package build/upload evidence:

- Workflow run: `33246148229`
- Artifact id: `9712940987`
- Artifact name: `waveshare-800x480-ec4fb846`
- Artifact ZIP SHA256: `ba858ee5e93718c4dedb3f8417f76904712852123b16909ed1db88a55f278fda`
- Candidate manifest inside artifact:
  - `candidate_sha=ec4fb846875d67a806c2b7cc48b21d5706f91995`
  - `candidate_tree=09bf66d223e91fc0a248551d0e39c02eb15a2e52`
  - `esp_idf=6.0.1`
  - `product_dir=boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480`
  - `source_pr=34`

Selected inner file hashes from the artifact `SHA256SUMS.txt`:

- application `automatrix_pvdg_waveshare_800x480.bin`: `57799c5a621ae61c21474132b85588188895839b886f6fa1fb489dc2827f41d1`
- bootloader `bootloader.bin`: `8c316ddcc7dd4c65581e2a7eb4b7e514aea0eebfdf2c80ee8756a4f590e44282`
- partition table `partition-table.bin`: `a718889e6c239bacf1e4d512710cf9c638e9eeacede2e8425812c3b2e2112eac`
- OTA data `ota_data_initial.bin`: `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`
- `flash_args`: `290a63a639abf4ebd9ec5e79d701c00d9e75dce3a5c25bda129dce0572c442c9`
- `flash_project_args`: `290a63a639abf4ebd9ec5e79d701c00d9e75dce3a5c25bda129dce0572c442c9`
- `flasher_args.json`: `c33dcb77d033706fd5ad886064cb51f42bdf9cf8fb2731d5acbf031b5ec370cb`

## Build-generated flash layout

The authoritative offsets are the ESP-IDF-generated `flash_args` packaged with this image:

```text
--flash-mode dio --flash-freq 80m --flash-size 16MB
0x0 bootloader/bootloader.bin
0x8000 partition_table/partition-table.bin
0xf000 ota_data_initial.bin
0x20000 automatrix_pvdg_waveshare_800x480.bin
```

Do not manually substitute different offsets or binaries. The physical run must use the packaged build-generated arguments (or `idf.py flash` from the same exact candidate build) and must record the flashed candidate SHA plus image hash in the evidence log.

## Physical acceptance remains open

The following still require the real Waveshare ESP32-S3 Touch LCD 5 board and serial access:

- visible flicker/shake/tearing and touch stability across all required pages and Wi-Fi activity;
- Commissioning Review unchanged-refresh hold and a real gate-state transition;
- native live/status/meter/inverter/alarm/event parity and loss/recovery;
- >=30 minute Alarms hold with every `Screen soak` resource line retained;
- >=20 save/readback cycles and >=20 save -> reboot -> read cycles;
- persistence failure/partial-save fail-closed behavior and ARM/re-entry proof;
- heap/PSRAM/internal-DMA/task-stack/flash-dispatcher/watchdog/control-cadence evidence;
- minimum 4 continuous hours integrated soak.

No physical row may be marked passed from this document, CI, source review, or package hashes alone. Parent PR #20 must remain Draft until the exact-board acceptance matrix passes.
