# Commissioning Wizard Engineering TODO

## Target workflow

1. Site Details
2. Devices
3. Communication Channel
4. Modbus Tuning
5. Connection Test
6. Controller Health
7. Review and Finish

The wizard is mobile-first and must remain fully usable on 320 px phones, tablets, and laptops. Every step must expose only the information required for the current engineering decision.

## Release principles

- Never show a device as Ready after one successful request.
- Never mark RTU ready until the RTU runtime is active and the selected UART/RS-485 port is owned safely.
- Never infer register maps or byte order for an unqualified model.
- Never enable physical inverter writes during commissioning tests.
- Preserve NVS; no full-flash erase.
- Block completion when required evidence is missing.

## Phase A — Commissioning UX foundation

- [x] Site Details step
- [x] Device catalogue for meters and inverter profiles
- [x] Per-device TCP/RTU channel editor
- [x] Mobile-first step navigation and sticky actions
- [ ] Add Modbus Tuning step between channel and test
- [ ] Add Controller Health step after connection test
- [ ] Add final Review and Finish step
- [ ] Save/restore draft commissioning state locally
- [ ] Add per-device completion badges and step blockers
- [ ] Add editable device role: grid meter, generator meter, load meter, inverter
- [ ] Add duplicate endpoint/slave-ID conflict summary

## Phase B — Modbus tuning model

### Transaction timing

- [ ] Poll/inter-call interval
- [ ] Response timeout
- [ ] Device response delay
- [ ] Retry count
- [ ] Retry interval/backoff
- [ ] Communication-detection attempts
- [ ] Failure ceiling
- [ ] Reconnection ceiling
- [ ] Normal/high/low scan frequency policy
- [ ] Per-device priority

### Data interpretation

- [ ] Function code
- [ ] Address convention: base 0, base 1, 40001 notation
- [ ] Start register and block length
- [ ] Data type
- [ ] Byte order and word order
- [ ] Signedness
- [ ] Scale and offset
- [ ] Engineering unit and display precision
- [ ] Batch-read and batch-write capability flags

### RTU bus discipline

- [ ] Silent interval
- [ ] Driver-enable pre-delay
- [ ] Driver-disable post-delay
- [ ] Request/response turnaround delay
- [ ] Shared-bus utilization estimate
- [ ] Duplicate slave-ID protection per UART

### TCP behavior

- [ ] Connection timeout
- [ ] Persistent socket/connection policy
- [ ] Keepalive policy
- [ ] Reconnect delay and ceiling
- [ ] Gateway-latency allowance

### Derived validation

- [ ] Estimated single-transaction duration
- [ ] Estimated full scan cycle
- [ ] Stale-data margin
- [ ] Retry-amplified worst-case scan time
- [ ] Warnings and blockers for unsafe timing combinations

## Phase C — Persistent runtime configuration

- [ ] Introduce communication channel enum: TCP/RTU
- [ ] Add versioned migration from the current TCP-only configuration
- [ ] Persist RTU UART, baud, parity, data bits, stop bits, slave ID, and timing
- [ ] Persist tuning fields per meter/inverter
- [ ] Validate configuration before NVS commit
- [ ] Disable automatic control when communication topology changes
- [ ] Require restart only when the driver cannot be reinitialized safely

## Phase D — Runtime drivers

- [ ] Shared Modbus transaction scheduler
- [ ] Modbus TCP timing/retry application
- [ ] Modbus RTU master component
- [ ] RS-485 port ownership and collision protection
- [ ] Meter polling through TCP or RTU
- [ ] Inverter read-only probing through TCP or RTU
- [ ] Scan priorities and high/normal/low frequency classes
- [ ] Runtime communication statistics per device

## Phase E — Connection qualification

- [ ] Channel-open test
- [ ] Modbus request/response validation
- [ ] Exception-code reporting
- [ ] Consecutive-read stability test
- [ ] Minimum/average/maximum latency
- [ ] Success, timeout, CRC/frame, retry, and decode counters
- [ ] Raw and scaled value comparison
- [ ] Expected-range check
- [ ] Clear Ready / Ready with warning / Blocked verdict
- [ ] Re-test one device or all devices

## Phase F — ESP32 health and performance

- [x] Initial read-only resource API source
- [ ] Register resource API in the firmware build and HTTP server
- [ ] Correct flash-size reporting
- [ ] Report chip model, revision, cores, and configured CPU frequency
- [ ] Report uptime and reset reason
- [ ] Report total/free/minimum/largest internal heap
- [ ] Report PSRAM total/free when available
- [ ] Report task count
- [ ] Report temperature only when the sensor driver is initialized
- [ ] Report scan-loop average/max duration and missed cycles
- [ ] Report communication utilization and longest transaction
- [ ] Calculate controller reliability verdict with explicit thresholds

## Phase G — Reporting and acceptance

- [ ] Final site/device/channel/tuning summary
- [ ] Device qualification evidence table
- [ ] Controller-health evidence table
- [ ] Remaining blockers and warnings
- [ ] Export commissioning JSON
- [ ] Export human-readable commissioning report
- [ ] Finish action remains disabled while blockers exist
- [ ] Record firmware SHA, configuration version, date, and engineer

## Phase H — Automated verification

- [ ] Source contract for seven-step wizard
- [ ] Source contract for Modbus tuning fields and validation
- [ ] Source contract for system resource API
- [ ] No-write commissioning safety contract
- [ ] Mobile layout contract at 320/360/390 px
- [ ] ESP-IDF v6.0.1 zero-warning build
- [ ] Simulator qualification scenarios
- [ ] Physical TCP meter qualification
- [ ] Physical RTU meter qualification after RTU runtime completion
