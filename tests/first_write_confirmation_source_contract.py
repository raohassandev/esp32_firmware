"""The first setpoint on a machine must be confirmable.

Found on the plant. A 120 kW Huawei was enabled, online and answering, 100 kW of
load stood on the grid, automatic control was on -- and no command went out. The
controller reported commandable_rated_kw 0, command_tested 0, last_write_ok 0 and
"an inverter setpoint could not be confirmed by readback; that inverter is held
at zero and excluded from the fleet". A direct read-only probe of the setpoint
register showed the controller's own safe zero sitting in it: the write had
reached the inverter and the controller could not see that it had.

The readback poll was gated on has_command, which is assigned in exactly one
place -- the branch where a write has already been CONFIRMED. So:

    write issued -> readback never polled -> no evidence -> confirmation_fault
    latches -> the inverter leaves the commandable fleet -> no further write is
    issued -> nothing can ever clear the fault

A machine that had never had a confirmed setpoint could never get one. The gate
admitted only the state it was supposed to produce.

This is a deadlock, not a slow path: no amount of waiting, sun, load or retrying
resolves it, and every symptom points at the inverter rather than at the poll
that was never made. It cost a plant its entire solar output.

WHAT THIS HOLDS.

  1. The readback is polled while a write is OUTSTANDING, not only once one has
     succeeded. write_issued is set on every attempt; has_command only after a
     confirmation.

  2. The fault still latches and still excludes the inverter. The fix is to let
     the evidence arrive, NOT to assume the write landed -- an unconfirmed
     setpoint must still hold the machine at zero.

  3. The readback stays off the control path. It rides the acquisition task,
     after the telemetry gate, so confirming a setpoint cannot add a blocking
     transaction to a control cycle or to an HTTP handler.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANAGER = ROOT / 'components' / 'inverter_manager' / 'inverter_manager.c'

failures = []

source = MANAGER.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')

# ------------------------------------------------ 1. the gate admits the first
call = re.search(r'if \(([^;]*?has_power_limit_readback[^;]*?)\)\s*\{\s*'
                 r'\(void\)poll_readback', source, re.S)
if not call:
    failures.append('the readback poll call could not be located; if a setpoint '
                    'is no longer read back at all, no write on this product can '
                    'be confirmed and every commanded inverter is held at zero')
else:
    gate = call.group(1)
    if 'write_issued' not in gate:
        failures.append('the readback is polled only when has_command is already '
                        'set, and has_command is assigned ONLY where a write has '
                        'been confirmed. The first setpoint on a machine can then '
                        'never be confirmed: the evidence that would confirm it '
                        'is gathered only after it has been confirmed. This is '
                        'the deadlock that cost the plant its solar output')
    if 'has_power_limit_readback' not in gate:
        failures.append('the readback is polled without asking whether the profile '
                        'describes one')

# has_command must remain the CONFIRMED-only flag. If it is ever set earlier --
# on issue, say -- the confirmation becomes self-fulfilling: the controller would
# treat a write it merely sent as a write the machine accepted.
assignments = re.findall(r'runtime->data\.has_command\s*=\s*true', source)
if len(assignments) != 1:
    failures.append(f'has_command is assigned true in {len(assignments)} places. '
                    f'It must be set only where a readback has CONFIRMED the '
                    f'setpoint; setting it on issue would make the controller '
                    f'report a write it merely sent as one the inverter accepted')

# ------------------------------------- 2. an unconfirmed setpoint still fails closed
if not re.search(r'runtime->data\.confirmation_fault\s*=\s*true', source):
    failures.append('nothing latches confirmation_fault any more. Polling the '
                    'readback earlier is only safe BECAUSE an unconfirmed '
                    'setpoint still holds the machine at zero; without the latch '
                    'this change would let an unverified write stand')

fault_clear = re.search(r'verdict\.state == INVERTER_WRITE_CONFIRMED.*?'
                        r'confirmation_fault\s*=\s*false', source, re.S)
if not fault_clear:
    failures.append('confirmation_fault is no longer cleared by a CONFIRMED '
                    'verdict, so a machine that recovers stays excluded for ever')

# ------------------------------------------- 3. still off the control path
#
# Constraint from the owner: HTTP handlers must not perform blocking Modbus
# transactions, and the control loop must not grow one either. The readback runs
# in the acquisition task; this checks it has not migrated.
task = re.search(r'static void inverter_telemetry_task\(.*?\n\}', source, re.S)
if not task:
    failures.append('inverter_telemetry_task could not be located')
elif 'poll_readback' not in task.group(0):
    failures.append('poll_readback no longer runs in the acquisition task. If it '
                    'moved to the control loop or to a request handler it puts a '
                    'blocking Modbus transaction on a path that must not have one')

if failures:
    print('First-write confirmation contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('First-write confirmation contract passed '
      '(the readback is polled while a write is outstanding, so a first setpoint '
      'can be confirmed; an unconfirmed one still holds the machine at zero; the '
      'poll stays in the acquisition task)')
