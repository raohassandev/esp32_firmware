#!/usr/bin/env python3
"""No credential may be shared across units, or committed to this repository.

This exists because one was. `config_manager.c` `defaults()` hard-coded a
recovery access-point password, identical on every unit built from this tree, in a
PUBLIC repository. Anyone able to read the repository could join the setup network
of any controller in the field. The existing production release gate did not catch
it, because it only checks the build-time Wi-Fi provisioning symbols.

A default credential is not a default. It is a published one.

The recovery AP password is now empty by default and generated per device on first
start. This contract keeps it that way, and generalises to the other credential
fields so the next one cannot be introduced quietly.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# Nothing in defaults() may assign a password from a string literal
# ---------------------------------------------------------------------------

start = CONFIG.index("static void defaults(app_config_t *c)")
end = CONFIG.index("\n}", CONFIG.index("reconnect_backoff_ms", start))
defaults_body = CONFIG[start:end]

# Any strlcpy/snprintf into a *password* field must take its value from a Kconfig
# symbol or from a generated secret -- never from a literal in this file.
for match in re.finditer(r"(strlcpy|snprintf)\s*\(\s*c->wifi\.([A-Za-z_.]*password)[^;]*;",
                         defaults_body, re.DOTALL):
    statement = match.group(0)
    field = match.group(2)
    has_literal = re.search(r'"[^"]{1,}"', statement.replace('"%02X%02X%02X"', ""))
    from_kconfig = "CONFIG_PVDG" in statement
    require(from_kconfig or not has_literal,
            f"defaults() assigns {field} from a string literal. A credential shipped "
            "identically on every unit from a public repository is published, not "
            "default. Take it from a Kconfig symbol that defaults to empty, or "
            "generate it per device.")

# The specific field that was wrong must come from the Kconfig symbol whose default
# is empty.
require("CONFIG_PVDG_RECOVERY_AP_PASSWORD" in defaults_body,
        "the recovery AP password must come from CONFIG_PVDG_RECOVERY_AP_PASSWORD")

kconfig_block = KCONFIG[KCONFIG.index("config PVDG_RECOVERY_AP_PASSWORD"):]
kconfig_block = kconfig_block[:kconfig_block.index("\nconfig ")]
require(re.search(r'default\s*""', kconfig_block) is not None,
        "PVDG_RECOVERY_AP_PASSWORD must default to empty, so a build that does not "
        "deliberately set it ships no shared credential")

# ---------------------------------------------------------------------------
# An empty password must be replaced per device, not left open
# ---------------------------------------------------------------------------

require("ensure_recovery_ap_secret" in CONFIG,
        "an empty recovery AP password must be replaced by a per-device secret, not "
        "left blank, which would leave the setup network open")
require("esp_fill_random" in CONFIG,
        "the per-device recovery secret must be random; a value derived from the MAC "
        "or any other published input is computable from this public repository")
require("ensure_recovery_ap_secret(loaded)" in CONFIG,
        "the per-device secret must actually be applied during config_manager_init")

# ---------------------------------------------------------------------------
# A unit already carrying the retired shared password must rotate it
# ---------------------------------------------------------------------------
#
# Fixing the default alone only helps units that were never commissioned: a
# commissioned unit has the published password STORED, so it is not empty and would
# keep using it forever.
require("RETIRED_RECOVERY_AP_PASSWORD_FNV1A64" in CONFIG,
        "a commissioned unit carrying the retired shared password must recognise and "
        "rotate it; fixing the default only helps units never commissioned")
require("fnv1a64" in CONFIG,
        "the retired password check must compare a hash, so the retired credential is "
        "not reprinted in this source")

# The retired literal itself must not reappear anywhere in the tree.
RETIRED_FNV = 0x70D9019AAA1F69DD


def fnv1a64(text: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in text:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


scanned = 0
for path in list((ROOT / "components").rglob("*.c")) + list((ROOT / "components").rglob("*.h")) \
        + list((ROOT / "main").rglob("*.c")) + list((ROOT / "web").rglob("*.js")) \
        + list((ROOT / "config-samples").rglob("*.json")):
    if "managed_components" in str(path):
        continue
    scanned += 1
    text = path.read_text(encoding="utf-8", errors="ignore")
    for literal in re.findall(r'"([ -~]{6,64})"', text):
        if fnv1a64(literal.encode()) == RETIRED_FNV:
            failures.append(f"{path.relative_to(ROOT)} contains the retired shared "
                            "recovery-AP password as a literal")

require(scanned > 20, f"only {scanned} files scanned; the sweep is probably not working")

# ---------------------------------------------------------------------------
# The serialised configuration must never emit a password in clear
# ---------------------------------------------------------------------------

for field in ("fallback_ap_password", "password"):
    for match in re.finditer(
            r'cJSON_AddStringToObject\([^,]+,\s*"' + field + r'"\s*,\s*([^)]+)\)', CONFIG):
        require("MASKED_PASSWORD" in match.group(1),
                f"the configuration API emits {field} without masking it")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(f"shared-credential contract passed (no literal credential in defaults, "
      f"per-device secret generated and rotated, {scanned} files swept)")
