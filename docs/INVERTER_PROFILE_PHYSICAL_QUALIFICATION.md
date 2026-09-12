# Production inverter profile physical qualification

This is the evidence handoff for Issue #82. It is intentionally stricter than generic Modbus support: a profile is qualified only for one exact manufacturer/model/inverter-firmware/manual/profile/controller/connection identity and only to the stage actually proved on physical equipment.

## Promotion stages

1. `documented` — exact official manufacturer manual, exact model and inverter-firmware applicability identified. No write permission.
2. `read_only_qualified` — physical identity, telemetry, status and fault mappings verified against the exact unit. No write permission.
3. `write_qualified` — controlled command, measured readback, safe-zero, timeout/exception handling, reconnect identity, failure and rollback behavior physically proved. This is still not production approval.
4. `production_approved` — authorized signed record confirms the same exact identity and bench evidence; only this stage may support a later governed code change that enables production writes.

The record must use the corresponding `evidence_state`:

- `DOCUMENTED_MANUFACTURER_EVIDENCE`
- `EXECUTED_PHYSICAL_READ_ONLY_EVIDENCE`
- `EXECUTED_PHYSICAL_WRITE_EVIDENCE`
- `EXECUTED_PRODUCTION_APPROVAL_EVIDENCE`

## Freeze the identity before qualification

The validator must be invoked with independently selected expected values. Do not copy these values from the evidence JSON at validation time. Freeze them from the approved commissioning/engineering package first:

- manufacturer;
- exact model;
- exact inverter firmware;
- exact profile source SHA;
- exact controller firmware SHA and controller artifact SHA-256;
- official manufacturer manual revision and downloaded document SHA-256;
- connection topology;
- endpoint;
- Unit ID.

A record that is internally self-consistent but belongs to a different device, manual, controller build, endpoint or Unit ID must fail.

## Manufacturer-map rules

- Use an official/current manufacturer protocol manual applicable to the exact model and inverter firmware. Record the downloaded manual file SHA-256 and its revision/date. A distributor, forum, generic family map or plausible adjacent-model register map is not sufficient.
- Every register-map entry must repeat the exact manual revision and manual document SHA-256. Mixing mappings from another manual revision fails qualification.
- Do not infer status from a plausible address. Correlate the documented status/fault value against real physical operating-state changes.
- Identity probing must be bound to the frozen endpoint and Unit ID.

## Physical read-only stage

Capture start/end timestamps, raw and decoded identity, and the separately observed manufacturer, model and inverter-firmware values. Those observed values must exactly match the frozen identity; a boolean `identity_matches=true` cannot substitute for the recorded values. Also capture telemetry cross-checks and physical status/fault correlation. Store an immutable evidence-package reference and SHA-256. No write may be attempted during this stage.

## Physical write stage

Keep automatic control disabled and use an authorized bench/site condition where the requested command cannot create unsafe export or source interaction. Record:

- start/end timestamps;
- immutable evidence-package digest;
- requested engineering and raw values;
- actual transmitted-command reference;
- measured engineering and raw readback values;
- documented readback tolerance;
- safe-zero evidence;
- timeout fail-safe evidence;
- Modbus exception fail-safe evidence;
- rollback/failure-path evidence with original and restored measured values;
- reconnect identity revalidation and proof that stale identity blocks write authority.

The requested raw command must remain inside the exact manufacturer-documented raw range. The validator independently checks measured command/readback and rollback differences against the documented tolerance; a boolean `pass` alone is not sufficient. Write qualification cannot start before read-only qualification has completed.

## Production approval stage

The signed approval record must be created after the write evidence is complete and must carry its own SHA-256. It must repeat the exact manufacturer, model, inverter firmware, profile SHA, controller SHA/artifact digest, manual revision/document digest, topology, endpoint and Unit ID. The approver must explicitly confirm that no identity or mapping changed after the bench evidence.

The approval record authorizes the qualified profile identity only. It does **not** silently modify firmware or enable runtime writes. A separate governed code PR is still required to change the corresponding runtime profile from fail-closed/non-production to production-write-enabled.

## Start fail-closed

```bash
cp tools/inverter_profile_physical_evidence.example.json inverter-profile-evidence.json
```

The example deliberately fails until real manufacturer and bench evidence replaces its placeholders.

## Validate

Example for a final production approval record:

```bash
python3 tools/inverter_profile_physical_evidence_verify.py \
  inverter-profile-evidence.json \
  --expected-stage production_approved \
  --expected-manufacturer 'EXACT_MANUFACTURER' \
  --expected-model 'EXACT_MODEL' \
  --expected-inverter-firmware 'EXACT_INVERTER_FW' \
  --expected-profile-sha '<40-hex-profile-source-sha>' \
  --expected-controller-sha '<40-hex-controller-firmware-sha>' \
  --expected-controller-artifact-digest 'sha256:<64-hex-controller-artifact-digest>' \
  --expected-manual-revision 'EXACT_MANUAL_REVISION' \
  --expected-manual-digest 'sha256:<64-hex-downloaded-manual-digest>' \
  --expected-topology direct_tcp \
  --expected-endpoint '192.168.10.30:502' \
  --expected-unit-id 1 \
  --json
```

Use the same external identity-lock arguments for `documented`, `read_only_qualified` and `write_qualified`, changing only `--expected-stage` to the stage actually being reviewed.

A passing validator proves only completeness, exact-identity binding, chronology and internal consistency of the supplied evidence. It cannot create physical observations, manufacturer authority, a signature, or a production write permission that does not exist.
