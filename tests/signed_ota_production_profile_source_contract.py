#!/usr/bin/env python3
"""Source contract for the production signed-OTA software profile.

This verifies configuration, release tooling and runtime policy visibility only.
It deliberately does not convert CI into physical OTA/rollback qualification.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = (ROOT / "sdkconfig.signed-ota.defaults").read_text(encoding="utf-8")
OTA_API = (ROOT / "components/web_server/ota_api.c").read_text(encoding="utf-8")
SIGN_TOOL = (ROOT / "tools/sign_ota_release.py").read_text(encoding="utf-8")
DOC = (ROOT / "docs/SIGNED_OTA_RELEASE.md").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Network-delivered production OTA images must be cryptographically signed,
# without silently turning on irreversible hardware Secure Boot/eFuse behavior.
for token in (
    "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y",
    "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y",
    "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y",
    "# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set",
    "# CONFIG_SECURE_BOOT is not set",
):
    require(token in PROFILE, f"signed OTA production profile missing: {token}")

require("CONFIG_SECURE_BOOT=y" not in PROFILE,
        "production signed-OTA profile must not enable irreversible hardware Secure Boot")
require("SIGNING_KEY" not in PROFILE and ".pem" not in PROFILE,
        "signed OTA profile must not name or embed a private key")

# Runtime status must expose policy rather than letting the browser infer it.
for token in (
    '"signed_app_required"',
    '"signed_ota_verification"',
    '"hardware_secure_boot"',
    '"app_signature_scheme"',
    "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT",
    "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT",
    "signed_ota_verification_enabled()",
):
    require(token in OTA_API, f"OTA runtime policy visibility missing: {token}")

# Release helper must use official ESP-IDF signing and verification, keep the
# private key outside the repository, and retain the physical-evidence boundary.
for token in (
    '"secure-sign-data"',
    '"secure-verify-signature"',
    "key_is_inside_repo",
    "Refusing to use a private signing key stored inside the repository tree",
    "OTA_SLOT_BYTES = 0x300000",
    '"private_key_embedded": False',
    '"physical_interruption_test_claimed": False',
    '"hardware_secure_boot_claimed": False',
    '"requires_initial_signed_app": True',
):
    require(token in SIGN_TOOL, f"signed OTA release tool contract missing: {token}")

for forbidden in (
    "BEGIN PRIVATE KEY",
    "BEGIN RSA PRIVATE KEY",
    "PRIVATE_SIGNING_KEY =",
):
    require(forbidden not in PROFILE + SIGN_TOOL + DOC,
            f"private key material/example assignment must not be committed: {forbidden}")

for token in (
    "initial production application",
    "0x300000-byte OTA slot",
    "Hardware Secure Boot",
    "physical OTA interruption / rollback qualification",
    "private RSA-3072 key is never required by routine GitHub CI",
):
    require(token in DOC, f"signed OTA release documentation missing boundary: {token}")

print("Signed OTA production profile source contract passed")
