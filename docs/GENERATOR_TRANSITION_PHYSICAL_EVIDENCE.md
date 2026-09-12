# Generator/source-transition physical evidence validator

This tooling supports Issue #80. It validates a completed physical evidence record; it does not control a generator, breaker, inverter, ATS or site and it cannot create a physical PASS.

## Freeze one exact physical-test identity

Before executing the matrix, independently freeze the intended test identity from the authorized commissioning package. Do not copy the expected values from the completed evidence JSON at validation time:

- exact flashed firmware SHA;
- exact firmware artifact SHA-256;
- exact site/bench ID;
- exact configuration identity;
- approved topology/SLD/interlock package and SHA-256;
- approved breaker/run/ATS/source-signal map and SHA-256;
- approved Grid/Generator meter map and SHA-256.

The completed physical evidence package must have its own immutable reference and SHA-256. A self-consistent record from a different image, site, config, topology, source map or meter map must fail.

The record must use `EXECUTED_GENERATOR_TRANSITION_PHYSICAL_EVIDENCE`. The supplied example is deliberately marked `UNEXECUTED_TEMPLATE_NOT_PHYSICAL_EVIDENCE` and must fail closed.

## Required real-world authority

The authorized executor must identify the actual source-contact provenance, current site SLD/topology, exact device manuals/wiring drawings, and Grid/Generator meter identity, role, CT/PT polarity, datatype, word order, scale and proven sign. Unknown values remain unqualified. Do not copy a register, mask, polarity or topology assumption from another device/site.

Power sign may corroborate electrical direction but must never be used as the sole breaker/ATS/synchronism/source-authority input.

## Matrix represented by the JSON record

The validator requires records for:

- Grid stable -> Transfer -> Generator stable;
- Generator stable -> Transfer -> Grid stable;
- Island;
- synchronized Grid+Generator when the authoritative topology supports it, otherwise explicit `not_supported` plus exact topology evidence;
- stale evidence;
- conflicting breaker/run evidence;
- transfer asserted;
- source loss/no source;
- generator-meter sign/scaling proof;
- fresh recovery dwell before authority returns.

Every scenario, including an authoritative `not_supported` synchronism row, must repeat the exact firmware SHA, artifact digest, site ID, config identity, topology digest, source-map digest and meter-map digest. This prevents a scenario captured on another image or another wiring/meter map from being pasted into the final matrix.

## Executable scenario evidence

Each executable scenario needs:

- start/end timestamps;
- immutable scenario evidence reference + SHA-256;
- raw source/contact/register values with immutable raw-source evidence reference + SHA-256;
- detected mode history;
- expected and observed authority sequences;
- complete Grid and Generator meter evidence;
- inverter command/readback evidence where a qualified inverter path exists, otherwise an immutable safe-PV observation;
- zero WDT/panic/NO_MEM/unexpected-reset/resource-collapse counts;
- serial/runtime and HMI/HTTP references;
- explicit PASS reason/note.

For each meter sample, record `meter_id`, role, CT/PT polarity reference, datatype, word order, raw value, scale, scaled kW, freshness and sign provenance. The validator independently checks `raw × scale == scaled_kw` for the submitted sample; this is an arithmetic consistency check, not a new plant tolerance.

The generator-meter-sign scenario must also record the known physical direction and an independent reference. Its observed generator kW must agree with the generator-meter value in the same scenario.

## Transition/recovery evidence

`grid_to_generator`, `generator_to_grid` and `recovery_dwell` must begin with authority blocked and end allowed. They must record:

- `authority_returned_early: false`;
- recovery start timestamp;
- recovery end timestamp;
- claimed recovery dwell milliseconds;
- immutable recovery evidence reference + SHA-256.

The recovery interval must lie inside the scenario interval and its measured timestamp duration must be at least the claimed dwell. The validator does not invent a recovery threshold; it only checks that the submitted chronology supports the submitted claim.

Invalid/stale/conflict/transfer/source-loss evidence must remain blocked and the recorded safe-PV request must match the observed safe-PV request.

## Qualified inverter command path

If `qualified_inverter_path` is true, command and readback must be numeric and carry an immutable command evidence digest. No new command/readback tolerance is invented here; model-specific tolerance belongs to the separately approved inverter profile evidence in Issue #82. If the inverter path is not qualified, use the safe-PV observation path with its own evidence reference/digest.

## Use

Copy `tools/generator_transition_physical.example.json` to a new evidence file and fill it only from genuine observations. Validate against the independently frozen identity:

```bash
python3 tools/generator_transition_physical_verify.py generator_transition_physical.json \
  --expected-firmware-sha <exact-flashed-sha> \
  --expected-artifact-digest sha256:<exact-artifact-digest> \
  --expected-site-id <exact-site-or-bench-id> \
  --expected-config-identity <exact-config-identity> \
  --expected-topology-digest sha256:<approved-topology-digest> \
  --expected-source-map-digest sha256:<approved-source-map-digest> \
  --expected-meter-map-digest sha256:<approved-meter-map-digest> \
  --json
```

A validator PASS means only that the supplied record satisfies this fail-closed identity/completeness/consistency contract. It is not an independent hardware observation and does not promote Draft PR #106.

Attach/link the checked record and its raw serial/HMI/manual/wiring/meter evidence in Issue #80 before any promotion of Draft PR #106. If #106 is behind live `dev` after a genuine physical PASS, replay only the validated runtime slice on the then-current base, prove equivalence, obtain fresh exact-head CI, require `behind_by=0`, and merge with an expected-head guard. Do not merge the stale branch directly.
