# Waveshare 5-inch 800x480 HIL image

Exact target: **Waveshare ESP32-S3-Touch-LCD-5 (800x480)**. Do not use this image for the 1024x600 `5B` variant.

This is a board-only hardware validation image. It initializes the real RGB LCD, CH422G sequencing, GT911 touch, LVGL adapter and the product screen shell. It deliberately does **not** fabricate backend measurements: plant values remain unavailable until the qualified existing-backend provider is bound.

The site-tested root/default firmware remains unchanged and does not include this HIL image.

## Windows ESP-IDF 6.0.1

From the repository root, with the board connected, replace `COM6` if Windows assigned another port:

```bat
call "C:\Espressif\frameworks\esp-idf-v6.0.1\export.bat" && cd /d D:\Working\esp32_firmware\boards\waveshare_esp32_s3_touch_lcd_5\screen\hil_800x480 && idf.py set-target esp32s3 && idf.py build && idf.py -p COM6 flash monitor
```

Expected HIL evidence:

- image boots without reset loop;
- backlight turns on;
- 800x480 UI renders cleanly;
- Overview/Grid/Solar/Alarms/Ready navigation responds to touch;
- unavailable plant values remain `--`/unavailable rather than zero;
- no watchdog, PSRAM, DMA or heap fault appears in the serial log.

Exit monitor with `Ctrl+]`.
