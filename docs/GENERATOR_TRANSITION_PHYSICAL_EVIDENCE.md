# Generator/source-transition physical evidence validator

This tooling supports Issue #80. It validates a completed physical evidence record; it does not control a generator, breaker, inverter, ATS or site and it cannot create a physical PASS.

## Required real-world inputs

Before running the matrix, the authorized executor must identify the exact flashed firmware/artifact, site configuration identity, actual source-contact provenance, current site SLD/topology, exact manuals/wiring drawings, and grid/generator meter sign/scaling. Unknown values remain unqualified. Do not copy a register, mask, polarity or topology assumption from another device/site.

Power sign may corroborate electrical direction but must never be used as the sole breaker/ATS/synchronism/source-authority input.

## Matrix represented by the JSON record

The validator requires records for:

- Grid stable -> Transfer -> Generator stable;
- Generator stable -> Transfer -> Grid stable;
- Island;
- synchronized Grid+Generator when the authoritative topology supports it, otherwise explicit `not_supported` plus topology evidence;
- stale evidence;
- conflicting breaker/run evidence;
- transfer asserted;
- source loss/no source;
- generator-meter sign/scaling proof;
- fresh recovery dwell before authority returns.

Every executable scenario needs timestamps, raw source evidence, detected mode history, expected and observed authority sequences, meter evidence, inverter command/readback evidence where a qualified inverter path exists (otherwise a safe-PV observation), zero fatal/resource-collapse counts, and serial/HMI references. Invalid/transition evidence must stay fail-closed. Carrying-source transitions must begin blocked and only return to allowed after a fresh uninterrupted dwell.

## Use

Copy `tools/generator_transition_physical.example.json` to a new evidence file and fill it only from genuine observations. The example is intentionally incomplete and must fail validation.

```bash
python3 tools/generator_transition_physical_verify.py generator_transition_physical.json \
  --expected-firmware-sha <exact-flashed-sha> \
  --expected-artifact-digest <exact-artifact-digest> \
  --json
```

A validator PASS means the supplied record is structurally complete and internally consistent with the safety contract. It is not an independent hardware observation. Attach/link the checked record and its raw serial/HMI/manual/wiring evidence in Issue #80 before any promotion of Draft PR #106. If #106 is behind live `dev` after physical PASS, replay only the validated runtime slice on the then-current base and obtain fresh exact-head CI; do not merge the stale branch directly.
