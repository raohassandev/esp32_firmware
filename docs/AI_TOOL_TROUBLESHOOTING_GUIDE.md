# AI Tool Troubleshooting Guide

This guide is for AI coding agents and local automation working in this repo.
It records failure modes that are easy to repeat when the tool can build and
flash firmware but cannot see the hardware.

## Waveshare ESP32-S3-Touch-LCD-5 Black Screen

### Symptom

- Firmware flashes successfully to the ESP32-S3, but the Waveshare 800x480 LCD
  stays black.
- Serial flashing may still identify the chip as ESP32-S3 on `COM8`.

### Do Not Flash The Root Project

The repository root project is a headless controller firmware. It can boot
Product Core but does not initialize the Waveshare RGB panel, LVGL adapter,
GT911 touch, or CH422G backlight. Flashing it to the Waveshare board can produce
a valid boot with a black screen.

For the Waveshare LCD product, build and flash only:

```powershell
D:\Working\esp32_firmware\boards\waveshare_esp32_s3_touch_lcd_5\screen\product_800x480
```

### Known Good Display Baseline

The proven black-screen restore baseline is commit:

```text
2b6dbed089c5d855e39f78ca4a24f4a957bc8b9a
fix(waveshare): restore proven RGB display baseline
```

Key expected properties:

- Native LCD/DMA reservation happens before Product Core.
- RGB pixel clock is 12 MHz.
- RGB mode is `DOUBLE_DIRECT` anti-tear.
- Two PSRAM framebuffers are used.
- RGB bounce buffer is 12 lines.
- RGB panel creation pins the scanout/refill interrupt path to CPU1.
- Product Core's `operational_task` is pinned to CPU0 so its critical section
  cannot share a core with the RGB refill interrupt.
- LVGL activation logs stages 1 through 6, ending with backlight on.

### Required Clean Reconfigure

Old generated config can preserve bad display settings. Before rebuilding the
product image, remove product-local generated config and build output if the
environment allows it:

```powershell
cd D:\Working\esp32_firmware\boards\waveshare_esp32_s3_touch_lcd_5\screen\product_800x480
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
Remove-Item sdkconfig -Force -ErrorAction SilentlyContinue
Remove-Item sdkconfig.old -Force -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
```

If shell deletion is blocked, remove `sdkconfig` and `sdkconfig.old` with the
editing tool and let `idf.py set-target esp32s3` run its IDF-native fullclean.

### Expected Boot Logs

After flashing the product image, monitor serial output for these lines:

```text
Reserving native 800x480 Waveshare LCD/touch DMA before Core
RGB live-update mode: DOUBLE_DIRECT anti-tear
pclk=12000000
LVGL activation stage 1/6
LVGL activation stage 2/6
LVGL activation stage 3/6
LVGL activation stage 4/6
LVGL activation stage 5/6
LVGL activation stage 6/6: backlight on
Native LCD/LVGL/touch ready
```

If these lines appear and the screen is still black, treat it as a physical
panel/backlight/RGB scanout issue, not as proof that the root firmware is OK.

### COM8 Flashing Notes

On this workstation, `COM8` can intermittently fail with Windows pySerial errors:

```text
OSError(22, 'The I/O operation has been aborted...', None, 995)
PermissionError(13, 'A device attached to the system is not functioning.', None, 31)
Invalid head of packet (0x45)
```

Recommended order:

1. Prefer a normal flash first:

   ```powershell
   idf.py -p COM8 flash monitor
   ```

2. If `COM8` drops mid-flash, clear line state:

   ```powershell
   mode COM8 RTS=OFF DTR=OFF
   ```

3. If large app writes repeatedly fail, use `esptool --no-stub` with low baud
   and verified chunks. Resume from the last hash-verified address; do not erase
   NVS unless explicitly requested.

4. If Windows reports the port is busy, missing, or cannot configure the port
   for several retries, ask for a physical USB replug or board power-cycle. Do
   not keep rewriting unverified chunks indefinitely.

5. Do not report the device as flashed unless the app image write completes and
   esptool verifies the app hash. A run that verifies only bootloader,
   partition table, and OTA data, then drops during the app write, leaves the
   device only partially flashed.

Current observed hard-stop signature:

```text
Writing 'automatrix_pvdg_waveshare_800x480.bin' at 0x00020000...
Lost connection, retrying...
ERROR: A serial exception error occurred: Cannot configure port...
ERROR: A fatal error occurred: Could not open COM8, the port is busy or doesn't exist.
```

After this signature, try at most a read-only `chip-id` probe. If that also
cannot open `COM8`, stop and require a physical USB replug or board power-cycle
before another flash attempt.

If the screen is black after a failed/interrupted flash, run `idf.py -p COM8
monitor` before changing display code. This bootloader loop means the app image
is incomplete or corrupt, not that LVGL/backlight reached a black-screen state:

```text
E (...) esp_image: invalid segment length 0xffffffff
E (...) boot: OTA app partition slot 0 is not bootable
E (...) esp_image: image at 0x320000 has invalid magic byte (nothing flashed here?)
E (...) boot: No bootable app partitions in the partition table
```

In that state, close every `idf.py monitor`, `idf_monitor.py`, and
`esp_idf_monitor` process holding the port, physically replug/power-cycle the
board if Windows reports error 31 or 995, then reflash at 115200 and verify the
app hash completes.

### Safety Rule

Do not use `erase-flash` as a black-screen troubleshooting step unless the user
explicitly requests it. Preserve NVS/commissioned state by writing only:

- bootloader at `0x0`
- partition table at `0x8000`
- OTA data at `0xf000`
- product app at `0x20000`
