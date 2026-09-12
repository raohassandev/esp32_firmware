# Integrated FAT / endurance / SAT execution

Issue: #83. Parent program: #79.

This package is the AISH-OS final release-evidence gate. It validates evidence records; it does not operate plant equipment, perform OTA, create physical PASS, or replace an authorized site executor.

## Preconditions

Use one immutable intended release identity throughout the integrated run. Before FAT/SAT, the evidence record must reference genuine PASS evidence for:

- Waveshare final acceptance and required post-soak/operator-continuity evidence;
- generator source-transition physical qualification #80;
- real site source commissioning #81;
- all production inverter profiles used by the site #82;
- secure OTA physical qualification #86.

Freeze and independently record the exact final release identity before execution:

- firmware source SHA;
- firmware tree SHA;
- firmware artifact digest;
- configuration identity;
- site ID;
- site source-map digest;
- approved inverter-profile manifest digest and signed profile-manifest reference.

Any changed firmware behavior, configuration, site source map, or approved inverter profile set requires disposition/requalification before SAT.

## Safe execution order

1. Freeze firmware SHA/tree/artifact, configuration, site source mapping and approved inverter profile set.
2. Execute Grid FAT scenarios: zero export, limited export, minimum import, load rise/rejection, meter stale/loss/recovery, inverter loss/recovery.
3. Execute Generator FAT scenarios: single/multiple generators where applicable, minimum loading, reserve/reverse-power protection, load rejection, generator-meter stale/loss, run/breaker conflicts.
4. Execute mixed-source scenarios: Grid -> Transfer -> Generator, Generator -> Transfer -> Grid, island/no-source, conflict/stale/recovery dwell, and synchronization only where the authoritative topology supports it.
5. Exercise all three Modbus connection modes independently: per-transaction, persistent, reconnect-on-error. Include slow/dead slaves, exceptions, TCP reset/reconnect, gateway restart, repeated connect/close and simultaneous multi-device load while recording lwIP/socket/resource trends and unrelated-service responsiveness.
6. Reference the complete secure-OTA physical matrix from #86 for the same intended release identity.
7. Record endurance summary and fatal counters.
8. Obtain authorized SAT acceptance tied to the exact firmware/tree/artifact/config/site-map/profile-manifest identity.
9. Validate the final record against the independently frozen identity without lowering thresholds or substituting CI/simulator results for physical evidence.

## Per-scenario evidence

Every physical scenario must repeat the exact frozen release identity and record:

- firmware SHA/tree/artifact digest, configuration identity, site ID, site-map digest and profile-manifest digest;
- endpoint/Unit-ID map, meter-role map and source-contact provenance references;
- timestamps and the exact scenario stimulus/step;
- measured source/meter values and explicit command/readback values;
- expected fail-closed/authority state versus observed state;
- serial/runtime logs and relevant HMI/HTTP evidence;
- WDT/panic/NO_MEM/reset/resource-collapse counters;
- explicit PASS/FAIL reason. No inferred PASS is allowed.

Where synchronized Grid+Generator operation is not supported by the qualified topology, only the dedicated synchronism scenario may use `not_supported`, with authoritative topology reference and factual reason. Its exact release identity is still mandatory.

## Signed SAT evidence

The signed SAT record must repeat the exact firmware SHA/tree/artifact, configuration, site ID/site-map digest and approved-profile-manifest digest, reference the signed site source map and approved profile manifest, and include a SHA-256 digest of the signed SAT record. A later behavior-affecting firmware/runtime/config/profile/site-map change invalidates dependent evidence.

## Validator

Supply the independently frozen identity on every validation run:

```bash
python3 tools/integrated_fat_sat_evidence_verify.py evidence.json \
  --expected-firmware-sha <40-hex-source-sha> \
  --expected-firmware-tree-sha <40-hex-tree-sha> \
  --expected-artifact-digest sha256:<64-hex-artifact-digest> \
  --expected-config-identity <exact-config-identity> \
  --expected-site-id <exact-site-id> \
  --expected-site-map-digest sha256:<64-hex-site-map-digest> \
  --expected-profile-manifest-digest sha256:<64-hex-profile-manifest-digest> \
  --json
```

A release-ready record must match every external identity lock, have zero fatal/reset/resource-collapse counts, bind all prerequisites and scenarios to one release identity, and contain signed/authorized SAT evidence. The example file is intentionally incomplete/unqualified and must fail closed.

## AISH-OS ownership

- Orchestrator / evidence contract: ChatGPT release lane.
- Physical execution: authorized site/bench operator.
- Regression QA: GitHub Actions.
- Product/site acceptance: authorized representative.

No guessed topology, register, polarity, profile approval or physical PASS is permitted.
