# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge; update this board through L8/#84 after state changes.

Current `dev` snapshot: `430e9157eb82196501f896d9323da16c86f9255e` (AISH-OS v2 PR #97 merged).

| Lane | Scope | Coder / Owner | QA | State | Primary issue / PR | Dependencies |
|---|---|---|---|---|---|---|
| L0 | governance v2 reconciliation | ChatGPT | diff review / GitHub Actions | COMPLETE | #89 / PR #97 | none |
| L1 | Modbus TCP connection modes | ChatGPT | GitHub Actions | EXECUTING; reconcile WIP to current dev before PR | #88, #78 | none |
| L2 | Generator source-transition release gate | ChatGPT integration + Claude physical | GitHub Actions + physical evidence | SOFTWARE GREEN / BENCH BLOCKED | #80 / PR #77 | real bench/source evidence |
| L3 | Waveshare soak/parity/persistence/promotion | Claude physical + ChatGPT integration | GitHub Actions | SHORT PASS / FINAL SOAK INCOMPLETE | #87, #24-#27 / PR #57, #20 | uninterrupted >=4 h same-image evidence |
| L4 | Secure OTA release qualification | ChatGPT + Claude physical | GitHub Actions | SOFTWARE GREEN / BLOCKED | #86, #50 / PR #52 | L3 source graph |
| L5 | Real site source commissioning | ChatGPT + site engineer | ChatGPT + GitHub Actions | BLOCKED PARTIAL EXTERNAL | #81 | manuals/wiring/site evidence |
| L6 | Production inverter profiles | ChatGPT + Owner manuals + Claude bench | ChatGPT + GitHub Actions | BLOCKED EXTERNAL | #82 | official manuals + bench |
| L7 | Integrated FAT/SAT/endurance | ChatGPT plan + Claude physical | GitHub Actions + Owner signoff | WAITING | #83 | L1, L2, L5, L6 |
| L8 | continuous governance reconciliation | ChatGPT | diff review | CONTINUOUS | #84 | all lanes |
| L9 | Rev-A PCB/KiCad product hardware | ChatGPT + hardware engineer | DRC/ERC/review | SEPARATE TRACK | #85 / PR #18, #19 | Owner milestone decision |
| L10 | final served-browser poller audit | ChatGPT | GitHub Actions | READY PARALLEL | #90 | none |
| L11 | evidence traceability | ChatGPT | CI + physical sources | CONTINUOUS | #91 | all evidence lanes |
| L12 | requirements closure matrix | ChatGPT | independent review | READY PARALLEL | #92 | live code/evidence |
| L13 | promotion graph / stale PR hygiene | ChatGPT | exact-tree + CI | CONTINUOUS | #93 | merges/physical acceptance |
| L14 | live-state monitoring cadence | ChatGPT | live GitHub/physical sources | CONTINUOUS PER CYCLE | #94 | none |
| L15 | held phase1 work reconciliation | ChatGPT | GitHub Actions | BLOCKED | #95 / PR #52, #54 | L3 |
| L16 | stale TODO/evidence cleanup | ChatGPT | diff review | COMPLETE | #96 / PR #97 | live state |

## Execution policy

1. Keep 2–3 CI-active software PRs maximum.
2. Keep blocked physical work moving independently through Claude/site executor; never stop software lanes waiting for it.
3. Before every merge: re-fetch live target, require exact current PR head, 0-behind target, fresh required CI GREEN, and expected-head merge guard.
4. If target advances, replay validated non-overlapping work onto current target rather than merging stale branches.
5. No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production inverter write from an unqualified profile.
6. Physical evidence is valid only for its exact source/artifact identity.
7. QA does not silently repair coder work; failures return to the implementation lane.

## Next software concurrency set

- **Slot A — L1:** finish Modbus modes; current WIP branch was based on pre-PR97 dev and must be reconciled onto current dev before final PR/merge.
- **Slot B — L10:** final served-browser audit; create CI-active PR only if a genuine active-runtime gap exists.
- **Slot C:** keep free unless a non-overlapping high-value runtime lane is ready; governance/docs should not crowd out runtime CI.

Read-only audits L11/L12/L13/L14 run in parallel without consuming CI slots.

## Next physical concurrency set

- **L3:** rerun uninterrupted Waveshare >=4 h same-image soak, then #25/#26 matrices.
- **L2:** prepare/run source-transition bench matrix independently when required bench wiring/evidence is available.
- **L5/L6:** collect manuals/site mappings in parallel so coding/bench qualification can start without waiting for final Waveshare promotion.
