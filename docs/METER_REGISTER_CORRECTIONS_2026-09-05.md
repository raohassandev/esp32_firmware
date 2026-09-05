# Meter register corrections — 2026-09-05

This note supersedes older commissioning examples where they conflict with the live, cross-validated meter evidence below. Historical documents remain unchanged as records of what was tested at the time.

## Automatrix EM500 — active power

For the installed EM500 devices used on the 2026-09-05 physical bench:

- Modbus function: FC03
- PDU/address base: zero-based physical address
- System total active power: **0x003A / decimal 58**
- Data type: INT32
- Word order: ABCD / high-word first in the repository decoder convention
- Controller scale: **0.00001 kW/raw**

Address 57 (`0x0039`) is a misaligned 32-bit read and must not be used as the active-power starting address. Live contiguous-register evidence showed `0x003A,0x003B` agreeing with the phase-power sum, VA and PF while a read beginning at `0x0039` produced an impossible value.

The fresh/factory Grid-meter default is therefore PDU 58. Existing commissioned NVS is not silently rewritten by this correction.

## Carlo Gavazzi WM15 — GEN-1 active power

Authoritative source: Carlo Gavazzi *WM15 Communication Protocol V1.1, 25-Oct-2019*, supplied during physical commissioning.

Validated profile:

- Modbus function: FC03 (FC04 is also valid for the documented read variables)
- PDU/address base: zero-based physical address
- System total active power `W sys`: **0x0028 / decimal 40**
- Length: 2 words
- Data type: INT32
- Register word order: LSW→MSW, repository decoder **CDAB**
- Weight: Watt ×10
- Controller scale: **0.0001 kW/raw**
- PF sys: 0x0031, INT16, raw/1000
- Phase sequence: 0x0032, INT16
- Frequency: 0x0033, INT16, raw/10 Hz
- Variant identification: 0x000B, UINT16

`0x003A` is **W sys DMD MAX**, not instantaneous system active power, and must not be used as the fast control input.

The live WM15 at slave 2 was cross-validated against an independent Modbus client before this profile was marked verified.

## Shared TCP-to-RTU gateway rule

When several Modbus slave IDs are behind the same TCP-to-RTU gateway endpoint, requests to that exact `host:port` must be serialized because they share one downstream RTU bus. The current meter runtime groups meters by gateway endpoint and applies the qualified inter-session settle time. Different gateway endpoints may operate independently.

## Qualification rule

A commissioning connection test is read-only. It must not replace the live meter array or claim success against pre-restart runtime. The release commissioning UI first matches the proposed device against the already persisted runtime endpoint and acquisition mapping, then performs repeated read-only checks. Configuration changes remain an explicit Engineering action and keep automatic control fail-closed until restart and fresh evidence.
