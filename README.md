# Automatrix ESP32 PV-DG Controller

Compact ESP-IDF firmware for the ESP32-S3 PV-DG controller.

## Current scope

- Grid and generator meter polling through Modbus TCP or an RTU gateway.
- Up to twelve configurable inverter channels.
- Multi-brand inverter profile catalogue with fail-closed production write qualification.
- Periodic inverter identity, active-power and command-readback telemetry.
- SolTrix Modbus TCP simulator tests for Huawei, GoodWe and Solis synthetic profiles.
- Automatic removal of stale/offline inverter capacity.
- Lightweight embedded commissioning interface.
- Persistent light and dark UI themes.
- JSON configuration import and export.

Simulator-only profiles and pending manufacturer profiles cannot issue production writes. Physical automatic inverter control remains locked until exact manuals and hardware qualification are complete.

## Pull the current integration branch

Run in an ESP-IDF Command Prompt from the local repository directory:

```bash
git fetch origin
git switch feature/multibrand-inverter-profiles
git pull --ff-only origin feature/multibrand-inverter-profiles
git rev-parse HEAD
```

## Build and flash

Replace `COM5` with the board's actual serial port:

```bash
idf.py set-target esp32s3
idf.py -p COM5 build flash monitor
```

Normal `flash` preserves the NVS configuration partition. Do not run `erase-flash` unless a destructive reset is intentionally required.

To exit the serial monitor, press `Ctrl+]`.

## One-line Windows command

Replace the repository path and COM port:

```bat
cd /d C:\path\to\esp32_firmware && git fetch origin && git switch feature/multibrand-inverter-profiles && git pull --ff-only origin feature/multibrand-inverter-profiles && git rev-parse HEAD && idf.py set-target esp32s3 && idf.py -p COM5 build flash monitor
```

## Theme control

The sun/moon button in the top bar switches between light and dark themes. The selection is stored in the browser. On first use, the interface follows the operating-system color preference.
