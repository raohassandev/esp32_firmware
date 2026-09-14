"""Regression contract for complete native Waveshare source commissioning.

The native 800x480 HMI must expose the same authoritative source-evidence
channels as Solar-Grid schema 4: Grid pair, Generator 1..3 run/breaker pairs,
optional Transfer/ATS and optional Grid+Generator synchronism. No channel may
be inferred from measured power, and every mutation must remain fail-closed.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "boards/waveshare_esp32_s3_touch_lcd_5/screen"
HEADER = (SCREEN / "pages/source_commissioning_screen.h").read_text(encoding="utf-8")
UI = (SCREEN / "pages/source_commissioning_screen.c").read_text(encoding="utf-8")
BACKEND = (SCREEN / "product_800x480/main/local_source_commissioning_backend.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Keep native HMI capacity locked to the authoritative Core schema.
require("#define SOLAR_GRID_MAX_GENERATORS 3U" in CORE_H,
        "Core schema is expected to expose three generator channels")
require("#define SOURCE_COMMISSIONING_MAX_GENERATORS 3U" in HEADER,
        "native source commissioning must expose all three generator channels")
require("_Static_assert(SOURCE_COMMISSIONING_MAX_GENERATORS == SOLAR_GRID_MAX_GENERATORS" in BACKEND,
        "compile-time guard must prevent native/Core generator-count drift")

# DTO coverage: no generator, transfer or synchronism evidence may be silently
# omitted from the native commissioning surface.
for token in (
    "grid_evidence_enabled",
    "generator_evidence_enabled[SOURCE_COMMISSIONING_MAX_GENERATORS]",
    "transfer_evidence_enabled",
    "synchronism_evidence_enabled",
    "generator_running[SOURCE_COMMISSIONING_MAX_GENERATORS]",
    "generator_breaker_closed[SOURCE_COMMISSIONING_MAX_GENERATORS]",
    "transfer_active",
    "grid_generator_synchronized",
):
    require(token in HEADER, f"native source DTO must contain {token}")

# Backend must round-trip every channel into the authoritative schema.
for token in (
    "solar.generators[i].running",
    "solar.generators[i].breaker_closed",
    "next.generators[i].running",
    "next.generators[i].breaker_closed",
    "next.transfer_active",
    "next.grid_generator_synchronized",
):
    require(token in BACKEND, f"backend must map {token}")
require("sync_generator0_compat(&next);" in BACKEND,
        "Generator 1 changes must keep schema-4 legacy compatibility mirror coherent before validation")
require(BACKEND.index("sync_generator0_compat(&next);") < BACKEND.index("solar_grid_config_valid(&next)"),
        "Generator 1 compatibility mirror must be synchronized before explicit Core validation")

# Safety boundary: save through Core validation/persistence only, with automatic
# control forced off first. Never manufacture source authority from kW sign.
require("solar_grid_config_valid(&next)" in BACKEND,
        "Core Solar-Grid validation must remain authoritative")
require("control_engine_force_disable();" in BACKEND and "app.control.enabled = false;" in BACKEND,
        "source commissioning mutations must force runtime and persistent control disabled")
require(BACKEND.index("control_engine_force_disable();") < BACKEND.index("solar_grid_config_save(&next)"),
        "control must be forced disabled before source evidence is persisted")
require("never infers contacts from power sign" in BACKEND,
        "backend must preserve the explicit no-power-sign-inference safety boundary")

# 800x480 workflow must make every channel reachable and preserve touch sizing.
require("#define SOURCE_SIGNAL_PAGE_COUNT 10U" in UI and "#define SOURCE_PAGE_COUNT 11U" in UI,
        "native workflow must expose ten signal sections plus enable/timing")
for label in (
    "Grid available", "Grid breaker",
    "Gen 1 running", "Gen 1 breaker",
    "Gen 2 running", "Gen 2 breaker",
    "Gen 3 running", "Gen 3 breaker",
    "Transfer active", "Grid + Gen sync", "Enable + timing",
):
    require(f'"{label}"' in UI, f"native UI must expose {label}")
require("Do not infer breaker state from kW sign" in UI,
        "native copy must explicitly forbid using power sign as source authority")
require("lv_obj_set_height(obj, 44);" in UI,
        "native source commissioning buttons must keep the 44 px touch floor")
require("Enable Generator 3 pair" in UI and "Enable Transfer/ATS evidence" in UI and
        "Enable Grid + Generator sync" in UI,
        "final enable section must expose all paired/optional evidence channels")

print("Waveshare complete source commissioning source contract passed")
