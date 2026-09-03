# Secure OTA real-controller physical evidence validator

This tooling supports Issue #86. It validates a physical evidence record after an authorized real-controller OTA matrix is executed. It does **not** upload firmware, manipulate partitions, reboot hardware, simulate power loss, or create a physical PASS.

## Identity boundary

The validator is intentionally generic: it does **not** hardcode or inherit the frozen Waveshare `87841ece...` physical candidate. Issue #86 must run on one exact intended **OTA-capable release identity** after the release/source graph is resolved.

At execution time bind the evidence record to all of:

- exact 40-hex release source SHA;
- exact 40-hex Git tree SHA;
- exact immutable `sha256:<64-hex>` package/artifact digest;
- exact `sha256:<64-hex>` application-image digest;
- exact configuration identity;
- previous valid and target OTA partitions;
- partition-table identity;
- rollback-enabled bootloader evidence;
- Engineering-authentication evidence;
- fresh exact-head CI and package-identity evidence.

## Required scenarios

The record must include physical observations for:

1. authenticated valid upload, completed firmware write, staged-image validation and exact staged app digest;
2. invalid image rejection **before firmware write and before boot selection**;
3. interrupted upload without completion/selection;
4. controlled power loss during update with previous valid running partition, boot target and NVS recovery;
5. partial image explicitly not selected;
6. previous valid slot restored as both running partition and boot target;
7. staged image already selected as boot target, followed only by explicit authenticated reboot into the target partition;
8. pending-verification first boot without premature mark-valid;
9. mark-valid only after at least 30 seconds stabilization;
10. deliberate rollback restoring the previous valid running partition and boot target;
11. fail-closed control through OTA uncertainty states;
12. NVS/config persistence with no NVS or full-flash erase.

Every scenario also requires timezone-aware timestamps, running/boot partitions before and after, lifecycle states, blocked expected/observed control state, zero fatal/resource-collapse counters, reboot reason/count, serial/API-HMI references, and a substantive evidence note. Reboot/power-loss scenarios require exact NVS before/after equality.

## Use

Copy `tools/ota_physical_evidence.example.json` to a new evidence file only after the intended release candidate has been frozen and package-verified. The example is deliberately empty and must fail validation.

```bash
python3 tools/ota_physical_evidence_verify.py ota_physical.json \
  --expected-source-sha <exact-release-source-sha> \
  --expected-tree-sha <exact-release-tree-sha> \
  --expected-artifact-digest <exact-package-digest> \
  --expected-app-digest <exact-app-image-digest> \
  --expected-config-identity <exact-config-identity> \
  --json
```

A validator PASS means only that the supplied record is structurally complete and internally consistent with the release-safety contract. Raw serial/API/partition/NVS evidence and operator observations remain authoritative and must be attached or linked in Issue #86. Evidence from a different source, artifact or config cannot be transferred silently.
