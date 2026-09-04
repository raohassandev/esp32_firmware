# AISH-OS Blocker Ledger v3

Master program: #79. Current software baseline: `dev` `1b4d7631862afdb38da99fbbae9aa170729b0bdb` after PR #160. Software/evidence automation for every current release physical gate is now present; remaining release blockers are execution/evidence, not missing validator infrastructure.

## B-001 — Waveshare uninterrupted final acceptance

**Lane:** L3 / #87/#27  
**Exact candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / >=4 H CONTINUOUS SOAK PENDING  
**Automation:** PR #159 merged as `3fd831b677ff590c54cb5cef412a55c9cdea5ca8`

Prior run reached ~2 h / 121 samples plus 25 clean backend rounds before USB dock/power disappearance; partial runs are not additive. Obtain one new uninterrupted >=4 h / >=240-sample run on the same image. Human visual/touch observation remains required; capture tooling cannot infer it.

## B-002 — Waveshare backend parity + persistence/ARM

**Issues:** #25/#26  
**State:** BLOCKED UNTIL B-001 PASS  
**Automation:** PR #150

After B-001, execute backend parity/recovery and save/readback/reboot/restore/interrupted-save/ARM matrices on the same accepted identity.

## B-003 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING  
**Automation:** PR #151

Physical Grid<->Transfer<->Generator, island, supported sync, stale/conflict/source-loss, source-contact evidence, meter sign/scaling and recovery dwell must pass. Then replay identical runtime behavior to latest `dev` and re-earn CI before merge.

## B-004 — Real site source commissioning

**Lane:** L5 / #81  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING  
**Automation:** PR #156 merged as `1c6e1de9ba01c759bc7dc6331f418160614cbbd7`

Need exact breaker/run/ATS/sync provenance, manual/wiring reference, terminal/register/address/mask/polarity, physical before/after toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. kW sign cannot manufacture source authority.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** EXACT OFFICIAL DOCUMENT + MODEL-SPECIFIC BENCH PENDING  
**Automation:** PR #158 merged as `56e2abfb9291b8b5f0786dc8051820a53865984b`

Generic write safety is complete, but each deployed inverter model still requires exact official manual/model/firmware applicability, identity/telemetry/status proof, command/readback/tolerance/failure/rollback bench evidence and signed production approval. Wrong-family or guessed register maps remain forbidden.

## B-006 — Secure OTA physical qualification

**Lane:** L4 / #86/#50  
**State:** REAL CONTROLLER MATRIX PENDING AFTER INTENDED RELEASE IDENTITY FREEZE  
**Automation:** PR #152

Execute authenticated upload, invalid rejection before write, interruption, power loss, partial-image non-selection, previous-slot recovery, explicit reboot, pending verification, mark-valid, deliberate rollback, fail-closed control and NVS persistence on one immutable intended OTA release identity.

## B-007 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**State:** BLOCKED BY B-001..B-006 PREREQUISITES  
**Automation:** PR #160 merged as `1b4d7631862afdb38da99fbbae9aa170729b0bdb`

Final execution requires complete Grid, Generator and mixed-source FAT; all three Modbus modes; slow/dead/exception/reset/reconnect/gateway/Wi-Fi/multi-device endurance; resource trends; zero fatal/reset/resource-collapse counters; and authorized signed SAT tied to exact release SHA/config/profile/source-map identity.

## B-008 — Rev-A H4 prototype

**Lane:** L9 / #85  
**State:** H2/H3 AUTOMATED PASS / H4 PHYSICAL PROTOTYPE PENDING  
**H2/H3 evidence:** run `33797012638`; provider artifact id `9909976209`, digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`

Design routing/DRC/SI/STEP/manufacturing export/provider package are complete. Remaining Rev-A work is fabrication/assembly and physical 12/24 V, protection, USB/backfeed, rail, Ethernet, dual-RS485, HMI, relay, enclosure-fit, thermal and required environmental/EMC validation. This separate track does not block the current Waveshare release unless explicitly coupled.

## Resolved infrastructure/tooling blockers

- Generator physical record infrastructure — PR #151.
- Secure OTA physical record infrastructure — PR #152.
- Site source commissioning record infrastructure — PR #156.
- Inverter production qualification record infrastructure — PR #158.
- Waveshare automated soak capture infrastructure — PR #159.
- Integrated FAT/endurance/SAT record infrastructure — PR #160.

No new software patch should be created merely to avoid executing a physical gate. New code is justified only by an observed defect, missing capability or failed current evidence contract.
