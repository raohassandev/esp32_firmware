"""The fleet is spread across its poll period, not bunched on one tick.

Every inverter used to start with the same next_poll_ms, so all of them fell due
on the same tick and stayed in step for ever. A plant with twelve inverters
produced a twelve-transaction burst once a second and nothing in between.

On separate gateways that only wasted idle time. On ONE gateway -- which is how
these plants are actually wired, and now that transactions to a gateway are
serialised so they cannot corrupt each other -- the burst becomes a QUEUE, and
the grid meter is in it. The one measurement the control loop depends on ends up
waiting behind eleven inverter reads, for no reason beyond their having chosen
the same instant.

Measured on the bench, by reading the same meter at four different block sizes:

    2 registers   35 ms      40 registers  111 ms
    10 registers  47 ms      72 registers  183 ms

which is 31 ms of fixed cost plus 2.1 ms per register. So a meter block is
183 ms and an inverter's power read is 35 ms. Twelve inverters bunched is a
420 ms burst; spread, it is 35 ms every 83 ms, and the meter never waits behind
more than one of them.

This is invisible in every test that runs one inverter, which is every test this
project has. It shows up only as unexplained meter staleness on a full plant --
so it is pinned here rather than left to be rediscovered on a site.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANAGER = ROOT / 'components' / 'inverter_manager' / 'inverter_manager.c'

failures = []

source = MANAGER.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')

assign = re.search(r'runtime->next_poll_ms\s*=\s*now_ms\(\)([^;]*);', source)
if not assign:
    failures.append('the initial next_poll_ms assignment could not be located')
else:
    offset = assign.group(1)
    if not offset.strip():
        failures.append('every inverter is scheduled for the same instant again, so '
                        'the whole fleet falls due on one tick and the grid meter '
                        'queues behind all of them on a shared gateway')
    else:
        if 'i' not in offset:
            failures.append('the stagger does not vary per inverter, so the fleet is '
                            'still in step even if it is offset as a whole')
        # Derived from the poll period, not a constant: a fixed spacing either
        # overflows the period on a large fleet or leaves it bunched on a small
        # one, and both re-create the burst this exists to remove.
        if 'poll_period_ms' not in offset and 'profile_poll_ms' not in offset:
            failures.append('the stagger is not derived from the poll period, so it '
                            'does not spread the fleet ACROSS that period: too small '
                            'a spacing leaves the burst, too large pushes inverters '
                            'past their own poll interval')
        if 'fleet' not in offset and 's_inverter_count' not in offset:
            failures.append('the stagger is not divided by the number of inverters, '
                            'so the spread does not adapt to fleet size')

# The prerequisite check is another transaction to the same bus. If it is
# scheduled independently it re-creates the burst the poll stagger removes.
if not re.search(r'runtime->next_prerequisite_ms\s*=\s*runtime->next_poll_ms;', source):
    failures.append('the prerequisite check no longer inherits the staggered poll '
                    'time, so it becomes a second synchronised burst against the '
                    'same gateway')

# A zero fleet size must not divide.
guard = re.search(r'(?:const\s+)?uint32_t\s+fleet\s*=\s*([^;]+);', source)
if guard and '?' not in guard.group(1):
    failures.append('the fleet size is used without guarding against zero, which '
                    'divides by zero on a controller with no inverters configured')

if failures:
    print('Inverter poll stagger contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('Inverter poll stagger contract passed '
      '(the fleet is spread across its poll period by index, derived from the '
      'period and the fleet size; the prerequisite check rides the same offset)')
