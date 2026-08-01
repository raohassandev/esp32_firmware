#!/usr/bin/env python3
"""There is no second route to write authority. The controller is on a site.

WHAT THIS REPLACES. Two contracts guarded a lab-simulator arm: one that a
declaration granted a narrower LAB authority without ever becoming production
authority, and one that the simulator profiles matched the simulator tool. Both
described a world where the plant's inverters were 2000 miles away and the thing
on the wire was a Modbus simulator.

The owner is now standing at the plant. Everything on the wire is real
equipment, and a declaration cannot make a real inverter a simulator. Keeping an
arm that grants command authority on the strength of a flag would mean the one
failure the whole gate exists to prevent -- an unqualified profile commanding
real hardware -- is reachable by setting a boolean.

So the arm is closed, and this contract holds it closed:

  1. inverter_profile_write_permission() takes no lab declaration at all. The
     parameter is gone, not ignored.
  2. INVERTER_WRITE_LAB_ONLY does not exist. The enum has two values.
  3. The catalogue carries no simulator-only profile.

The executed half is tests/inverter_write_permission_test.c, which runs the
permission function over the shipped catalogue and over constructed profiles at
every qualification level.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


body = re.search(r'inverter_write_permission_t inverter_profile_write_permission\('
                 r'.*?\n\}', PROFILES, re.S)
if not body:
    failures.append('the write-permission function could not be located')
else:
    text = body.group(0)
    require('INVERTER_WRITE_LAB_ONLY' not in text,
            'the permission function can still return LAB authority. On a live '
            'site that is a path from a boolean to a command on real equipment')
    require('declared_lab_target' not in text,
            'the lab declaration is back in the write gate. It was removed from '
            'the signature entirely: there is no lab target on a live site, so '
            'there is no parameter for one')
    require('INVERTER_WRITE_PRODUCTION' in text,
            'the production route is gone; the controller could then never '
            'command anything even after a profile is qualified')

require('.simulator_only = true' not in PROFILES,
        'a simulator-only profile is back in the catalogue. Pointed at real '
        'equipment it returns numbers that mean nothing while looking exactly '
        'like measurements')

if failures:
    print('No-lab-authority contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('No-lab-authority contract passed (a lab declaration grants nothing, '
      'LAB_ONLY is unreachable, and the catalogue carries no simulator profile)')
