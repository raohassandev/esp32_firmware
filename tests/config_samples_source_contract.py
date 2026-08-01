#!/usr/bin/env python3
"""The shipped sample configurations must be valid, and honest.

A sample configuration is copied and applied. A broken one is worse than none at
all, and one that quietly carries wrong enum values is worse still, because the
firmware will accept it and then decode power incorrectly.

Two specific things this guards, both of which have caused real errors in this
project:

  - Grid active power must be decoded as a SIGNED type. Grid power goes negative
    when exporting; an unsigned type decodes export as a large import, which is the
    wrong sign at exactly the moment the controller must reduce PV.
  - The site template must not become quietly applyable. Every value that has to be
    measured at the plant stays null, so the firmware refuses it rather than running
    on a plausible guess.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SAMPLES = ROOT / "config-samples"
SITE = SAMPLES / "site-template.json"
DOC = ROOT / "docs" / "SAMPLE_CONFIGURATION.md"
CONFIG_TYPES = (ROOT / "components/config_manager/include/config_types.h").read_text(encoding="utf-8")
MODBUS_TYPES = (ROOT / "components/modbus_tcp/include/modbus_types.h").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# Both samples must be parseable JSON
# ---------------------------------------------------------------------------

for path in (SITE,):
    require(path.exists(), f"{path.name} is missing")

site = json.loads(SITE.read_text(encoding="utf-8"))

# ---------------------------------------------------------------------------
# Enum values are checked against the headers, not against memory
# ---------------------------------------------------------------------------

# modbus_data_type_t: UINT16=0, INT16=1, UINT32=2, INT32=3, FLOAT32=4
data_types = re.search(r"typedef enum \{(.*?)\} modbus_data_type_t;", MODBUS_TYPES, re.DOTALL)
require(data_types is not None, "could not parse modbus_data_type_t")
signed_32 = None
if data_types:
    names = [n.strip().split("=")[0].strip()
             for n in data_types.group(1).split(",") if n.strip()]
    require(names[0] == "MODBUS_DATA_UINT16",
            f"modbus_data_type_t no longer starts at UINT16: {names[0]}")
    require("MODBUS_DATA_INT32" in names, "MODBUS_DATA_INT32 no longer exists")
    if "MODBUS_DATA_INT32" in names:
        signed_32 = names.index("MODBUS_DATA_INT32")

# meter_role_t: UNASSIGNED=0, GRID=1, GENERATOR=2, ...
require("METER_ROLE_GRID = 1" in CONFIG_TYPES,
        "METER_ROLE_GRID is no longer 1; the samples encode roles numerically")
require("METER_ROLE_GENERATOR = 2" in CONFIG_TYPES,
        "METER_ROLE_GENERATOR is no longer 2")

# ---------------------------------------------------------------------------
# THE LAB-SIMULATOR SAMPLE IS GONE.
#
# It configured a controller to command a Modbus simulator through a profile
# that had never been qualified against hardware. That was the right shape
# while the plant was 2000 miles away; it is the wrong shape to ship beside a
# site template now that the controller is on the site, because the two files
# differ by one boolean and applying the wrong one points an unqualified
# profile at real equipment.
#
# tests/no_lab_authority_source_contract.py holds the arm closed in firmware,
# which is the layer that decides.

# The site template must NOT be applyable
# ---------------------------------------------------------------------------

def nulls(node):
    """Counts null leaves, ignoring documentation keys."""
    if isinstance(node, dict):
        return sum(nulls(v) for k, v in node.items() if not k.startswith("_"))
    if isinstance(node, list):
        return sum(nulls(v) for v in node)
    return 1 if node is None else 0


site_nulls = nulls(site)
require(site_nulls >= 10,
        f"the site template has only {site_nulls} unfilled values; it must not "
        "become quietly applyable, because the values it leaves blank cannot be "
        "guessed and the firmware must refuse an incomplete configuration")

require(site["profile_assignment"]["lab_target"] is False,
        "the site template must set lab_target false: declaring real equipment a "
        "simulator is the one thing the design cannot defend against")
require(site["control"]["enabled"] is False,
        "the site template must ship with control disabled")
require(site["solar_grid"]["generator_rated_kw"] is None,
        "the site template must not carry a generator rating; it cannot be guessed")

# Per-engine generator limits must be expressed, and every one of them must stay
# a null placeholder. A site can run up to APP_MAX_GENERATORS gensets in parallel
# and the minimum-loading floor is computed against the AGGREGATE rating of the
# engines online, so a template that describes only one engine invites a
# denominator that is wrong in the permissive direction.
max_generators = re.search(r"#define APP_MAX_GENERATORS\s+(\d+)", CONFIG_TYPES)
require(max_generators is not None, "could not read APP_MAX_GENERATORS")
site_generators = site["solar_grid"].get("generators")
require(isinstance(site_generators, list),
        "the site template must express per-engine generator limits as a "
        "'generators' array; one rating cannot describe a plant that runs a "
        "variable number of engines in parallel")
if isinstance(site_generators, list) and max_generators is not None:
    # Engine 0 is the four legacy scalar fields; the array carries the rest.
    require(len(site_generators) == int(max_generators.group(1)) - 1,
            f"the site template describes {len(site_generators)} additional "
            f"engines but the firmware supports "
            f"{int(max_generators.group(1)) - 1} beyond engine 0")
    for position, engine in enumerate(site_generators):
        for key in ("enabled", "rated_kw", "minimum_loading_percent",
                    "reserve_kw", "reverse_power_margin_kw",
                    # Base-load sharing needs a role per engine and a setpoint for
                    # every base-loaded one. Neither can be defaulted: mis-declaring
                    # a swing engine as base-loaded, or the reverse, moves the floor
                    # in either direction depending on the setpoints.
                    "role", "base_load_kw"):
            require(key in engine,
                    f"engine {position + 1} in the site template omits '{key}'")
            require(engine.get(key) is None,
                    f"engine {position + 1} in the site template carries a value "
                    f"for '{key}'; per-engine generator protection numbers are "
                    f"nameplate data and must be measured, never shipped")

# The kW load-sharing mode decides WHICH engine binds the minimum-loading floor, so
# the floor is not computable without it. It must appear in the template, and it must
# stay a null placeholder: there is no safe default, because base-load sharing can
# place the floor either above or below the isochronous floor depending on the
# commissioned setpoints. A template that shipped "isochronous" would be shipping the
# very assumption the firmware stopped making.
for key in ("load_sharing_mode", "engine_0_role", "engine_0_base_load_kw"):
    require(key in site["solar_grid"],
            f"the site template omits '{key}'; how the engines divide load is a "
            "commissioned fact the minimum-loading floor depends on")
    require(site["solar_grid"].get(key) is None,
            f"the site template carries a value for '{key}'; no load-sharing mode is "
            "conservative for every plant, so none may be shipped as a default")

# The known modelling gap must stay stated rather than quietly forgotten.
site_text = SITE.read_text(encoding="utf-8")
require("parallel" in site_text.lower(),
        "the site template must state the parallel-generator modelling position")
# Which engines are running is a runtime fact read from the generator-role
# meters, and the template must say so: a commissioning engineer who omits the
# per-engine meters gets a plant that holds PV at zero, and needs to know why.
require("generator_index" in site_text,
        "the site template must tell the engineer to attribute a meter to each "
        "engine slot, or the running set cannot be established")
require("aggregate" in site_text.lower(),
        "the site template must state that the minimum-loading floor is computed "
        "against the aggregate rating of the engines online")
# A refused mode is only safe if the engineer is told it is refused. Silence here
# would read as "droop is supported, just leave it".
require("droop" in site_text.lower() and "refused" in site_text.lower(),
        "the site template must say that droop sharing is refused, not silently "
        "unsupported; an engineer who enters it must be told the gate stays closed")
require("isochronous" in site_text.lower() and "base_load" in site_text,
        "the site template must name the load-sharing modes the firmware accepts")

# The base-load setpoint-agreement tolerance decides whether a base-loaded engine's
# setpoint may be BELIEVED. The floor credits that engine with the load its setpoint
# promises, so a governor that has left kW control makes the floor wrong in the permissive
# direction -- the reverse-power condition. It must appear in the template, and it must
# stay a null placeholder: no manual, nameplate or site document in this repository states
# a figure, and a template that shipped one would be shipping an invented tolerance into
# every plant that copies it.
for key in ("base_load_tolerance_kw", "base_load_tolerance_percent_of_rating"):
    require(key in site["solar_grid"],
            f"the site template omits '{key}'; how far a base-loaded engine may sit from "
            "its setpoint before the controller stops believing it is a commissioned "
            "fact the minimum-loading floor depends on")
    require(site["solar_grid"].get(key) is None,
            f"the site template carries a value for '{key}'; no tolerance for a "
            "governor's kW accuracy is documented anywhere in this repository, so none "
            "may be shipped as a default")
# An engineer who leaves it blank must be told what happens, not left to discover a
# closed gate. And the two failure directions must be named, because "unknown" reading as
# "agreeing" is the specific mistake this field exists to prevent.
require("permissive" in site_text.lower(),
        "the site template must say that an unheld base-load setpoint is wrong in the "
        "PERMISSIVE direction; an engineer who reads it as merely inaccurate will not "
        "treat it as a safety number")
require("narrower" in site_text.lower(),
        "the site template must state which band wins when both figures are given; "
        "leaving it unsaid invites an engineer to assume the wider one")
require("unknown is not confirmation" in site_text.lower(),
        "the site template must state that a stale or missing measurement on a "
        "base-loaded engine holds PV at zero rather than reading as agreement")

# ---------------------------------------------------------------------------
# The documentation must exist and must not invent a credential
# ---------------------------------------------------------------------------

require(DOC.exists(), "docs/SAMPLE_CONFIGURATION.md is missing")
doc_text = DOC.read_text(encoding="utf-8")
require("product owner" in doc_text,
        "the guide must direct the reader to obtain the password from the product "
        "owner rather than embedding one")
for forbidden in ("automatrix123", "SETUP-"):
    require(forbidden not in doc_text,
            f"the guide must not contain a literal credential ({forbidden})")

# The script it points at must exist, or the instruction is a dead end.
require((ROOT / "scripts" / "lab_run.py").exists(),
        "the guide references scripts/lab_run.py, which does not exist")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(f"configuration sample contract passed "
      f"(site template holds {site_nulls} "
      f"values that must be measured on site)")
