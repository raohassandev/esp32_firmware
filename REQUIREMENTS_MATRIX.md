# AISH-OS Requirements Closure Matrix v10

**Reconciliation parent:** `dev` `a86d801cc952ce9ab6032821d41b531d2df343ef` after PR #199 (with PR #198 already merged). This is the known parent before this reconciliation; **live `dev` overrides this document and must be re-fetched before every action.**

| Requirement | Scope | State | Current authority / remaining proof |
|---|---|---|---|
| R-CORE-01 | Core runtime/config/Modbus/safety | COMPLETE | Merged runtime + always-on regression coverage. |
| R-UI-01 | Industrial operator/engineering UI software | COMPLETE | PRs #165/#167/#169/#172/#173/#176; exact Waveshare candidate PR #179 is software-GREEN. |
| R-UI-02 | Waveshare exact-image physical acceptance | PENDING PHYSICAL | #174 on PR #179 exact image/artifact, including >=4 h / >=240 samples. |
| R-SRC-01 | Generator source-transition runtime | SOFTWARE GREEN / PHYSICAL PENDING | Draft PR #106; #80 physical matrix required. |
| R-SRC-02 | Generator physical evidence integrity | COMPLETE TOOLING | PR #151 + PR #195 exact identity, meter scaling/sign and chronology locks. |
| R-SITE-01 | Real site source commissioning | PENDING PHYSICAL SITE | PR #156 + #194 validator authority; exact wiring/manual/channel/meter proof required by #81. |
| R-INV-01 | Generic inverter framework/write-readback protections | COMPLETE | Static compiled profiles, production write gate, identity/telemetry/readback, stale removal, simulator harness and fail-closed runtime are complete. |
| R-INV-02 | Inverter manual-source inventory | COMPLETE / NON-AUTHORITATIVE | PR #198 inventories immutable SolTrix manual/protocol source SHAs and public discovery boundary; exact deployment applicability still required. |
| R-INV-03 | Compiled-profile assignment backup/restore | COMPLETE | PR #199: 12-channel atomic manifest, exact compiled-definition fingerprint, live+persistent control disable, restart required; no register/qualification/approval import. |
| R-INV-04 | Production inverter model qualification | PENDING EXACT MANUAL + PHYSICAL + SIGNED | PR #158 + #193 evidence authority; #82 per deployed model/firmware. |
| R-OTA-01 | Secure OTA software | COMPLETE | Merged implementation/regression. |
| R-OTA-02 | Exact-release OTA rollback/interruption proof | PENDING PHYSICAL | #86 / PR #152 validator; real controller required. |
| R-FAT-01 | Integrated Grid/DG/Modbus endurance | PENDING PHYSICAL | PR #160 + #192 exact final-release identity validation; #83 after prerequisites. |
| R-SAT-01 | Authorized signed SAT | PENDING | Exact final release identity required. |
| R-REL-01 | Final traceability tooling | COMPLETE TOOLING | PR #188 + #190 + #196 lock complete release identity, lane evidence digests and signed SAT digest. |
| R-REL-02 | Final release evidence population | PENDING GENUINE INPUTS | #91 after #174/#80/#81/#82/#86/#83 pass and zero critical blockers. |
| R-HW-01 | Rev-A H2 CAD/routing reproducibility | COMPLETE | Freeze `a877e5d844af114a6e4386f6294f514288ca5df6`; ERC/DRC/unconnected clean. |
| R-HW-02 | Intended-fabricator DFM/capability | PENDING EXTERNAL | Submit artifact `10300571374`; obtain written acceptance or exact DFM changes. |
| R-HW-03 | Rev-A fabrication | PENDING | Only after accepted DFM. |
| R-HW-04 | Rev-A H4 physical acceptance | PENDING PHYSICAL | PR #187 + #191 evidence tooling; controlled PCB/PCBA lot required. |
| R-WAVE-01 | historical short Waveshare qualification | HISTORICAL EVIDENCE ONLY | Exact old image retains its own short PASS; no transfer to PR #179. |
| R-WAVE-02 | historical uninterrupted >=4 h / >=240 sample soak | NOT COMPLETED / RETIRED SUPERSEDED | Old run ended around 2 h / 121 samples. |
| R-WAVE-03 | historical backend parity + persistence/ARM | NOT COMPLETED / RETIRED SUPERSEDED | Old release graph closed and not a current dependency. |

## Current completion boundary

All known generic software/runtime, inverter manual-source discovery, compiled-profile backup/restore and evidence-validator gaps identified through PR #199 are closed. The project is **not 100% release-complete** because #174, #80, #81, #82, #86, #83 and #91 still require genuine physical/site/manufacturer/signed evidence. Rev-A separately requires intended-fabricator DFM, fabrication and #162 H4.

No CI, source contract, simulator, manual inventory or assignment backup may be promoted to a physical or production PASS. A changed firmware/artifact/config/site/profile identity requires the affected evidence to be dispositioned or requalified.
