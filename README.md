# Automatrix ESP32 PV-DG Controller

Minimal ESP-IDF firmware for the ESP32-S3 PV-DG controller.

## Current scope

- Real grid/generator meter polling through a ZLAN5143D Modbus TCP-to-RTU gateway.
- External inverter simulator or real inverter exposed as Modbus TCP.
- No simulator code inside the firmware.
- Deterministic control task with communication fail-safe.
- Lightweight embedded HTML configuration interface.
- JSON configuration import and export.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

The first implementation reads the configured active-power register from meter 0 and controls configured inverter power-limit registers. Additional AC/DC points and manufacturer-specific profiles will be added through the same compact profile model.
