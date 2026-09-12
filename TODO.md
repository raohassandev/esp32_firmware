# AISH-OS Live TODO v6

Master program: #79. Live repository truth overrides this checklist. **Reconciliation parent:** `dev` `8016c005be8d548e9388026a89592b70b92ec1a3` (PR #182 merge). This file deliberately does not claim that parent is the post-reconciliation live head; every execution cycle must fetch live `dev` before acting.

## RELEASE-CRITICAL EXECUTION QUEUE

- [ ] **P0 / L16 / #174 — execute frozen Industrial UI Waveshare candidate.** Exact PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`, artifact `10293685030`, digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`, app SHA256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`. Physically prove native 800x480/touch/roles, Network scan/select/manual/connect/restart, Grid+Gen1..3 source mapping, explicit Transfer/ATS and Sync applicability, alarm filters/sorts/Engineering ACK, real board↔simulator Modbus counter/value evidence, browser/resource health and one uninterrupted >=4 h / >=240-sample run. Transfer/Sync configured => real round-trip PASS required; not configured/supported => roundtrip false plus factual non-empty reason. Do not modify PR #179 candidate before disposition.
- [ ] **P0 / L2 / #80 — Generator source-transition physical bench.** Execute Grid<->Transfer<->Generator/Island/stale/conflict/source-loss/recovery with authoritative breaker/run/ATS/sync and meter sign/scaling evidence using PR #151. After PASS replay exact #106 runtime slice to then-current `dev`, earn fresh exact-head CI and merge 0-behind.
- [ ] **P0 / L5 / #81 — Real site source commissioning.** Execute PR #156 evidence for exact Grid/Gen/ATS/sync channels, address/contact/mask/polarity/manual/wiring provenance, meter mapping/sign/scaling, physical toggle, stale and recovery.
- [ ] **P0 / L6 / #82 — Production inverter qualification.** For every deployed model obtain exact applicable official manual/model/firmware, physically verify identity/telemetry/status, then controlled write/readback/failure/rollback/safe-zero and signed production approval using PR #158. Pending catalogue entries remain fail-closed; no guessed maps.
- [ ] **P0 / L4 / #86 — Secure OTA physical qualification.** On one exact intended final OTA-capable release identity execute authenticated upload, invalid rejection, interrupted upload, power-loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, rollback and NVS persistence using PR #152.
- [ ] **P0 / L7 / #83 — Integrated FAT/endurance/SAT.** After prerequisites pass, execute complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network resets/recovery/resource trends and signed SAT using PR #160.
- [ ] **P0 / #91 — Final release evidence index.** Bind final release SHA/tree/artifact/config/site source maps/approved profiles/UI physical record/OTA/FAT/SAT records; zero critical blockers; close #79 only then.

## SEPARATE HISTORICAL WAVESHARE LANE

- [ ] **L3 / #87/#27 — historical `87841ece...` final soak.** Obtain one uninterrupted >=4 h / >=240-sample run with existing capture tooling. Prior ~2 h / 121-sample interrupted run is not additive and cannot qualify PR #179.
- [ ] **#25/#26 — historical backend parity + persistence/ARM.** Execute only after genuine historical soak PASS on that exact old identity.

## REV-A PRODUCT HARDWARE TRACK

- [ ] **L9 / #178/#85 — new controlled H2 acceptance.** Historical H2 `DRC=0` is not reproducible; fresh replay reported 20 violations. Obtain authoritative component/fabricator evidence for any footprint/manufacturing exception, commit narrowly scoped approved rules before checkpoint, rerun ERC/DRC/unconnected/SI/STEP/provider packaging and mint a new H2 identity. Historical provider artifact `9909976209` is evidence only.
- [ ] **L9 H4 / #162 — fabricate and validate only from new accepted H2/H3 package.** Execute power/protection/USB, Ethernet, dual RS485, HMI serial, relay, enclosure, thermal and required environmental/EMC tests.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation service.** Keep all eight authoritative artifacts synchronized after state changes. A reconciliation file records its known parent baseline, not an unknowable future merge SHA; live `dev` must be fetched every cycle.
- [ ] **L13 / #93 — promotion graph hygiene.** Exact-head CI + zero-behind + expected-head merge; preserve frozen physical candidates.
- [ ] **L14 / #94 — orchestration cycle.** Physical waits do not stop independent software/governance/hardware work.

## COMPLETED SOFTWARE / EVIDENCE AUTHORITY

- [x] Core runtime/config/Modbus/safety/OTA software and always-on regressions through PR #148.
- [x] Industrial UI software chain PR #165/#167/#169/#172/#173/#176.
- [x] PR #179 exact Waveshare Industrial UI candidate software: root + exact board builds GREEN; source commissioning, Network, alarms, auth, rollback and exact packaging complete. Remains Draft because physical #174 is not passed.
- [x] PR #181: #174 evidence authority expanded to Network/source/alarm/real-Modbus surfaces with exact identity-bound unexecuted starter.
- [x] PR #182: eight-file governance reconciliation merged from parent `7cb824a...` to integration merge `8016c005...`.
- [x] Physical/evidence validators for historical Waveshare, source transition, OTA, site source, inverter profiles and integrated FAT/SAT are implemented and fail closed on incomplete evidence.

## GLOBAL DONE GATE

Do not claim 100% until every required physical/site/manufacturer/FAT/SAT gate genuinely passes against exact identities, all behavior-affecting promotions are merged with fresh exact-head CI, final evidence is traceable, and no critical blocker remains. CI and validators enforce evidence quality but cannot create hardware/site PASS.
