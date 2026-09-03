# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge. Software CI never substitutes for required physical acceptance.

Current `dev` snapshot: `3096f2bfa10e86b3163b99ae7622bffded6791ac` after governed PR #124 merge.

| Lane | Scope | State | Primary evidence / dependency |
|---|---|---|---|
| L0/L8 | Governance and live reconciliation | CONTINUOUS / HEALTHY | #84/#93; PR #120 plus this reconciliation |
| L1 | Modbus TCP connection modes and bounded endpoint admission | COMPLETE/MERGED | PR #99, PR #114; physical endurance #83 |
| L2 | Generator source-transition admission | SOFTWARE GREEN / PHYSICAL BENCH BLOCKED | #80 / Draft PR #106 |
| L3 | Waveshare release | SHORT PHYSICAL PASS / FINAL SOAK INCOMPLETE | #87/#27; exact `87841ece...`; one uninterrupted >=4 h run required |
| L4 | Secure OTA | SOFTWARE GREEN / BASELINE + PHYSICAL BLOCKED | #86/#50 / Draft PR #52; depends on accepted Waveshare graph |
| L5 | Real site source commissioning | BLOCKED EXTERNAL | #81; actual breaker/run/ATS/sync/polarity/meter evidence |
| L6 | Production inverter profiles | GENERIC CORE HARDENED / MANUFACTURER QUALIFICATION BLOCKED | #82; official manuals + bench proof |
| L7 | Integrated FAT/SAT/endurance | PHYSICAL RELEASE GATE PENDING | #83 |
| L9 | Rev-A PCB/KiCad | SEPARATE TRACK | #85 / PR #18/#19 |
| L10 | Served-browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Exact evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 / PR #103 |
| L15 | Held Phase-1 reconciliation | BLOCKED BY L3 | #95 / PR #52/#54 |

## Recent governed software merges

- PR #99 — configurable Modbus connection modes -> `3aa69162a6847a852e9b648ef8ec6988f5e3f296`.
- PR #100 — remove compiled site STA defaults / opt-in provisioning -> `7d9abdccb032b387525df9240b2943b74324a8e9`.
- PR #102 — inverter reconnect/stale identity revalidation -> `b60bcb9474f4ed7dd0b0ed631410b54628a47d01`.
- PR #103 — requirements reconciliation -> `81b02a4b6188b9f7150d9161883f0465e14ba6f5`.
- PR #105 — Engineering DOM/error stability -> `6569e36e019adf4b890d04f163091f04eec6020b`.
- PR #107 — web spinlock/nonblocking regression -> `6ae86b09294b3e3a2c8a8eaf085ce5c35c69cf74`.
- PR #108 — generic inverter command width/scale/range/FC06/FC16 safety -> `b8996d5f834cc8edeb084f56c802ff5fc6ecd04d`.
- PR #114 — Modbus endpoint admission closes unbounded synchronous-DNS path -> `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f`.
- PR #117 — disable automatic control before profile-assignment persistence -> `1360c4a8356ff8acdc19878f65da311c0b0eccc6`.
- PR #119 — complete production write authority / fresh ON_GRID gate -> `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`.
- PR #120 — governance reconciliation through PR #119 -> `1b7cfe57f4c236516e2b5595f544c3524dcead0c`.
- PR #122 — preserve commissioned NVS on legacy migration OOM -> `dfe93de50e2a5715f4d212ff3233d566d36e2cfd`; focused `33738503242`, Wi-Fi `33738503441`, Modbus `33738503220`, full `33738503251` GREEN.
- PR #124 — atomic safety-alarm snapshot publication -> `3096f2bfa10e86b3163b99ae7622bffded6791ac`; focused `33739241779`, full `33739241807` GREEN.

Stale replay PRs #115/#116/#118/#121/#123 are closed and are not current evidence sources.

## Remaining release work

1. Exact Waveshare `87841ece...`: one uninterrupted >=4 h / >=240-sample same-image soak. First attempt was ~2 h / 121 clean samples before USB dock/power loss; partial runs cannot be combined.
2. After that PASS, #25 backend parity/recovery and #26 persistence/ARM on the exact accepted identity, then governed source promotion.
3. #80 source-transition physical matrix for Draft PR #106.
4. #81 real site source mapping and meter sign/scaling provenance.
5. #82 official manufacturer profile documentation and physical read/write/readback/rollback qualification.
6. #86 OTA interruption/power-loss/rollback qualification on the intended accepted baseline.
7. #83 integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance, and signed SAT.

## Execution policy

- Maximum 2–3 CI-active software PRs.
- Before every merge: re-fetch live target, exact current PR head, require 0-behind and fresh exact-head required CI, then use expected-head guard.
- If target advances, replay only validated non-overlapping work onto current target; never merge a behind PR.
- No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production write from an unqualified profile.
- Physical evidence is valid only for its exact source/artifact/config/profile identity.
- Do not call the project 100% complete until every physical/external release gate above is genuinely satisfied.
