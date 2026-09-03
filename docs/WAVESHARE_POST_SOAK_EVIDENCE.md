# Waveshare post-soak physical evidence validators

These tools support Issues #25 and #26 after Issue #27 earns a genuine uninterrupted final-soak PASS. They do not observe or control hardware and they do not change the frozen controller image.

Exact frozen qualification identity:

- candidate: `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- artifact: `9843536218` / `waveshare-800x480-87841ece`
- ZIP digest: `sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096`

## Ordering rule

1. First complete Issue #27's one new uninterrupted >=4 h / >=240-sample same-image run.
2. Only after genuine PASS, record and validate backend parity/recovery for Issue #25.
3. Then record and validate persistence/reboot/failure/ARM behavior for Issue #26 on the same exact accepted identity.

Do not mark `final_soak_passed` true before Issue #27 actually passes. Do not lower Issue #27 validator thresholds to manufacture a PASS.

## Backend parity / recovery

Copy `tools/waveshare_backend_parity.example.json` to a new evidence file and fill it only from real observations. Every required check must be explicitly true and must carry a non-empty evidence note. The record also requires a >=30 minute observation interval and references to serial plus HMI/HTTP evidence.

```bash
python3 tools/waveshare_backend_parity_verify.py backend_parity.json \
  --expected-candidate-sha 87841ecee727fe1d814d4186be8c8c26e4afafb4 \
  --expected-artifact-digest sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096 \
  --json
```

A PASS validates record completeness and consistency; it is not an independent hardware observation.

## Persistence / ARM

Copy `tools/waveshare_persistence_arm.example.json` and fill it from the physical save/readback/reboot/interruption/ARM matrix. The validator requires all safety checks plus explicit `save_readback`, `reboot_restore`, `interrupted_save`, and `arm_gate` operation records. NVS erase and full-flash erase must remain absent.

```bash
python3 tools/waveshare_persistence_arm_verify.py persistence_arm.json \
  --expected-candidate-sha 87841ecee727fe1d814d4186be8c8c26e4afafb4 \
  --expected-artifact-digest sha256:89e621034d4c91096fc5d38dd57ac40eeeab34275e4af1fc0461b48575039096 \
  --json
```

The checked JSON and referenced raw logs/captures should be attached or linked in the corresponding physical evidence issue. A later source/config/artifact substitution cannot inherit the PASS silently.
