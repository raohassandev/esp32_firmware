# Rollback-safe secure OTA — current `dev`

This slice replays the validated secure OTA implementation from historical PR #52 onto the current `dev` architecture without changing the frozen Waveshare physical candidate or claiming hardware qualification.

## Software behavior

- Streams the application image into the inactive OTA slot with a bounded 4096-byte upload buffer.
- Validates ESP image magic, exact Automatrix product identity, ESP32-S3 chip ID, secure-version monotonicity and inactive-slot capacity before `esp_ota_begin()` can erase/write the update slot.
- Forces automatic control disabled and waits for confirmed safe-zero command state before the first OTA firmware write.
- Aborts interrupted/failed uploads without selecting the partial image as the boot target and verifies that the original boot partition remains selected.
- Calls `esp_ota_end()`, re-reads the staged application descriptor and validates it before selecting the new boot partition.
- Requires an explicit authenticated reboot into a fully validated staged image.
- Keeps a newly booted image pending verification through complete subsystem initialization and a 30-second stabilization window before marking it valid.
- Rolls a failed pending first boot back to the previous valid slot.
- Uses the existing Engineering authentication compile gateway for OTA endpoints.
- Preserves NVS, Wi-Fi credentials and commissioned configuration; no NVS erase or full-flash erase is part of the OTA flow.

## Current web architecture

The OTA API is registered through the current web server and `ota.js` / `ota.css` are embedded into the existing C-composed `/app.js` and `/app.css` bundles. OTA status polling is route- and visibility-aware and uses bounded timeouts; upload progress uses XHR because browser upload progress is required.

## Release boundary

Software CI can prove source ordering, fail-closed guards, bounded buffering, browser lifecycle behavior, exact ESP32-S3 compilation and HTTP route capacity. It cannot prove interruption or rollback on a real controller.

Issue #86 remains mandatory before production release: authenticated upload, wrong-image rejection, interrupted upload, power loss during update, partial-image non-selection, previous-slot recovery, staged reboot, pending-verification first boot, mark-valid stabilization, deliberate rollback and fail-closed control must all be physically demonstrated on the intended release identity. No NVS/full erase is allowed during that matrix.
