# Real-site source evidence commissioning handoff

This handoff is the machine-checkable evidence format for Issue #81. It does not discover or guess a site's Grid, Generator, ATS, breaker, run or synchronism mapping. Unknown fields remain unqualified and automatic control must remain disabled until the actual site evidence is proven.

## Freeze the exact site identity first

Before executing commissioning, independently freeze the intended identity from the authorized engineering package. Do not copy these expected values from the completed evidence JSON at validation time:

- exact flashed firmware SHA;
- exact firmware artifact SHA-256;
- site ID;
- exact persisted configuration identity;
- approved site SLD revision/file and SHA-256;
- approved source-channel map reference and SHA-256.

The final evidence package must also have its own immutable reference and SHA-256. A self-consistent record from another site, another configuration, another SLD or another channel map must fail validation.

## Start from the fail-closed template

Copy:

```bash
cp tools/site_source_commissioning.example.json site-source-commissioning.json
```

The starter is explicitly `UNEXECUTED_TEMPLATE_NOT_PHYSICAL_EVIDENCE`. A genuinely executed record must use `EXECUTED_SITE_SOURCE_COMMISSIONING_EVIDENCE` and replace every placeholder with authorized site evidence.

Fill the record only from the exact approved site SLD/wiring drawings, exact device manuals and observations made on the real equipment.

Every required channel must identify its semantic purpose, signal provenance, manufacturer/model/manual revision, site drawing reference, exact hardwired terminal/input or exact Modbus endpoint/Unit-ID/function/address/mask-value, active/inactive raw values, validated timing authority, and related meter evidence where applicable. Qualified and authoritative `not_supported` channel records must repeat the exact site ID, configuration identity and channel-map digest so evidence from another site cannot be pasted into the package.

## Physical proof required per qualified channel

A `pass` channel must include:

- the exact site/config/channel-map binding;
- start/end timestamps;
- a real physical before/after state change;
- a real raw before/after change;
- a corresponding runtime before/after state change;
- immutable toggle evidence reference + SHA-256;
- HMI/API observation reference;
- stale/missing evidence test proving the observed fail-closed state, with immutable evidence digest;
- recovery evidence proving authority did not return early;
- observed recovery dwell at least equal to the site-authorized configured `recovery_ms` value;
- exact persisted configuration/channel-map readback with immutable evidence digest;
- an explicit PASS reason.

The validator compares the submitted observed dwell against the submitted authorized configured recovery value. It does not invent a new timing threshold.

## Meter evidence

Meter power may corroborate electrical direction but the record must explicitly keep `power_sign_used_as_source_authority` false. A meter sign or kW threshold cannot replace breaker/ATS/run/synchronism evidence.

For an applicable meter, record meter role, CT/PT polarity evidence, datatype, word order, scale, raw value, scaled kW, sign convention, known physical direction, independent reference and whether the sign was proven. The validator independently checks that `raw × scale == scaled_kw` for the submitted sample. Where the meter has a communication endpoint, record the exact endpoint and Unit ID; if not applicable, state why.

## Unsupported synchronism

If synchronized Grid+Generator operation is physically impossible, record the synchronism channel as `not_supported`, `required: false`, with the authoritative topology/interlock reference and the same exact site/config/channel-map binding. Do not fabricate a synchronized PASS.

## Configuration acceptance

Before automatic control can be considered for later release gates, the executed record must prove exact persisted readback, that unqualified channels block automatic control, stale/missing/conflicting evidence fails closed, physical toggles map the expected source mode, and contradictory power sign cannot override authoritative contacts. The configuration-acceptance record must repeat the exact site/config/channel-map identity and carry an immutable evidence digest.

## Validation

Run against the independently frozen identity:

```bash
python3 tools/site_source_commissioning_verify.py \
  site-source-commissioning.json \
  --expected-firmware-sha <exact-flashed-sha> \
  --expected-artifact-digest sha256:<exact-artifact-digest> \
  --expected-site-id <exact-site-id> \
  --expected-config-identity <exact-config-identity> \
  --expected-site-sld-digest sha256:<approved-sld-file-digest> \
  --expected-channel-map-digest sha256:<approved-channel-map-digest> \
  --json
```

The command returns non-zero for wrong identity/site/config/SLD/channel map, missing or guessed provenance, incomplete mapping, unqualified required channels, missing physical toggle/stale/recovery/persistence proof, meter scaling/sign inconsistency, recovery earlier than the authorized configured dwell, pasted channel evidence, or incomplete configuration acceptance.

A validator PASS means only that the submitted record satisfies the evidence contract and is bound to the expected site identity. It does not create observations that were not physically made, does not prove undocumented wiring/registers, does not authorize automatic control, and does not replace authorized physical commissioning or safety review.
