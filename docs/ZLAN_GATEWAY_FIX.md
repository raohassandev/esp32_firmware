# ZLAN gateway configuration for reliable shared-bus Modbus (blocks physical acceptance)

## Device (confirmed from the unit's own web UI)

- Vendor: Shanghai ZLAN, `Modbus TCP to RTU` gateway at `192.168.100.200`
- Equipment name `ZLDEV0001`, MAC `04-EE-E8-16-D1-78`, **firmware V1.523**
- Work mode `TCP server`, port `502`, web port `80`
- Serial: **9600 8/none/1**, flow control none
- Conversion Agreement: `Modbus TCP to RTU`

## Observed state that causes the failure

On the live unit:

- **Multi-host configuration = Disable**
- **Command response timeout = 0**
- Idle time interval = 20 ms

With multi-host disabled and the response timeout at 0, the gateway acts as a
plain byte bridge with **no request↔response matching**. The EM500 turnaround was
measured up to ~1040 ms; long before that reply arrives the ESP32 (per-transaction
mode) has already closed its TCP session, so the late RTU response is left in the
gateway and handed to the **next** TCP connection. That is the source of the
frames carrying a transaction id and unit id nobody requested — proven by reading,
with the ESP32 idle, responses whose TIDs were a monotonic counter no client sent.

## Correction to the earlier version of this document

An earlier revision said to **disable** multi-host. That was wrong, and the live
screenshot disproves it: multi-host was already disabled and the gateway was still
cross-delivering. On the ZLAN, multi-host mode is precisely the feature that adds
per-request queuing and response matching for a shared RS485 bus. It must be
**enabled**, with a response timeout long enough to cover the slowest meter.

## The fix — two changes on the gateway web UI

`http://192.168.100.200` → log in → in **Multi-host setup**:

1. **Multi-host configuration: Disable → Enable**
2. **Command response timeout: 0 → 1280** ms
   - Must be a multiple of 32 (1280 = 32 × 40).
   - Covers the observed ~1040 ms EM500 turnaround with margin.
3. Leave Conversion Agreement = `Modbus TCP to RTU`, baud 9600 for now.
4. Submit change and let the gateway reboot.

## Acceptance test after the change (must pass before resuming)

1. ESP32 off the bus, single strict client (`/tmp/mb2.py` pattern: persistent
   socket, incrementing MBAP TID, strict TID + unit-id validation, sequential):
   send N unit-1 requests with known TIDs. **Every** response must echo the sent
   TID and unit id 1 — zero foreign TIDs, zero foreign unit ids.
2. ESP32 polling all three: `/api/meters` success ~100%, all three `online`,
   `data_age_ms` well under `stale_after_ms`.

## After transport is reliable — performance tuning (separate step)

- The 1040 ms EM500 tail may shrink once buffering is clean; re-measure turnaround
  and then lower Command response timeout toward the real worst case for a faster
  scan.
- Raise serial baud (38400, then 115200) on gateway **and** all three meters
  together; reduce Idle time interval toward the Modbus t3.5 floor
  (~4 ms at 9600, ~1.75 ms above 19200).
- On the firmware side, the fixed 75 ms inter-session settle should be re-derived
  from t3.5 once the gateway is deterministic (tracked separately).

## If it still cross-delivers after multi-host is enabled

Then it is a confirmed gateway defect/incompatibility: replace with a transparent
Modbus TCP↔RTU gateway or restrict it to a single Modbus TCP master. The
controller firmware must never be changed to accept mismatched TID/unit frames —
that would let a generator's reading appear as the Grid measurement.
