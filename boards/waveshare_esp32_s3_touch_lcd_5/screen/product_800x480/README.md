# Waveshare 5-inch 800x480 product image

This is the dedicated product build for **Waveshare ESP32-S3-Touch-LCD-5 (800x480)**.
It composes the shared, site-tested Product Core with the board-local LCD/touch UI.
The root/default firmware project is not modified or replaced.

The screen is read-only. It consumes the controller's existing `/api/*` contracts
through `http://127.0.0.1` so control, safety, commissioning, source attribution,
meter/inverter state and alarms remain owned by the existing Core/backend.

Expected serial evidence after flash:

- `Shared Product Core started`
- `Native LCD/LVGL/touch ready`
- `Loopback read-only API provider ready`
- `Screen bound read-only to existing Core API over loopback`

Expected screen evidence:

- Overview/Grid/Solar/Alarms/Ready navigation stays stable;
- `BACKEND: ONLINE` appears once `/api/live` is readable;
- unavailable measurements remain `--`, never fabricated zero;
- source attribution remains the fail-closed `/api/status.source.attributed_to` value.

## Windows / ESP-IDF 6.0.1

```bat
call "C:\Espressif\frameworks\esp-idf-v6.0.1\export.bat" && cd /d D:\Working\esp32_firmware && git fetch origin && git switch board/waveshare-esp32-s3-touch-lcd-5 && git pull --ff-only origin board/waveshare-esp32-s3-touch-lcd-5 && cd /d D:\Working\esp32_firmware\boards\waveshare_esp32_s3_touch_lcd_5\screen\product_800x480 && idf.py set-target esp32s3 && idf.py build && idf.py -p COM8 flash monitor
```

Exit monitor with `Ctrl+]`.
