# Current Execution TODO — Superseded Pointer

This file is retained only so old links do not break. Its former `feature/multibrand-inverter-profiles` checklist is no longer authoritative and contained many software items that are now complete on `dev`.

## Live execution sources

- root `TODO.md` — current executable/release-blocking queue;
- root `PROGRAM_BOARD.md` — live lane state;
- root `REQUIREMENTS_MATRIX.md` — closure matrix;
- root `BLOCKERS.md` — unresolved blockers;
- root `EVIDENCE_INDEX.md` — exact evidence;
- `docs/RELEASE_EXECUTION_RUNBOOK.md` — operator/release-manager procedure for the remaining physical gates;
- `docs/SOFTWARE_PRODUCT_DEEP_AUDIT_2026-09-15.md` — current software-only product/UI/reporting audit and residual-boundary record;
- Issue #79 — authoritative program Done definition and dependencies.

Software-product baseline at this update: `dev` = `ac436b18f1f5e0437a2ad523c352ac8fac0591c6` after PR #202, with `feature/software-product-consolidation` closing the remaining remote product-shell/reporting audit gaps. Live `dev` always overrides this snapshot.

The remaining release work is physical/external: exact PR #179 Waveshare acceptance, generator source-transition bench evidence on frozen PR #106, real-site source mapping, exact manufacturer inverter qualification, secure OTA physical rollback/interruption qualification, integrated FAT/endurance/signed SAT, and final traceability. Rev-A fabricator DFM/fabrication/H4 proceeds independently unless explicitly coupled to the firmware release.

Remote software work should stop once the software-product-consolidation change is merged with exact-head CI green and zero-behind. Further software changes must be driven by a reproduced defect or by evidence from the remaining physical gates. This does not make the overall product/release 100% complete.

Do not reopen an item from an old checklist unless current `dev` is inspected first and a live regression is demonstrated. Do not mark a physical gate complete from CI, validators, simulators, manual inventory, browser exports, or guessed evidence.