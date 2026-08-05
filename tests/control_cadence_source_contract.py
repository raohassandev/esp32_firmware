"""One measurement, one decision, one command.

The control loop ticks every interval_ms -- 20 ms by default. The meter produces
a new reading about once a second. Nothing asked whether a reading was NEW, only
whether it was younger than the stale timeout, so the same sample was decided on
up to fifty times and the integral accumulated the same error fifty times over.
It wound to one rail, the next reading showed a large error the other way, and it
wound to the other.

Measured on the plant while this was live: PV commanded 0 % to 100 % and back
several times a second, grid power swinging +50 to -50 kW, and setpoint writes
reaching a 100 kW inverter every 5 to 224 ms. No inverter is built to be
commanded at that rate.

What proved it was the controller and not the plant: with automatic control
switched off, the same meter read a steady 50.0 kW at 231.2 V and 77.4 A,
50.02 Hz, six samples running -- and 231.2 x 77.4 x 3 is 53.7 kVA, which at the
measured power factor is the 50 kW it reported. The instrument was telling the
truth about a plant the controller was shaking.

WHAT THIS HOLDS.

  1. The policy runs on a reading the loop has not acted on before, and holds its
     previous output otherwise. A held decision is what keeps the setpoint steady
     between samples; recomputing from zero would command zero.

  2. Switching control off still clears the held decision. A decision must not
     outlive the authority to make it.

  3. The interval handed to the integral and the ramps is the measured gap
     between the samples acted on -- not the configured tick, which is fifty
     times shorter and would make the integral accumulate fifty times too little
     and the per-second ramp limits fifty times too tight.

  4. Freshness is still enforced separately. "New" and "fresh" are different
     questions: a sample can be new and already too old to control on, and the
     fail-closed path depends on the second question still being asked.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'components' / 'control_engine' / 'control_engine.c'

failures = []

source = ENGINE.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')

# --------------------------------------------- 1. the decision follows the data
gate = re.search(r'const bool new_measurement\s*=(.*?);', source, re.S)
if not gate:
    failures.append('nothing establishes whether the meter reading is one the '
                    'loop has already acted on, so the policy runs again on every '
                    'tick and the integral accumulates the same error up to fifty '
                    'times per reading')
else:
    expression = gate.group(1)
    if 'last_update_ms' not in expression:
        failures.append('the new-measurement test does not compare the sample '
                        'timestamp, so it cannot tell a fresh poll from a '
                        'repeated one')
    if 'processed_sample_ms' not in expression:
        failures.append('the new-measurement test does not compare against the '
                        'sample last acted on')

step = re.search(r'if \(new_measurement\) \{\s*\n\s*policy = power_control_step', source)
if not step:
    failures.append('power_control_step is no longer gated on a new measurement, '
                    'which restores the oscillation')

# The policy is computed in BOTH states now. It used to run only while automatic
# control was enabled, so with control off the controller had no answer to "what
# would you do" -- every screen read 0 % on a plant it had a perfectly good answer
# for, and an engineer had to arm it to find out what arming it would command.
#
# The integral is the part that may NOT run while disabled: the loop is not
# acting, so its error is never corrected, and an integrator left running would
# wind up for as long as the plant sat idle and dump that history into the first
# command after arming.
if not re.search(r'if \(!control_enabled\) \{\s*\n(?:\s*/\*.*?\*/\s*\n)?\s*'
                 r'integral_kw = 0\.0f;', source, re.S):
    failures.append('the integral is not cleared while control is disabled, so it '
                    'winds up unopposed for as long as the plant is idle and lands '
                    'in the first command after arming')

# The held decision is what keeps the setpoint steady between readings. If policy
# is declared inside the loop again it resets every tick, and a hold cycle
# commands zero -- which on this product means dropping the whole PV plant.
#
# Scoped to control_task. An earlier `while (true)` in another task made a
# whole-file search report the declaration as being inside the loop when it was
# not -- the contract failed against correct source, which is the one outcome a
# contract may never have.
task = re.search(r'static void control_task\(.*', source, re.S)
if not task:
    failures.append('control_task could not be located')
else:
    body = task.group(0)
    declaration = body.find('power_control_output_t policy = {0};')
    loop = body.find('while (true) {')
    if declaration < 0:
        failures.append('the held decision is no longer declared; without it there '
                        'is nothing to repeat on a cycle that has no new reading')
    elif loop >= 0 and declaration > loop:
        failures.append('policy is declared inside the loop, so it resets on every '
                        'tick. A cycle with no new reading then has no decision to '
                        'hold and commands zero, dropping the PV plant between '
                        'measurements')

# ------------------------- 2. a held decision must not outlive its measurement
#
# This used to require clearing when control was DISABLED. That caught one way of
# going stale and missed the other: a meter that stops answering leaves the loop
# with nothing new to step on, and the last decision stands indefinitely while
# being reported as current. Freshness is the honest test and covers both, which
# matters more now that the policy is computed -- and shown -- while control is
# off, so that an engineer can see what arming the plant would command.
if not re.search(r'\}\s*else if \(!measurement_fresh\) \{\s*'
                 r'(?:/\*.*?\*/\s*)?policy = \(power_control_output_t\)\{0\};',
                 source, re.S):
    failures.append('the held decision is not cleared when the measurement goes '
                    'stale, so a setpoint can outlive the reading it was computed '
                    'from and still be reported as current')

# ---------------------------------------------- 3. the interval is the real gap
interval = re.search(r'static float safe_interval_seconds\(.*?\n\}', source, re.S)
if not interval:
    failures.append('safe_interval_seconds could not be located')
else:
    text = interval.group(0)
    # The old guard substituted the configured tick for any gap longer than four
    # ticks, which is every real measurement gap once the loop is data-driven.
    if re.search(r'configured\s*\*\s*4', text):
        failures.append('the interval still collapses to the configured tick for '
                        'any gap longer than four ticks. Driven by measurements '
                        'that gap is about a second against a 20 ms tick, so the '
                        'integral would accumulate fifty times too little and the '
                        'per-second ramps would be fifty times too tight')
    if 'meter_stale_timeout' not in text:
        failures.append('the interval is no longer bounded by the stale timeout, '
                        'which is the only honest ceiling on the gap between two '
                        'samples the loop could have acted on in sequence')

# Called once per new measurement, not once per tick.
calls = re.findall(r'safe_interval_seconds\(timestamp', source)
if len(calls) != 1:
    failures.append(f'safe_interval_seconds is called {len(calls)} times. Calling '
                    f'it once per tick advances its own clock on cycles that make '
                    f'no decision, so the gap it reports is the tick again rather '
                    f'than the distance between the samples acted on')
elif not re.search(r'if \(new_measurement\) \{\s*\n\s*interval_seconds = '
                   r'safe_interval_seconds', source):
    failures.append('safe_interval_seconds is not called under the new-measurement '
                    'gate, so its clock advances on cycles that made no decision')

# ------------------------------------------- 4. freshness is still a separate test
if 'meter_sample_fresh(&grid, timestamp)' not in source:
    failures.append('the freshness gate is gone. New and fresh are different '
                    'questions -- a reading can be one the loop has not seen and '
                    'still be too old to control on -- and the fail-closed path '
                    'depends on the second one still being asked')

if failures:
    print('Control cadence contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('Control cadence contract passed '
      '(the policy steps on a reading not acted on before and holds otherwise; '
      'disabling control clears the held decision; the integral and ramps are '
      'given the measured gap; freshness is still enforced separately)')
