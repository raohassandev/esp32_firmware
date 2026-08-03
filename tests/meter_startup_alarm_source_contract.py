"""A meter the controller could not reach has not necessarily failed.

Every restart raised alarms against a healthy meter: a critical "the primary grid
meter is not communicating -- check meter power, communication wiring, gateway
and network path", a stale-data warning, and a critical "grid measurement
unavailable". All cleared themselves seconds later. Observed on the plant against
an EM500 that was answering every second before and after.

The journal gave the cause. In order: "Controller started" and "Controller
network offline" at 55 s, "Network restored" at 35 s, the meter alarms raised in
that SAME observation, and cleared by 25 s. The meter is reached over the
controller's own network. While that network was still connecting, every poll
failed from the near end -- so by the moment the link came up the meter already
looked like an instrument with a history of failures, and it was condemned ten
seconds before it had any chance to answer.

The alarms were not wrong about the DATA -- there was none. They were wrong about
the CAUSE, and the cause is the whole content of an alarm. It sent an engineer to
inspect intact wiring at the far end of a link the controller had not yet joined,
and an alarm that fires at every startup and always clears teaches everyone to
discount the one class of alarm that means the controller has lost sight of the
plant. The true cause was already alarmed, at critical, with the right action.

This contract holds the four things that make the fix correct rather than merely
quiet.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
API = ROOT / 'components' / 'web_server' / 'operational_api.c'
SAFETY = ROOT / 'components' / 'safety_manager' / 'safety_manager.c'

failures = []


def read(path):
    return path.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')


api = read(API)
safety = read(SAFETY)

# ------------------- 1. the clock starts when the link does, and runs on events
#
# Gating on the network alone was tried first and changed almost nothing: the
# failure counters had already filled while the Wi-Fi was connecting, so the
# meter was judged the instant the link returned. What makes the verdict honest
# is that it waits for a poll ATTEMPTED AFTER the link existed.
ready = re.search(r'state->meter_verdict_ready\s*=(.*?);', api, re.S)
if not ready:
    failures.append('nothing computes meter_verdict_ready, so a controller that '
                    'cannot reach its meter is indistinguishable from one whose '
                    'meter has stopped answering')
else:
    expression = ready.group(1)
    if 'network_online' not in expression:
        failures.append('the verdict ignores the controller\'s own network. While '
                        'it is down every meter poll fails from the near end, so '
                        'the meter is alarmed as unresponsive and an engineer is '
                        'sent to inspect intact wiring at the far end of a link '
                        'the controller had not joined')
    if 's_network_online_since_ms' not in expression:
        failures.append('the verdict is not anchored to WHEN the link came up, so '
                        'failures accumulated while the controller was offline '
                        'still condemn the meter the moment it comes back -- the '
                        'exact sequence recorded on the plant')
    if 'last_attempt_ms' not in expression:
        failures.append('the verdict does not wait for a completed poll, so it is '
                        'given before the meter has had a chance to answer')
    # One COMPLETED poll, either way. Waiting for a SUCCESS would invert the bug:
    # a meter that never answers would never produce a verdict and never alarm.
    if 'success_count' in expression:
        failures.append('the verdict waits for a SUCCESSFUL read. A meter that '
                        'never answers then never produces a verdict and never '
                        'alarms. meter_manager stamps last_attempt_ms before it '
                        'branches on the outcome, so one completed poll settles '
                        'it either way and a dead meter is still reported')
    # A fixed startup grace period is the easy version and the wrong one: it goes
    # quiet for a set interval whether or not the meter is dead.
    if re.search(r'\b(uptime|grace|GRACE|STARTUP_)\b', expression):
        failures.append('the verdict is deferred by elapsed time. A grace period '
                        'goes quiet for a fixed interval whether or not the meter '
                        'is dead, so a plant commissioned against the wrong meter '
                        'address alarms only after the timer expires rather than '
                        'at its first completed poll')

verdict = re.search(r'static bool meter_verdict_unavailable\(.*?\n\}', api, re.S)
if not verdict:
    failures.append('meter_verdict_unavailable() could not be located; the state '
                    'in which the controller has learned nothing about its meter '
                    'is no longer named in one place')
elif 'meter_verdict_ready' not in verdict.group(0):
    failures.append('meter_verdict_unavailable() no longer reads the readiness it '
                    'exists to report')

# --------------------------------------------------- 2. both paths are guarded
first = re.search(r'if \(!s_observed\.initialized\) \{(.*?)\n        return;', api, re.S)
if not first:
    failures.append('the first-sample branch of detect_events could not be located')
else:
    body = first.group(1)
    for event in ('EVENT_METER_STATE', 'EVENT_METER_OFFLINE_ALARM',
                  'EVENT_METER_STALE_ALARM'):
        raise_line = re.search(r'if \(([^{]*?)\)\s*\{\s*append_event_ex\(%s' % event,
                               body, re.S)
        if not raise_line:
            continue
        if 'meter_unknown' not in raise_line.group(1):
            failures.append(f'{event} is still raised at the first sample without '
                            f'asking whether there is a verdict to give: this is '
                            f'the alarm that fired at every restart')

    # The optimistic record is the load-bearing half. Latching the fault instead
    # would mean a meter that NEVER answers produces no transition and therefore
    # no alarm at all -- silent on a dead meter, which is worse than the bug.
    if not re.search(r'meter_unknown[^;]*\{.*?s_observed\.meter_online\s*=\s*true',
                     body, re.S):
        failures.append('the first sample latches the fault into s_observed '
                        'instead of recording the meter as healthy, so a meter '
                        'that never answers never transitions and never alarms')

# Suppressing only the first sample would MOVE the false alarm rather than remove
# it: the optimistic state recorded at boot transitions to faulty at the next
# observation and raises the same thing one interval later.
steady = api[first.end():] if first else api
if 'const bool meter_unknown = meter_verdict_unavailable(next);' not in steady:
    failures.append('the steady-state path does not consult the verdict, so the '
                    'false alarm merely moves to the next observation: the '
                    'optimistic state recorded at boot transitions to faulty as '
                    'soon as the following sample is taken')
for name in ('next_offline', 'next_stale'):
    line = re.search(r'bool %s = (.*?);' % name, steady)
    if not line:
        failures.append(f'{name} could not be located in the steady-state path')
    elif 'meter_unknown' not in line.group(1):
        failures.append(f'{name} ignores whether there is a verdict to give')

# ------------------------------------------------ 3. the cause is still alarmed
#
# The whole justification for silencing the consequence is that the cause is
# reported. If the network alarm ever goes, this becomes plain suppression.
if not re.search(r'append_event\(EVENT_NETWORK_STATE', api):
    failures.append('the controller no longer alarms its own network state. The '
                    'meter alarms are deferred precisely BECAUSE this one reports '
                    'the true cause; without it the plant is silent about a '
                    'controller that cannot see its meter')

# --------------------------------------------------- 4. control is not touched
#
# The limiter must go on raising both flags whatever the read history is. This is
# the line that keeps PV at zero when there is no measurement.
limiter = re.search(r'float safety_manager_limit_target_kw\(.*?\n\}', safety, re.S)
if not limiter:
    failures.append('safety_manager_limit_target_kw could not be located')
else:
    text = limiter.group(0)
    if 'verdict' in text or 'network_online' in text:
        failures.append('the safety limiter now considers whether the meter could '
                        'be reached. Deferring an operator ALARM is a display '
                        'decision; deferring the CONTROL inhibit would let the '
                        'controller command PV against a measurement it has never '
                        'taken. The flags must be raised regardless')
    if 'SAFETY_ALARM_METER_OFFLINE' not in text or 'SAFETY_ALARM_METER_STALE' not in text:
        failures.append('the safety limiter no longer raises both meter flags')
    if not re.search(r'return\s+s_alarm_flags\s*\?\s*0\.0f\s*:', text):
        failures.append('the safety limiter no longer forces PV to zero while a '
                        'meter flag stands')

if failures:
    print('Meter startup alarm contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('Meter startup alarm contract passed '
      '(the meter is judged only on a poll completed after the link came up; '
      'both event paths defer; the cause is still alarmed; the safety limiter '
      'still inhibits control regardless)')
