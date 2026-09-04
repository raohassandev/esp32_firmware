# Integrated FAT / endurance / SAT execution

Issue: #83. Parent program: #79.

This package is the AISH-OS final release-evidence gate. It validates evidence records; it does not operate plant equipment, perform OTA, create physical PASS, or replace an authorized site executor.

## Preconditions

Use one immutable intended release identity throughout the integrated run. Before FAT/SAT, the evidence record must reference genuine PASS evidence for:

- Waveshare final acceptance and #25/#26 backend/persistence/ARM;
- generator source-transition physical qualification #80;
- real site source commissioning #81;
- all production inverter profiles used by the site #82;
- secure OTA physical qualification #86.

Any changed firmware behavior, configuration, site source map, or approved inverter profile set requires disposition/requalification before SAT.

## Safe execution order

1. Freeze firmware SHA, artifact/hash, configuration, source mapping and approved inverter profile set.
2. Execute Grid FAT scenarios: zero export, limited export, minimum import, load rise/rejection, meter stale/loss/recovery, inverter loss/recovery.
3. Execute Generator FAT scenarios: single/multiple generators where applicable, minimum loading, reserve/reverse-power protection, load rejection, generator-meter stale/loss, run/breaker conflicts.
4. Execute mixed-source scenarios: Grid -> Transfer -> Generator, Generator -> Transfer -> Grid, island/no-source, conflict/stale/recovery dwell, and synchronization only where the authoritative topology supports it.
5. Exercise all three Modbus connection modes independently: per-transaction, persistent, reconnect-on-error. Include slow/dead slaves, exceptions, TCP reset/reconnect, gateway restart, repeated connect/close and simultaneous multi-device load while recording lwIP/socket/resource trends and unrelated-service responsiveness.
6. Reference the complete secure-OTA physical matrix from #86 for the same intended release identity.
7. Record endurance summary and fatal counters.
8. Obtain authorized SAT acceptance tied to the exact firmware/config/profile identity.
9. Validate the final record without lowering thresholds or substituting CI/simulator results for physical evidence.

## Validator

```bash
python3 tools/integrated_fat_sat_evidence_verify.py evidence.json --json
```

A release-ready record must be internally consistent, have zero fatal/reset/resource-collapse counts, bind all prerequisites and scenarios to one release identity, and contain signed/authorized SAT evidence. The example file is intentionally incomplete/unqualified and must fail closed.

## AISH-OS ownership

- Orchestrator / evidence contract: ChatGPT release lane.
- Physical execution: authorized site/bench operator.
- Regression QA: GitHub Actions.
- Product/site acceptance: authorized representative.

No guessed topology, register, polarity, profile approval or physical PASS is permitted.