# Acquisition timing: measured, not estimated

**Date:** 2026-07-30
**Target:** site EM500 energy meter, unit 3, via its ZLAN gateway at `192.168.100.200:502`
**Signal:** equivalent three-phase active power, FC03, PDU 57, 2 registers
**Method:** back-to-back Modbus TCP transactions on one socket from a PC on the
same Wi-Fi network as the controller

This exists because the acquisition design was changed to **poll-on-completion**
(issue the next request as soon as the previous transaction returns) and the
performance claims for that need to rest on measurement. An earlier estimate in
this project was taken from best-case latency and was wrong by roughly 3x.

---

## 1. Sustained rate, three cadences

Each run: 10 s, one socket, no reconnect.

| Configured gap | Achieved | Failures | min | p50 | p95 | max |
|---|---|---|---|---|---|---|
| **0 ms (poll-on-completion)** | **10.9 req/s** | 0 % | 26.6 ms | 28.9 ms | 294.7 ms | 303.7 ms |
| 10 ms | 8.1 req/s | 0 % | 26.7 ms | 30.3 ms | 297.7 ms | 316.4 ms |
| 25 ms | 6.2 req/s | 0 % | — | 33.4 ms | 302.5 ms | 310.9 ms |

Two conclusions:

- **Poll-on-completion is safe on this hardware.** Zero failures at the maximum
  rate the link will carry. The gateway does not complain, drop, or return
  exceptions when driven as fast as it can answer.
- **Adding a delay buys nothing here.** It reduces throughput without improving
  latency, because the latency is not caused by contention. So 0 ms is the right
  default for this site, and the configurable delay exists for equipment that
  behaves differently, not for this one.

## 2. The latency is bimodal, and that is the important finding

129 back-to-back transactions:

| | |
|---|---|
| mean | **93.1 ms** |
| p50 | 28.7 ms |
| p90 | 293.5 ms |
| max | 318.7 ms |
| under 50 ms | **74 %** |
| over 250 ms | **24 %** |
| over 300 ms | 3 % |

This is not jitter around a mean. Three quarters of transactions complete in under
50 ms and about a quarter take over 250 ms, clustered near 290–300 ms. That shape
suggests a periodic stall — the gateway's own RS-485 scan cycle, or the meter's
internal update period — rather than network variance.

**Consequence for the achievable sample rate:** the honest figure is
**~11 samples/s, mean data age ~93 ms**, not the ~27/s and ~37 ms that the median
alone implies. The mean is what matters for a control loop, because the slow
transactions are exactly the ones during which the controller is acting on an
older sample.

Which side of the link causes the stall cannot be determined from Modbus TCP
alone. Distinguishing gateway from meter needs an RS-485 capture at the gateway,
which is a site activity.

## 3. A live defect this exposed

The meter endpoint timeout was **300 ms**, chosen from best-case figures. It sits
*inside* the measured tail: about **3 % of perfectly good responses overran it**
and were recorded as failures.

That is not a cosmetic mislabel. Each one:
- wastes the full timeout,
- increments consecutive failures and triggers exponential backoff,
- feeds the 20-sample quality window that marks a meter **degraded** below 80 %
  success, and a degraded meter **blocks control input**.

So a healthy meter was being intermittently reported as unhealthy. This is
consistent with the `EM500 slave 3 communication degraded: 60% success over 5
requests; control input blocked` line observed in a boot log earlier in the
project, which had been attributed to start-up transients.

The default is now **800 ms**, which clears the measured maximum with margin. It
does not slow control: control reads the cached sample and applies its own
staleness rule, and the poll task runs below control in priority on the same core.
The cost is that a genuinely dead endpoint takes 800 ms to declare, which the
failure backoff spaces out regardless.

**A commissioned unit keeps its stored 300 ms** until reconfigured. The new value
applies to a fresh configuration only.

## 4. What is still unmeasured

1. **The same figures from the controller itself.** These were taken from a PC on
   the same network. The ESP32 has a different TCP stack, a single Wi-Fi radio
   shared with the HTTP server, and its own scheduling. Its numbers will be worse.
   The firmware already records `last_response_time_ms` per meter; it should be
   surfaced and compared against this table.
2. **Two meters at once.** A second meter on the same gateway doubles the offered
   load onto a device that already stalls ~24 % of the time. Untested.
3. **Simultaneous inverter traffic.** Acquisition and command writes share the
   network and the gateway may share the RS-485 bus.
4. **Behaviour under HTTP load.** The measurements were taken with an idle web
   interface. The UI now reads from cache rather than the Modbus mutex, so
   interference should be limited to the radio, but that is reasoning, not data.
5. **Whether the ~300 ms stall is periodic or load-dependent**, which decides
   whether it can be avoided at all.
