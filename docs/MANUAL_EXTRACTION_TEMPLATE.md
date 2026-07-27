# Inverter manual extraction template

Use one section per exact manufacturer/model family and manual revision.

## Identity

- Profile ID:
- Manufacturer:
- Model family:
- Supported exact models:
- Manual filename:
- Manual revision/date:
- Manual repository path:
- Protocol:
- Connection mode:

## Addressing

- Register notation:
- PDU conversion:
- Function code(s):
- Slave ID rules:
- Word order:
- String encoding:

## Safe read-only points

| Purpose | Register | PDU | FC | Words | Type | Scale | Unit | Evidence page |
|---|---:|---:|---:|---:|---|---:|---|---:|

## Power control

| Purpose | Register | PDU | FC | Words | Type | Scale | Range | Evidence page |
|---|---:|---:|---:|---:|---|---:|---|---:|

## Required sequence

1. Identity/compatibility check:
2. Remote-control enable/unlock:
3. Limit write:
4. Commit/apply command:
5. Readback:
6. Disable/release control:
7. Minimum command interval:
8. Timeout/retry limits:

## Safety and qualification

- Manual restrictions:
- Firmware/version restrictions:
- Grid-code restrictions:
- Failure behavior:
- Simulator evidence:
- Bench evidence:
- Read-only qualification:
- Write qualification:
- Production approval:

No profile may become writable from this document alone. Physical command/readback qualification and explicit approval are mandatory.
