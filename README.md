# Automatrix ESP32 PV-DG Controller

Minimal ESP-IDF firmware for an ESP32-S3 PV-DG controller.

Current hardware path:

- Real grid/generator meters over RS485
- ZLAN5143D Modbus TCP-to-RTU gateway
- External inverter simulator during development
- Real inverter profiles added later without built-in simulator code

The firmware is intentionally structured for small size, deterministic polling, reliable fail-safe control, and a lightweight embedded HTML interface.
