# Real-site source evidence commissioning handoff

This handoff is the machine-checkable evidence format for Issue #81. It does not discover or guess a site's Grid, Generator, ATS, breaker, run or synchronism mapping. Unknown fields remain unqualified and automatic control must remain disabled until the actual site evidence is proven.

## Start from the fail-closed template

Copy:

```bash
cp tools/site_source_commissioning.example.json site-source-commissioning.json
```

Fill the record only from the exact approved site SLD/wiring drawings, exact device manuals and observations made on the real equipment.

Every required channel must identify its semantic purpose, signal provenance, manufacturer/model/manual revision, site drawing reference, exact hardwired terminal/input or exact Modbus endpoint/Unit-ID/function/address/mask-value, active/inactive raw values, validated timing authority, and the related meter evidence where applicable.

## Physical proof required per qualified channel

A `pass` channel must include a real before/after state change with raw and runtime observations, stale/missing evidence test proving fail-closed behavior, recovery-dwell proof showing authority did not return early, exact persisted configuration readback, and a clear evidence reference for HMI/API/log/photo/video observations.

Meter power may corroborate electrical direction but the record must explicitly keep `power_sign_used_as_source_authority` false. A meter sign or kW threshold cannot replace breaker/ATS/run/synchronism evidence.

If synchronized Grid+Generator operation is physically impossible, record the synchronism channel as `not_supported`, `required: false`, with the authoritative topology/interlock reference. Do not fabricate a synchronized PASS.

## Validation

Run against the exact flashed identity:

```bash
python3 tools/site_source_commissioning_verify.py \
  site-source-commissioning.json \
  --expected-firmware-sha <exact-flashed-sha> \
  --expected-artifact-digest sha256:<exact-artifact-digest>
```

The command returns non-zero for identity mismatch, missing/guessed provenance, incomplete mapping, unqualified required channels, missing physical toggle/stale/recovery/persistence proof, unproven meter scaling/sign, early authority return, or incomplete configuration acceptance.

A validator PASS means the submitted record satisfies the evidence contract. It does not retroactively prove any observation that was not actually made, and it does not replace authorized physical commissioning or safety review.
