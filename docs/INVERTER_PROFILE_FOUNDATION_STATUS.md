# Inverter profile foundation status

Branch: `feature/multibrand-inverter-profiles`

## Implemented

- Compact profile catalogue API in the inverter manager component.
- Manufacturer/model-family catalogue entries for Custom, Huawei SUN2000, GoodWe commercial, Solis commercial and FoxESS/Knox commercial families.
- Explicit qualification lifecycle from documented through production approved.
- Central read eligibility and write eligibility functions.
- Hard write rule: a profile must contain both a command and readback mapping and be production approved.
- All manufacturer profiles remain write-locked pending exact manual extraction and physical qualification.
- Source-contract regression test.
- ESP-IDF CI compilation of the catalogue.

## Next implementation slice

1. Inventory exact PDF/manual filenames and revisions from `SolTrix/Manuals`.
2. Extract exact manufacturer/model register maps with evidence.
3. Replace pending catalogue entries with exact model-family profiles.
4. Add `profile_id` to inverter configuration with backward-compatible migration.
5. Add a read-only `/api/inverter-profiles` endpoint.
6. Add manufacturer/model picker and advanced custom mode in the web UI.
7. Add safe read-only connection/identity test.
8. Integrate profile-driven command encoding and readback while retaining the production approval gate.

No live inverter behavior was enabled by this foundation change.
