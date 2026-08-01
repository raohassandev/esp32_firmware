"""The Grid power page states each measured quantity once.

WHAT THE OWNER SAW. On the Grid power page with Engineering unlocked, the same
voltages, currents and powers were rendered twice on one screen by two
different modules:

  - web/em500-core.js draws the EM500 workspace ("Read-only live values"), with
    its own live/energy/status/setting tabs.
  - web/devices.js draws the legacy meter card underneath it and appended
    AutomatrixMeterDetail.render(), which is the same register block again.

Two renderings of one quantity are worse than one. They are read as two
measurements, and when they disagree -- different poll instants, different
decode paths -- there is nothing on the screen to say which is right.

WHAT MUST NOT HAPPEN INSTEAD. Deleting the legacy card to remove the duplicate
would delete the diagnostics, because the endpoint, the acquisition function
and PDU address, the data format, the poll and timeout timings, the success and
error counters and the per-phase split are rendered nowhere else in the
product. The duplicate is the measurement matrix, not the card.

So this contract holds both halves:

  1. The card does not append the measurement matrix while the EM500 workspace
     is on the page.
  2. The card still carries the facts that exist only on it.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEVICES = ROOT / 'web' / 'devices.js'

failures = []

source = DEVICES.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')

card = re.search(r'function meterCard\(meter\)\s*\{(.*?)\n    \}\n', source, re.S)
if not card:
    failures.append('meterCard() could not be located')
else:
    body = card.group(1)

    # ------------------------------------------------------- 1. no duplicate
    #
    # Anchored on the guard, not on the presence of the word "em500" anywhere in
    # the function: a comment explaining the rule reads exactly like the rule
    # while the call underneath it stays unconditional.
    detail = re.search(r'const detail = ([^;]+);', body, re.S)
    if not detail:
        failures.append('the meter detail block is no longer assigned in '
                        'meterCard(); if it is appended unconditionally the '
                        'measurement matrix is drawn twice on the Grid power '
                        'page whenever Engineering is unlocked')
    else:
        expression = detail.group(1)
        if 'AutomatrixMeterDetail' not in expression:
            failures.append('the meter detail block is no longer rendered at '
                            'all, so a family with a transcribed block shows '
                            'none of it when the workspace is absent')
        # The guard may be lifted into its own const, so the condition is
        # resolved one level rather than demanding it inline -- a contract that
        # insists on a particular spelling fails a correct refactor and teaches
        # people to work around it.
        guarded = 'em500Workspace' in expression
        if not guarded:
            names = re.findall(r'[A-Za-z_$][\w$]*', expression)
            for name in names:
                held = re.search(r'const %s\s*=\s*([^;]+);' % re.escape(name), body, re.S)
                if held and 'em500Workspace' in held.group(1):
                    guarded = True
                    break
        if not guarded:
            failures.append('the measurement matrix is appended without asking '
                            'whether the EM500 workspace is already rendering '
                            'the same registers above it: every voltage, '
                            'current and power appears twice on one screen, '
                            'from two modules that poll at different instants')

    # --------------------------------------------- 2. the diagnostics survive
    #
    # These are the labels the workspace does NOT carry. If the card is ever
    # reduced to remove the duplicate, this is what would be lost with it.
    for label, why in (
        ('Endpoint', 'which address and unit id this meter answers on'),
        ('Acquisition', 'the function code and PDU address actually polled'),
        ('Format', 'the data type, word order and scale the value is decoded with'),
        ('Timing', 'the poll interval and timeout'),
        ('Successful polls', 'how often the meter has answered'),
        ('Errors', 'the failure count and the consecutive run'),
        ('Last error', 'what the last failure actually was'),
    ):
        if f"'{label}'" not in body:
            failures.append(f'"{label}" has gone from the meter card and is '
                            f'rendered nowhere else: {why}')

    if 'phase_power_kw' not in body:
        failures.append('the per-phase active power split has gone from the '
                        'meter card; it is how an unbalanced site is seen, and '
                        'how a limit the total does not appear to justify is '
                        'explained')

if failures:
    print('Meter page duplication contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('Meter page duplication contract passed '
      '(the measurement matrix is drawn once; the acquisition, timing, error '
      'and per-phase diagnostics survive on the card)')
