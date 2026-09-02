#!/usr/bin/env python3
"""Strong generator/transfer evidence must be configurable, acquired and fail-closed."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
SG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
STATUS = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
TYPES = (ROOT / "components/control_engine/include/control_types.h").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


SIGNALS = (
    "generator_running",
    "generator_breaker_closed",
    "transfer_active",
    "grid_generator_synchronized",
)

require("SOLAR_GRID_CONFIG_VERSION 3u" in SG_H,
        "strong source evidence must live in schema 3")
for signal in SIGNALS:
    require(f"solar_grid_signal_config_t {signal};" in SG_H,
            f"persisted strong evidence signal missing: {signal}")
    require(f'signal_to_json(root, "{signal}"' in API,
            f"GET config does not expose {signal}")
    require(f'parse_optional_signal(root, "{signal}"' in API,
            f"POST config does not accept/preserve {signal}")
    require(signal in CONTROL, f"control engine does not consume {signal}")
    require(signal in TYPES, f"runtime status does not expose {signal}")
    require(f'"{signal}"' in STATUS, f"status API does not report {signal}")

require("legacy_solar_grid_config_v2_t" in SG_C,
        "schema 2 layout must be frozen for migration")
require("offsetof(solar_grid_config_t, generator_running)" in SG_C,
        "schema 2 prefix identity must be compile-time checked")
for signal in SIGNALS:
    require(f"signal_safe_defaults(&loaded->{signal})" in SG_C,
            f"migration must disable {signal} rather than guess an address")
require("config->generator_running.enabled != config->generator_breaker_closed.enabled" in SG_C,
        "generator run and breaker contacts must commission as a pair")

require("read_optional_signal" in CONTROL,
        "strong contacts must be acquired through the evidence task")
require("if (!signal->enabled) return ESP_OK;" in CONTROL,
        "disabled optional contacts must consume no Modbus request")
require("next.last_update_ms = timestamp" in CONTROL and "if (error == ESP_OK)" in CONTROL,
        "snapshot freshness must advance only when every enabled evidence read succeeds")
require("source_mode_evaluate(&source_evidence)" in CONTROL,
        "strong evidence must drive the tested source-state engine")
for token in (
    ".transfer_active = evidence.transfer_active",
    ".generator_running = evidence.generator_configured &&",
    ".generator_breaker_closed = evidence.generator_configured &&",
    ".grid_generator_synchronized = evidence.grid_generator_synchronized",
):
    require(token in CONTROL, f"source-state evidence mapping missing: {token}")
require("APP_MODE_GENERATOR" in CONTROL,
        "runtime mode must identify generator-carried operation")
require("SOURCE_MODE_ISLAND" in CONTROL,
        "island generator operation must reach the generator safety limit")

# No default register or contact may be enabled. Safe defaults establish only
# transport shape (FC03/mask/active polarity) while leaving enabled=false and
# address=0 from memset; site commissioning supplies the actual mapping.
defaults = SG_C[SG_C.index("void solar_grid_config_defaults"):SG_C.index("bool solar_grid_config_evidence_complete")]
require("signal_safe_defaults(&config->generator_running)" in defaults,
        "generator defaults must use the disabled safe template")
require(".enabled = true" not in defaults,
        "strong evidence must never be enabled by default")

print("Generator strong source evidence contract passed")
