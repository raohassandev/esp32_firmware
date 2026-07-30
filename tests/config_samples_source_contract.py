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
LAB = SAMPLES / "lab-simulator.json"
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

for path in (LAB, SITE):
    require(path.exists(), f"{path.name} is missing")

lab = json.loads(LAB.read_text(encoding="utf-8"))
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
# The lab sample must be complete and internally consistent
# ---------------------------------------------------------------------------

lab_meters = lab["meters"]["meters"]
require(len(lab_meters) >= 1, "the lab sample defines no meter")
grid = next((m for m in lab_meters if m.get("role") == 1), None)
require(grid is not None, "the lab sample has no meter in the grid role")

if grid is not None and signed_32 is not None:
    require(grid["data_type"] == signed_32,
            f"the lab grid meter decodes active power as type {grid['data_type']}, "
            f"but a SIGNED 32-bit type ({signed_32}) is required: grid power is "
            "negative when exporting, and an unsigned type reads export as import")
    # No null may remain in the lab sample: it is meant to be applyable as-is.
    for key, value in grid.items():
        if key.startswith("_"):
            continue
        require(value is not None,
                f"the lab sample leaves '{key}' null, but it is meant to apply as-is")

lab_inverter = lab["inverters"]["inverters"][0]
assignment = lab["profile_assignment"]
require(assignment.get("lab_target") is True,
        "the lab sample must declare lab_target true; without it the controller "
        "refuses to command a profile that has not passed physical qualification")
require(assignment.get("profile_id", "").startswith("soltrix.sim."),
        "the lab sample must use a simulator profile")

# The simulator profile it names must actually exist in the catalogue.
profiles_c = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
require(f'"{assignment["profile_id"]}"' in profiles_c,
        f"the lab sample names profile {assignment['profile_id']}, which is not in "
        "the catalogue")

# Meter and inverter must be on the same simulator host, or the plant model in the
# simulator cannot close the loop between them.
require(grid["host"] == lab_inverter["host"],
        "the lab meter and inverter must be on the same simulator host, or the "
        "simulator's plant model cannot close the loop between them")

# Control must ship disabled: a sample that enables control on import would start
# commanding as soon as it is applied.
require(lab["control"]["enabled"] is False,
        "the lab sample must ship with control disabled")

# ---------------------------------------------------------------------------
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
                    "reserve_kw", "reverse_power_margin_kw"):
            require(key in engine,
                    f"engine {position + 1} in the site template omits '{key}'")
            require(engine.get(key) is None,
                    f"engine {position + 1} in the site template carries a value "
                    f"for '{key}'; per-engine generator protection numbers are "
                    f"nameplate data and must be measured, never shipped")

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
      f"(lab sample complete and applyable, site template holds {site_nulls} "
      f"values that must be measured on site)")
