# Current Execution TODO — Superseded Pointer

This file is retained only so old links do not break. Its former `feature/multibrand-inverter-profiles` checklist is no longer authoritative and contained many software items that are now complete on `dev`.

## Live execution sources

- root `TODO.md` — current executable/release-blocking queue;
- root `PROGRAM_BOARD.md` — live lane state;
- root `REQUIREMENTS_MATRIX.md` — closure matrix;
- root `BLOCKERS.md` — unresolved blockers;
- root `EVIDENCE_INDEX.md` — exact evidence;
- `docs/RELEASE_EXECUTION_RUNBOOK.md` — operator/release-manager procedure for the remaining physical gates;
- Issue #79 — authoritative program Done definition and dependencies.

Runbook baseline at this update: `dev` = `10d964237a351a5577e19ff023485064a180843f` after PR #200. Live `dev` always overrides this snapshot.

Current remaining release work is physical/external: exact PR #179 Waveshare acceptance, generator source-transition bench evidence on frozen PR #106, real-site source mapping, exact manufacturer inverter qualification, secure OTA physical rollback/interruption qualification, integrated FAT/endurance/signed SAT, and final traceability. Rev-A fabricator DFM/fabrication/H4 proceeds independently unless explicitly coupled to the firmware release.

Do not reopen an item from an old checklist unless current `dev` is inspected first and a live regression is demonstrated. Do not mark a physical gate complete from CI, validators, simulators, manual inventory, or guessed evidence.
