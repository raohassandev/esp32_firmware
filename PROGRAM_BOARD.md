# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge. This board records only evidence-backed state; software CI never substitutes for physical acceptance.

Current `dev` snapshot: `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258` (PR #119 merged).

| Lane | Scope | Coder / Owner | QA | State | Primary issue / PR | Dependencies |
|---|---|---|---|---|---|---|
| L0 | governance v2 reconciliation | ChatGPT | diff review / GitHub Actions | COMPLETE / service continues in L8 | #89 / PR #97 | none |
| L1 | Modbus TCP connection modes + bounded endpoint admission | ChatGPT | GitHub Actions | COMPLETE/MERGED | #88/#78; PR #99; PR #114 | physical endurance #83 |
| L2 | Generator source-transition release gate | ChatGPT integration + site physical | GitHub Actions + physical evidence | SOFTWARE GREEN / BENCH BLOCKED | #80 / Draft PR #106 | real bench/source evidence |
| L3 | Waveshare soak/parity/persistence/promotion | site physical + ChatGPT integration | GitHub Actions + observed hardware evidence | SHORT PASS / FINAL SOAK INCOMPLETE | #87, #24-#27 / PR #57, #20 | uninterrupted >=4 h same-image evidence |
| L4 | Secure OTA release qualification | ChatGPT + site physical | GitHub Actions | SOFTWARE GREEN / PHYSICAL BLOCKED | #86/#50 / Draft PR #52 | accepted Waveshare release baseline |
| L5 | Real site source commissioning | ChatGPT + site engineer | ChatGPT + physical QA | BLOCKED EXTERNAL | #81 | manuals/wiring/site evidence |
| L6 | Production inverter profiles | ChatGPT + Owner manuals + site bench | GitHub Actions + safety review | GENERIC CORE HARDENED / MANUFACTURER QUALIFICATION BLOCKED | #82; PR #102/#108/#117/#119 | official manuals + physical read/write/readback/rollback |
| L7 | Integrated FAT/SAT/endurance | ChatGPT plan + site physical | GitHub Actions + Owner signoff | WAITING ON PHYSICAL/PROFILE QUALIFICATION | #83 | L2/L3/L4/L5/L6 |
| L8 | continuous governance reconciliation | ChatGPT | diff review / GitHub Actions | CONTINUOUS | #84 | all lanes |
| L9 | Rev-A PCB/KiCad product hardware | ChatGPT + hardware engineer | ERC/DRC/review | SEPARATE TRACK | #85 / PR #18/#19 | Owner milestone decision |
| L10 | final served-browser poller audit | ChatGPT | GitHub Actions | COMPLETE/CLOSED | #90 | none |
| L11 | evidence traceability | ChatGPT | CI + physical sources | CONTINUOUS | #91 | all evidence lanes |
| L12 | requirements closure matrix | ChatGPT | independent review | COMPLETE/CLOSED | #92 / PR #103 | continuous updates through L8/L11 |
| L13 | promotion graph / stale PR hygiene | ChatGPT | exact-tree + CI | CONTINUOUS | #93 | merges/physical acceptance |
| L15 | held Phase-1 reconciliation | ChatGPT | GitHub Actions | BLOCKED BY L3 | #95 / PR #52/#54 | accepted Waveshare graph |

## Recent governed software closure

- PR #99 — configurable Modbus connection modes; merge `3aa69162a6847a852e9b648ef8ec6988f5e3f296`.
- PR #100 — remove compiled site STA defaults / keep provisioning opt-in; merge `7d9abdccb032b387525df9240b2943b74324a8e9`.
- PR #102 — inverter reconnect/stale identity revalidation; merge `b60bcb9474f4ed7dd0b0ed631410b54628a47d01`.
- PR #103 — live requirements reconciliation; merge `81b02a4b6188b9f7150d9161883f0465e14ba6f5`.
- PR #105 — served Engineering DOM/error stability regression; merge `6569e36e019adf4b890d04f163091f04eec6020b`.
- PR #107 — web spinlock/nonblocking regression gate; merge `6ae86b09294b3e3a2c8a8eaf085ce5c35c69cf74`.
- PR #108 — inverter command width/scale/range/FC06/FC16 safety; merge `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d`.
- PR #114 — Modbus endpoint admission rejects hostname/DNS paths that escape the cumulative transaction deadline; merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f`.
- PR #117 — automatic control is persisted disabled before inverter profile assignment persistence; merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6`.
- PR #119 — production inverter write authority requires complete live evidence and fresh ON_GRID status for positive commands while preserving fail-safe zero; merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`.

Stale replay PRs #115/#116/#118 are closed and are not release-evidence sources.

## Current executable sets

### Software / governance
- Keep Draft PR #106 unchanged until #80 physical source-transition qualification. It is a held physical candidate, not a software work queue.
- Keep Draft PR #52 and PR #54 held until the accepted Waveshare source graph is known.
- Continue live audit/reconciliation through #84/#91/#93; open runtime PRs only for a genuine current-source gap.

### Physical / external
- #87/#27: exact `87841ece...` Waveshare image needs one uninterrupted >=4 h / >=240-sample run. First attempt reached ~2 h / 121 clean samples then the USB dock/power path disappeared; partial runs cannot be added together.
- After that PASS, execute #25 backend parity/recovery and #26 persistence/ARM on the same accepted identity before promotion.
- #80: source-transition bench matrix for Draft PR #106.
- #81: real breaker/run/ATS/synchronism provenance, polarity and meter sign/scaling.
- #82: official manufacturer manuals and per-model physical identity/status/write/readback/rollback qualification.
- #86: OTA interruption/power-loss/rollback qualification on the intended accepted release baseline.
- #83: integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance and signed SAT.

## Execution policy

1. Keep no more than 2–3 CI-active software PRs.
2. Before every merge: re-fetch live target, require exact current PR head, 0-behind target, required exact-head CI GREEN and expected-head merge guard.
3. If target advances, replay validated non-overlapping work onto current target rather than merging stale branches.
4. No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production inverter write from an unqualified profile.
5. Physical evidence is valid only for its exact source/artifact/config/profile identity.
6. The project is not 100% complete until every release physical/external gate in #79 is satisfied.
