#!/usr/bin/env python3
"""Production builds must not ship a site STA identity or overwrite NVS Wi-Fi."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
CONFIG = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Normal production/development defaults must contain no site STA identity.
for forbidden in (
    "CONFIG_PVDG_PRIMARY_WIFI_SSID=",
    "CONFIG_PVDG_PRIMARY_WIFI_PASSWORD=",
    "CONFIG_PVDG_DEFAULT_WIFI_SSID=",
    "CONFIG_PVDG_DEFAULT_WIFI_PASSWORD=",
    "CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING=y",
):
    require(forbidden not in DEFAULTS,
            f"sdkconfig.defaults must not ship Wi-Fi provisioning material: {forbidden}")

for symbol in (
    "config PVDG_PRIMARY_WIFI_SSID",
    "config PVDG_PRIMARY_WIFI_PASSWORD",
    "config PVDG_DEFAULT_WIFI_SSID",
    "config PVDG_DEFAULT_WIFI_PASSWORD",
):
    start = KCONFIG.index(symbol)
    block = KCONFIG[start:KCONFIG.find("\nconfig ", start + 1) if "\nconfig " in KCONFIG[start + 1:] else len(KCONFIG)]
    require('default ""' in block, f"{symbol} must default empty")

provision = KCONFIG[KCONFIG.index("config PVDG_APPLY_BUILD_WIFI_PROVISIONING"):]
provision = provision[:provision.find("\nconfig ", 1)]
require("default n" in provision,
        "build-time credential replacement must be opt-in, never a production default")

# The firmware may support explicit factory provisioning, but only behind the
# disabled-by-default build gate and a strictly increasing positive generation.
for token in (
    "#ifndef CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING",
    "return false;",
    "CONFIG_PVDG_WIFI_PROVISION_ID <= 0",
    "CONFIG_PVDG_WIFI_PROVISION_ID <= c->wifi_provision_id",
    "loaded->wifi_provision_id = CONFIG_PVDG_WIFI_PROVISION_ID",
    "loaded->wifi_provision_id = legacy->wifi_provision_id",
):
    require(token in CONFIG, f"commissioned Wi-Fi preservation contract missing: {token}")

print("Wi-Fi build credential and NVS preservation contract passed")
