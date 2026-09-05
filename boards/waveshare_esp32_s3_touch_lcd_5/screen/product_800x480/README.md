# Waveshare 5-inch 800x480 Product Image

This is the dedicated product build for **Waveshare ESP32-S3-Touch-LCD-5 (800x480)**. It composes the shared current Product Core with the board-local native LVGL/touch HMI. The root/default firmware project is not replaced.

## Architecture

The native screen consumes current Core state through the board-local in-process provider. It does **not** own a second backend or loopback HTTP control stack.

Read surfaces include live/status, meters, inverters, telemetry, operator events and alarms. Protected local Engineering mutations reuse the same runtime setup-code/permanent-password authority and lockout state as the Engineering web workspace.

Supported native mutation paths are deliberately narrow:

- current-schema commissioning configuration;
- source-evidence commissioning configuration;
- per-alarm acknowledgement.

These paths never bypass Core validation/safety authority. Configuration changes force running automatic control disabled. Arming is persisted for the next restart; after restart the current Core begins fail-safe at zero PV command and only grants command authority when its own runtime evidence gates pass.

The production target exposes no compile-time Waveshare Engineering credential-prefill option.

## Candidate dependency policy

Board/runtime dependencies are exact-pinned where qualification requires binary reproducibility. In particular the product target pins `espressif/esp_flash_dispatcher ==1.0.3`; screen-level LVGL/adapter/GT911 versions are pinned in `../idf_component.yml`.

The exact-candidate workflow also records the generated dependency lock, compile command database, effective sdkconfig, toolchain and ELF/BIN/UF2 hashes in the immutable package.

## Expected serial evidence

Representative successful startup lines include:

- `Shared Product Core started`
- `Native LCD/LVGL/touch ready; awaiting existing Core data`
- `Local Engineering commissioning backend bound to touchscreen`
- `Local source-evidence commissioning backend bound to touchscreen`
- `Screen refresh task created in PSRAM`

During acceptance the product logs periodic `Screen soak` resource lines including heap, minimum heap, PSRAM, internal DMA and screen-task stack high-water evidence.

## Expected native screen behavior

- `Overview / Grid / Solar / Alarms / Ready / Commission / Source` navigation remains stable at native 800x480.
- Unavailable measurements remain explicit (`--`, unavailable/unknown), never fabricated zero.
- Runtime source/authority text remains fail-closed and owned by Core.
- Alarms expose bounded `All / Active / Unack` filtering, `Priority / State / ID` sorting and per-row acknowledgement.
- Alarm acknowledgement is refused while local Engineering is locked.
- Commissioning/Source writes require Engineering unlock and preserve fail-safe control behavior.
- Runtime Readiness never claims production qualification from runtime command authority.

## Build policy

Do not use an ad-hoc rebuild as the physical qualification image. The governed physical candidate is the artifact emitted by `.github/workflows/waveshare-industrial-ui-candidate.yml` for one exact PR head SHA.

For developer compile checks with ESP-IDF 6.0.1:

```text
cd boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480
idf.py set-target esp32s3
idf.py build
idf.py uf2
```

A developer build is useful for diagnosis only; it does not replace the immutable candidate package or its recorded provenance.

## Physical acceptance

Issue #174 owns final Waveshare physical acceptance. Flash the exact immutable package/UF2 for the selected final head, then execute the native layout/touch/role/alarm matrix and one uninterrupted `>=4 h` / `>=240 sample` resource/backend soak on that same image.

Any source, dependency, sdkconfig or binary change creates a new candidate identity and requires fresh exact-head CI/package evidence and affected physical acceptance.
