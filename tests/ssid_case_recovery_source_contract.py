#!/usr/bin/env python3
"""A saved SSID that differs from the air only in case must still connect.

SSIDs are case sensitive and the supplicant matches them exactly, so "inverter"
typed for a network broadcast as "Inverter" fails with reason 201, NO_AP_FOUND --
the same code the driver returns for a network that is not there at all. The
controller cannot tell the two apart and neither can the person reading the log.

That single wrong keystroke is a lockout, not an inconvenience. On 2026-08-15 a
commissioned controller sat in a retry loop it could never leave. The recovery
access point -- the documented way back into a unit that cannot join -- was on
air and answering, and still could not be joined from the only laptop present.
There was no route to the controller of any kind except a serial cable and a
reflash. On a customer site there would have been no route at all.

So the scan is now matched case-insensitively as a fallback, and the spelling on
the air wins. Three properties matter and each is asserted below:

  - the EXACT match is tried first, so a network really named "inverter" is
    never passed over in favour of a different "Inverter" nearby;
  - the correction is in RAM only, never written back to the stored
    configuration, because it is a correction for this radio environment and not
    an edit of what the operator typed;
  - it is logged plainly, because a controller that silently joined a network
    nobody configured would be worse than the failure being fixed.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/network_manager/network_manager.c").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


start = SOURCE.index("static esp_err_t scan_configured_networks")
end = SOURCE.index("\n}", SOURCE.index("Scan found %u APs", start))
scan = SOURCE[start:end]

require("strcasecmp(ssid, s_cfg.primary.ssid) == 0" in scan,
        "the primary SSID must be matched case-insensitively as a fallback, or a "
        "one-character case error locks the controller out permanently")
require("strcasecmp(ssid, s_cfg.fallback.ssid) == 0" in scan,
        "the fallback SSID needs the same treatment: it is the profile that runs "
        "when the primary is already failing")

require("#include <strings.h>" in SOURCE,
        "strcasecmp needs <strings.h>; without it the build takes an implicit "
        "declaration and the comparison is not what it appears to be")

# Exact first. The case-insensitive pass must be guarded on the exact pass having
# failed, or a controller with two similarly-named networks in range could be
# steered onto the wrong one.
exact_at = scan.index("strcmp(ssid, s_cfg.primary.ssid) == 0")
loose_at = scan.index("strcasecmp(ssid, s_cfg.primary.ssid) == 0")
require(exact_at < loose_at,
        "the exact match must be attempted before the case-insensitive one")
require("!*primary_found &&" in scan and "!*fallback_found &&" in scan,
        "the case-insensitive match must only run when the exact match already "
        "failed, or a network genuinely named in lower case could be passed over "
        "for a different one nearby")

require(re.search(r"strlcpy\(s_cfg\.primary\.ssid, ssid,", scan) is not None,
        "the spelling on the air must be adopted for the connect attempt; "
        "matching the scan but then connecting with the stored spelling would "
        "change the log message and nothing else")

require("config_manager_save" not in scan and "nvs" not in scan.lower(),
        "the correction must stay in RAM: it belongs to this radio environment, "
        "and silently rewriting the operator's stored configuration from inside "
        "a scan is a different and much larger decision")

require(scan.count("ESP_LOGW(TAG, \"Saved") >= 2 or scan.count("differs from the air only in case") >= 2,
        "both corrections must be logged plainly -- a controller that silently "
        "joined a network the operator did not type would be worse than the "
        "failure this fixes")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    raise SystemExit(f"{len(failures)} SSID case-recovery contract failure(s)")

print("SSID case-recovery contract passed (exact match first, case-insensitive "
      "fallback adopts the broadcast spelling, in RAM only, and says so)")
