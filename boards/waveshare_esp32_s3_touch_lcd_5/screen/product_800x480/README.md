# Waveshare 800x480 Product Target

This isolated ESP-IDF project composes the current `dev` Product Core with the qualified Waveshare ESP32-S3-Touch-LCD-5 hardware driver.

## Gate A

Gate A proves that current firmware contracts and the isolated 800x480 hardware target can coexist without importing the historical branch's legacy UI or commissioning backends. The default repository firmware project is unchanged.

The display/touch DMA resources are reserved before Product Core startup, matching the qualified board strategy. Product Core remains authoritative for control and safety. If display reservation fails, the controller continues headless; if Product Core initialization fails, the controller remains fail-safe.

No build-time Wi-Fi credentials are embedded in this target.

Gate B adds the `pvdg_ui_native` LVGL component and live host bindings after Gate A is stable.
