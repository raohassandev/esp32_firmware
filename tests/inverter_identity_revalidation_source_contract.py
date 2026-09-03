#!/usr/bin/env python3
"""Lock fail-closed inverter identity revalidation on reconnect/stale telemetry."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    "INVERTER_IDENTITY_RECHECK_MS 60000U",
    "static bool identity_is_current",
    "runtime->identity_checked && runtime->data.identity_verified",
    "runtime->last_identity_ms != 0U",
    "timestamp - runtime->last_identity_ms <= INVERTER_IDENTITY_RECHECK_MS",
    "static void invalidate_identity",
    "runtime->identity_checked = false",
    "runtime->last_identity_ms = 0U",
    "runtime->data.identity_verified = false",
):
    require(token in MANAGER, f"identity freshness contract missing: {token}")

# A previously verified identity must not remain command-eligible indefinitely.
require("identity_is_current(runtime, timestamp)" in MANAGER,
        "commandable capacity must require a current identity")
require("if (!identity_is_current(runtime, timestamp) && verify_identity(runtime) != ESP_OK)" in MANAGER,
        "background acquisition must re-probe identity after expiry/invalidation")
require("runtime->data.online = false" in MANAGER and
        "runtime->data.telemetry_valid = false" in MANAGER,
        "failed identity verification must leave the inverter unavailable")

# Any failed telemetry exchange may imply a broken/recreated TCP session or a
# different device behind the endpoint. The old identity proof must therefore
# be discarded before the inverter can become write-eligible again.
poll_start = MANAGER.index("static esp_err_t poll_active_power")
poll_end = MANAGER.index("static esp_err_t poll_readback", poll_start)
poll_body = MANAGER[poll_start:poll_end]
for token in (
    "runtime->data.online = false",
    "runtime->data.telemetry_valid = false",
    "runtime->data.telemetry_stale = true",
    "runtime->identity_checked = false",
    "runtime->last_identity_ms = 0U",
    "runtime->data.identity_verified = false",
):
    require(token in poll_body, f"telemetry failure does not invalidate identity: {token}")

# A sample can age out without a fresh I/O error. That path must revoke the same
# proof so a later recovered session cannot inherit stale device identity.
stale_start = MANAGER.index("static void update_stale_state")
stale_end = MANAGER.index("static void inverter_telemetry_task", stale_start)
stale_body = MANAGER[stale_start:stale_end]
for token in (
    "age > profile_stale_ms(runtime->profile)",
    "runtime->data.online = false",
    "runtime->identity_checked = false",
    "runtime->last_identity_ms = 0U",
    "runtime->data.identity_verified = false",
):
    require(token in stale_body, f"stale telemetry does not revoke identity: {token}")

# Identity-probe failures themselves may never be converted into a successful
# checked state. Only a matching response stamps a freshness timestamp.
verify_start = MANAGER.index("static esp_err_t verify_identity")
verify_end = MANAGER.index("static esp_err_t poll_active_power", verify_start)
verify_body = MANAGER[verify_start:verify_end]
require("bool matched = err == ESP_OK && identity_matches" in verify_body,
        "identity success is not tied to a matching successful read")
require("runtime->identity_checked = matched" in verify_body and
        "runtime->last_identity_ms = matched ? timestamp : 0U" in verify_body,
        "failed probes can retain a current identity timestamp")
require("runtime->data.identity_verified = matched" in verify_body,
        "runtime identity result is not fail-closed")

print("Inverter identity revalidation source contract passed")
