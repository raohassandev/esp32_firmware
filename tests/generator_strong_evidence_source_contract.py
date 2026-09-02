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

require("SOLAR_GRID_CONFIG_VERSION 4u" in SG_H,
        "strong source evidence must survive the schema-4 generator expansion")
# Generator-1 legacy aliases remain a compatibility mirror while schema 4 also
# persists run/breaker evidence independently inside each generators[] channel.
for signal in SIGNALS:
    require(f"solar_grid_signal_config_t {signal};" in SG_H,
            f"persisted compatibility strong-evidence signal missing: {signal}")
    require(signal in CONTROL, f"control engine does not consume {signal}")
    require(signal in TYPES, f"runtime status does not expose {signal}")
    require(f'"{signal}"' in STATUS, f"status API does not report {signal}")

require('signal_to_json(root, "generator_running", &generator0->running)' in API,
        "GET config must expose Generator-1 run evidence through the compatibility field")
require('signal_to_json(root, "generator_breaker_closed", &generator0->breaker_closed)' in API,
        "GET config must expose Generator-1 breaker evidence through the compatibility field")
for signal in ("transfer_active", "grid_generator_synchronized"):
    require(f'signal_to_json(root, "{signal}"' in API,
            f"GET config does not expose {signal}")
    require(f'parse_optional_signal(root, "{signal}"' in API,
            f"POST config does not accept/preserve {signal}")
require('parse_optional_signal(root, "generator_running",' in API and
        '&next.generators[0].running' in API,
        "legacy Generator-1 run POST must update authoritative channel 0")
require('parse_optional_signal(root, "generator_breaker_closed",' in API and
        '&next.generators[0].breaker_closed' in API,
        "legacy Generator-1 breaker POST must update authoritative channel 0")
require('parse_optional_signal(object, "running"' in API and
        'parse_optional_signal(object, "breaker_closed"' in API,
        "schema-4 generator array must accept run/breaker evidence per channel")

# Schemas 2 and 3 are frozen. Schema 3's strong Generator-1 evidence migrates
# into channel 0; Generator 2/3 remain disabled safe defaults.
for legacy in ("legacy_solar_grid_config_v2_t", "legacy_solar_grid_config_v3_t"):
    require(legacy in SG_C, f"frozen migration layout missing: {legacy}")
require("offsetof(solar_grid_config_t, generators)" in SG_C and
        "sizeof(legacy_solar_grid_config_v3_t)" in SG_C,
        "schema-3 prefix identity must be compile-time checked")
require("migrate_v3" in SG_C and "legacy_to_generator0(loaded);" in SG_C,
        "schema-3 strong evidence must migrate into Generator 1")
require("generator_safe_defaults(&config->generators[i]);" in SG_C,
        "new generator channels must default with evidence disabled")
require("config->generator_running.enabled != config->generator_breaker_closed.enabled" in SG_C,
        "Generator-1 compatibility run and breaker contacts must remain a pair")
require("generator->running.enabled == generator->breaker_closed.enabled" in SG_C,
        "every schema-4 generator channel must commission run/breaker as a pair")

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

# Defaults establish transport shape only; actual addresses/polarity remain site
# commissioning data. generator_safe_defaults() zeroes each channel and then
# signal_safe_defaults() leaves enabled=false.
defaults = SG_C[SG_C.index("void solar_grid_config_defaults"):SG_C.index("bool solar_grid_config_evidence_complete")]
require("generator_safe_defaults(&config->generators[i]);" in defaults,
        "generator defaults must use the disabled safe channel template")
require(".enabled = true" not in defaults,
        "strong evidence must never be enabled by default")

print("Generator strong source evidence contract passed")
