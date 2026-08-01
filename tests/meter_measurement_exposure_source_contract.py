#!/usr/bin/env python3
"""The meter's own measurements reach BOTH views, through ONE serializer.

Two things this pins, and both were real risks rather than hypotheticals.

FIRST: the operator view must carry them. `/api/meters` answers twice -- a full
engineering payload, and a reduced projection an unauthenticated browser gets.
The gate exists to withhold how the firmware TALKS to the meter: hosts, unit
ids, register addresses, error codes. It does not exist to withhold what the
meter SAYS. Voltage, current, power factor, frequency and the energy counters
are printed on the instrument's own front panel, and they are the evidence a
plant owner uses to satisfy themselves the controller is working. A projection
that dropped them would hide the proof from exactly the reader it was built for
while protecting nothing.

SECOND: one serializer, not two. Hand-written copies of "volts, per phase, null
when absent" drift. The first symptom of that drift is an operator and an
engineer standing at the same panel, reading the same instrument, and seeing
different numbers -- which destroys the trust the page exists to create.

Comments are stripped before matching, so prose describing a call can never
satisfy a contract about the code.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHARED = ROOT / "components" / "web_server" / "meter_json.c"
ENGINEERING_VIEW = ROOT / "components" / "web_server" / "device_api.c"
OPERATOR_VIEW = ROOT / "components" / "web_server" / "engineering_guard.c"

# Every quantity the EM500 block decodes. If a field is acquired but never
# published, no screen can show it and the acquisition was wasted.
PUBLISHED_FIELDS = [
    "phase_voltage_v",
    "line_voltage_v",
    "current_a",
    "active_power_kw",
    "reactive_power_kvar",
    "apparent_power_kva",
    "power_factor",
    "frequency_hz",
    "equivalent_phase_voltage_v",
    "equivalent_line_voltage_v",
    "equivalent_current_a",
    "total_active_power_kw",
    "total_reactive_power_kvar",
    "total_apparent_power_kva",
    "total_power_factor",
    "voltage_asymmetry_line_percent",
    "voltage_asymmetry_phase_percent",
    "current_asymmetry_percent",
    "neutral_current_a",
]

ENERGY_FIELDS = [
    "total_import_active_kwh",
    "total_export_active_kwh",
    "total_import_reactive_kvarh",
    "total_export_reactive_kvarh",
    "total_apparent_kvah",
    "partial_import_active_kwh",
    "partial_export_active_kwh",
    "partial_import_reactive_kvarh",
    "partial_export_reactive_kvarh",
    "partial_apparent_kvah",
]

SERIALIZERS = (
    "meter_json_add_measurements",
    "meter_json_add_energy",
    "meter_json_add_phase_power",
)


def stripped(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def main():
    failures = []

    shared = stripped(SHARED)
    engineering = stripped(ENGINEERING_VIEW)
    operator = stripped(OPERATOR_VIEW)

    # The struct's fields all reach JSON.
    for field in PUBLISHED_FIELDS + ENERGY_FIELDS:
        if f'"{field}"' not in shared:
            failures.append(
                f'{field} is decoded from the meter but never published as JSON. '
                f'A value acquired and not exposed cannot appear on any screen.')

    # Both views call the shared serializer.
    for name, source, path in (("engineering", engineering, ENGINEERING_VIEW),
                               ("operator", operator, OPERATOR_VIEW)):
        for serializer in SERIALIZERS:
            if serializer not in source:
                failures.append(
                    f'the {name} view ({path.name}) does not call {serializer}. '
                    f'Meter measurements are what the instrument reports, not how '
                    f'the firmware reaches it, and both views must publish them.')

    # And neither view has grown its own copy. A second definition of the same
    # shape is how the two drift apart.
    for name, source, path in (("engineering", engineering, ENGINEERING_VIEW),
                               ("operator", operator, OPERATOR_VIEW)):
        for field in ("phase_voltage_v", "total_import_active_kwh", "power_factor"):
            if f'"{field}"' in source:
                failures.append(
                    f'{path.name} names "{field}" directly, so the {name} view has a '
                    f'second copy of the measurement serializer. Two copies drift, and '
                    f'the first symptom is an operator and an engineer reading '
                    f'different numbers off the same meter.')

    # A measurement that is absent must serialize as null, never as zero: on a
    # power screen "0.0 V" and "not measured" are different plants.
    if "cJSON_AddNullToObject" not in shared:
        failures.append(
            "meter_json.c never emits null. An unmeasured quantity sent as 0.0 "
            "claims the instrument measured zero, which is a different fault "
            "from not having read it -- and sends an electrician to the wrong "
            "place.")

    # The blocks carry their own age. They poll on slower cadences than the
    # control measurement, so one age shown for all of them is a lie about at
    # least one.
    if shared.count('"age_ms"') < 2:
        failures.append(
            "the measurement and energy blocks do not each carry their own "
            "age_ms. They are polled on different cadences from the control "
            "measurement and from each other; a single age would misreport at "
            "least one of them as fresh.")

    if failures:
        print("Meter measurement exposure contract FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print(
        f"Meter measurement exposure contract passed "
        f"({len(PUBLISHED_FIELDS)} instantaneous and {len(ENERGY_FIELDS)} energy "
        f"fields published through one serializer, reaching both the operator and "
        f"the engineering view)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
