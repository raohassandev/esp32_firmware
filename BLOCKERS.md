# AISH-OS Blocker Ledger v4

Master program: #79. Current software baseline: `dev` `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134` after governed PR #176. Software/evidence automation exists for current release physical gates; remaining release blockers are genuine physical/site/manufacturer acceptance or explicitly tracked integration work.

## B-001 — Historical Waveshare uninterrupted final acceptance

**Lane:** L3 / #87/#27  
**Exact candidate:** `87841ecee727fe1d814d4186be8c8c26e4afafb4`  
**State:** SHORT PASS / >=4 H CONTINUOUS SOAK PENDING  
**Automation:** PR #159

Prior run reached ~2 h / 121 samples plus 25 clean backend rounds before USB dock/power disappearance; partial runs are not additive. Obtain one new uninterrupted >=4 h / >=240-sample run on the same image. Human visual/touch observation remains required. This historical PASS cannot be copied to the newer Industrial UI image.

## B-002 — Historical Waveshare backend parity + persistence/ARM

**Issues:** #25/#26  
**State:** BLOCKED UNTIL B-001 PASS  
**Automation:** PR #150

After B-001, execute backend parity/recovery and save/readback/reboot/restore/interrupted-save/ARM matrices on the same accepted historical identity.

## B-003 — Generator transition physical bench

**Lane:** L2 / #80  
**Runtime candidate:** Draft PR #106 head `a1620789235d21b515f9f245f2329fab88b50558`  
**State:** SOFTWARE GREEN / PHYSICAL BENCH PENDING  
**Automation:** PR #151

Physical Grid<->Transfer<->Generator, island, supported sync, stale/conflict/source-loss, source-contact evidence, meter sign/scaling and recovery dwell must pass. Then replay identical runtime behavior to latest `dev` and re-earn CI before merge.

## B-004 — Real site source commissioning

**Lane:** L5 / #81  
**State:** PHYSICAL SITE INPUT/EXECUTION PENDING  
**Automation:** PR #156

Need exact breaker/run/ATS/sync provenance, manual/wiring reference, terminal/register/address/mask/polarity, physical before/after toggle, stale/recovery and meter CT/PT/type/word-order/scale/sign. kW sign cannot manufacture source authority.

## B-005 — Production inverter profiles

**Lane:** L6 / #82  
**State:** EXACT OFFICIAL DOCUMENT + MODEL-SPECIFIC BENCH PENDING  
**Automation:** PR #158

Generic write safety is complete, but each deployed inverter model still requires exact official manual/model/firmware applicability, identity/telemetry/status proof, command/readback/tolerance/failure/rollback bench evidence and signed production approval. Wrong-family or guessed register maps remain forbidden.

## B-006 — Secure OTA physical qualification

**Lane:** L4 / #86/#50  
**State:** REAL CONTROLLER MATRIX PENDING AFTER INTENDED RELEASE IDENTITY FREEZE  
**Automation:** PR #152

Execute authenticated upload, invalid rejection before write, interruption, power loss, partial-image non-selection, previous-slot recovery, explicit reboot, pending verification, mark-valid, deliberate rollback, fail-closed control and NVS persistence on one immutable intended OTA release identity.

## B-007 — Integrated Grid/DG/Modbus endurance and signed SAT

**Lane:** L7 / #83  
**State:** BLOCKED BY PREREQUISITE PHYSICAL GATES  
**Automation:** PR #160

Final execution requires complete Grid, Generator and mixed-source FAT; all three Modbus modes; slow/dead/exception/reset/reconnect/gateway/Wi-Fi/multi-device endurance; resource trends; zero fatal/reset/resource-collapse counters; and authorized signed SAT tied to exact release SHA/config/profile/source-map/UI identity.

## B-008 — Rev-A H4 prototype

**Lane:** L9 / #85/#162  
**State:** H2/H3 AUTOMATED PASS / H2 PR-INTEGRATION FIX ACTIVE / H4 PHYSICAL PROTOTYPE PENDING  
**H2/H3 evidence:** run `33797012638`; provider artifact id `9909976209`, digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`

PR #163's fail-closed H2 provenance check currently rejects post-checkpoint `hardware/kicad/Automatrix_PVDG_RevA.kicad_dru`; that integration-policy defect must be dispositioned without changing accepted routed H2 identity. H4 still requires fabricated-board electrical/communications/relay/enclosure/thermal/environmental acceptance.

## B-009 — Industrial UI v1 exact-image Waveshare acceptance

**Lane:** L16 / #164/#174  
**Software baseline:** PR #176 merge `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134`  
**State:** SOFTWARE COMPLETE / NEW EXACT-IMAGE PHYSICAL HMI ACCEPTANCE PENDING  
**Automation:** PR #175 merge `9a22d56b9749a7689581e2b8f5e92df3c1e58038`

Select one immutable new Industrial UI Waveshare-capable firmware/package identity and physically prove native 800x480 Overview/Grid/Solar/Alarms/Readiness layout, Engineering Commission/Configure/Service workflows, light/dark readability, touchscreen/role behavior, browser/API responsiveness, resource stability and one uninterrupted >=4 h / >=240-sample same-image observation. Do not inherit #87/#27 `87841ece...` evidence and do not treat PR #175 validator PASS as hardware PASS.

## Resolved infrastructure/tooling blockers

- Generator physical record infrastructure — PR #151.
- Secure OTA physical record infrastructure — PR #152.
- Site source commissioning record infrastructure — PR #156.
- Inverter production qualification record infrastructure — PR #158.
- Waveshare automated soak capture infrastructure — PR #159.
- Integrated FAT/endurance/SAT record infrastructure — PR #160.
- Industrial UI exact-image physical record infrastructure — PR #175.
- Browser socket/LRU/PSRAM resilience regression gate — PR #173.

No new software patch should be created merely to avoid executing a physical gate. New code is justified only by an observed defect, missing capability, failed current evidence contract or governed integration defect.
