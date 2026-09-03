# Automatrix ESP32 PV-DG Controller — Historical Master Execution TODO

> **Superseded execution snapshot. Do not use this file as the live work queue.**
>
> This checklist was created from the `ed765b0` / `feature/multibrand-inverter-profiles` deep-audit baseline. Most software checkboxes it contained have since been implemented, tested and merged on `dev`; leaving the historical unchecked boxes as an active queue would reopen completed work.

## Current authoritative control plane

Use these live sources instead:

- Issue #79 — program-level authoritative dependency graph and global Done definition.
- `PROGRAM_BOARD.md` — current lane state and owners.
- `TODO.md` — current executable/release-blocking work only.
- `REQUIREMENTS_MATRIX.md` — requirement-by-requirement current state.
- `BLOCKERS.md` — current blockers only.
- `EVIDENCE_INDEX.md` — exact SHA/run/artifact evidence.
- `EXECUTION_TREE.yaml` — machine-readable lane/dependency snapshot.

Current software integration snapshot at this archival conversion: `3096f2bfa10e86b3163b99ae7622bffded6791ac` after PR #124.

## Historical scope retained from this document

The original objective remains the same: a fail-closed ESP32-S3 PV-DG controller supporting Solar+Grid, Solar+Generator and combined Grid/Generator source modes; verified Modbus and inverter command/readback; bounded network/browser behavior; and release evidence tied to exact firmware/config/profile identities.

The original deep audit remains at `docs/DEEP_CODE_AUDIT_2026-07-28.md`. Findings from that audit are **historical leads**, not proof of a current defect. A finding may be reopened only after checking current `dev` and demonstrating a live regression.

## Current release boundary

Software hardening is substantially merged, including browser/auth/HTTP safety, Modbus deadline/connection modes, Wi-Fi credential preservation, inverter identity/command/write authority, legacy-config OOM preservation and atomic safety-alarm publication. The remaining release blockers are predominantly physical/external and are tracked in root `TODO.md`/`BLOCKERS.md`:

1. exact Waveshare `87841ece...` uninterrupted >=4 h same-image soak, then backend parity and persistence/ARM;
2. generator source-transition bench qualification for Draft PR #106;
3. real site breaker/run/ATS/synchronism and meter sign/scaling evidence;
4. official manufacturer inverter manuals plus per-model physical write/readback/rollback approval;
5. secure OTA interruption/power-loss/rollback physical qualification on the accepted release baseline;
6. integrated Grid/DG/mixed-source FAT, Modbus/network endurance and signed SAT.

Do not mark any of those physical gates complete from source, simulator or GitHub Actions evidence alone.
