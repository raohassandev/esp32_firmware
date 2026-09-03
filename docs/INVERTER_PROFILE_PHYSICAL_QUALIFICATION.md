# Production inverter profile physical qualification

This is the evidence handoff for Issue #82. It is intentionally stricter than generic Modbus support: a profile is qualified only for one exact manufacturer/model/firmware/manual/profile identity and only to the stage actually proved on physical equipment.

## Promotion stages

1. `documented` — exact official manufacturer manual, exact model and firmware applicability identified. No write permission.
2. `read_only_qualified` — physical identity, telemetry, status and fault mappings verified against the exact unit. No write permission.
3. `write_qualified` — controlled command, readback, safe-zero, timeout/exception handling, reconnect identity, failure and rollback behavior physically proved. This is still not production approval.
4. `production_approved` — authorized signed record confirms exact manual revision, profile source SHA, controller firmware/artifact and bench evidence; only this stage may support a later code change that enables production writes.

## Rules

- Use an official/current manufacturer protocol manual applicable to the exact model and firmware. Record the downloaded/manual file SHA256. A distributor, forum, generic family map or plausible adjacent-model register map is not sufficient.
- Do not infer status from a plausible address. Correlate the documented status/fault value against real physical operating-state changes.
- Keep automatic control disabled during write qualification. Use an authorized bench/site condition where the requested command cannot create unsafe export or source interaction.
- Prove readback tolerance, safe-zero/fallback, timeout/exception behavior, reconnect identity revalidation, stale-identity write blocking, rollback and original-value restoration.
- A passing JSON validator checks completeness/consistency of the evidence record. It cannot create observations that were not physically made and it never performs inverter writes.

## Start fail-closed

```bash
cp tools/inverter_profile_physical_evidence.example.json inverter-profile-evidence.json
```

The example deliberately fails until real manufacturer and bench evidence replaces its placeholders.

## Validate

For a read-only qualification:

```bash
python3 tools/inverter_profile_physical_evidence_verify.py \
  inverter-profile-evidence.json \
  --expected-stage read_only_qualified
```

For final production approval evidence:

```bash
python3 tools/inverter_profile_physical_evidence_verify.py \
  inverter-profile-evidence.json \
  --expected-stage production_approved
```

Only after a genuine `production_approved` evidence record is reviewed should a separate governed code PR change the corresponding runtime profile from fail-closed/non-production to production-write-enabled. The approval record itself does not silently modify firmware write authority.
