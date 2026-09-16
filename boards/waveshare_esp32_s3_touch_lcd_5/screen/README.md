# Waveshare 800x480 Display Boundary

This directory is the isolated hardware/display layer for the Waveshare ESP32-S3-Touch-LCD-5 product target.

It was reconciled onto current `dev`; the historical `board/waveshare-esp32-s3-touch-lcd-5` branch is a hardware reference only and is not merged into current firmware.

## Authority boundary

- Current `dev` owns control, safety, configuration, meters, inverters, networking, source detection and web APIs.
- `drivers/` owns only the qualified RGB display/touch hardware port and board profile.
- The new native PV-DG LVGL component is integrated separately after this hardware gate.
- Legacy board-local pages and commissioning backends are intentionally not compiled.

## Product build

From `boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480` use the normal ESP-IDF build flow. This product project is isolated from the repository root project, so the site-tested default firmware build remains unchanged.
