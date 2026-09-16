# Signed OTA Production Release

## Purpose

The normal `sdkconfig.defaults` build remains suitable for development, CI and bench work. A production controller that may receive firmware over a network should additionally reject unauthorized OTA application images cryptographically.

ESP32-S3 / ESP-IDF supports **signed application verification without enabling hardware Secure Boot**. This project uses that mode for the production OTA profile so network-delivered application images are verified with the same RSA-PSS v2 signature format used by Secure Boot v2, while irreversible eFuse provisioning stays outside the normal software workflow.

This is a software security control. It does **not** claim protection against an attacker with physical write access to flash and it does not replace the physical OTA interruption / rollback qualification gate.

## Security profile

`sdkconfig.signed-ota.defaults` enables:

- `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y`
- `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y`
- `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y`
- build-time signing disabled (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` is not set)
- hardware Secure Boot remains disabled

The build therefore produces a **secure-padded but unsigned** application for controlled remote signing. The private RSA-3072 key is never required by routine GitHub CI and must never be committed to this repository.

## Build the production signing candidate

From an ESP-IDF v6.0.1 environment:

```bash
rm -rf build-signed-ota
idf.py \
  -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.signed-ota.defaults' \
  -DSDKCONFIG=build-signed-ota/sdkconfig \
  -B build-signed-ota \
  set-target esp32s3 build
```

Expected unsigned candidate:

```text
build-signed-ota/automatrix_pvdg.bin
```

The production-profile CI job performs the same compile and proves the generated `sdkconfig` actually contains the signed-app and signed-update options while hardware Secure Boot remains off.

## Key custody

Generate and store the RSA-3072 private signing key in controlled external storage (ideally a dedicated signing host or HSM). Do not put the key in:

- the Git repository;
- GitHub Actions secrets for ordinary CI;
- the firmware filesystem;
- release manifests;
- support bundles.

The initial production application and every future OTA application must use the same trusted signing lineage. Signed-app verification without hardware Secure Boot establishes trust from the signature block of the currently running application to the signature on the update candidate. An unsigned initial application is therefore not a valid starting point for this profile.

## Sign and verify a release

With ESP-IDF exported and the private key stored outside the repository:

```bash
python3 tools/sign_ota_release.py \
  --input build-signed-ota/automatrix_pvdg.bin \
  --key /secure/signing/automatrix-ota-rsa3072.pem \
  --output release/automatrix_pvdg-signed.bin
```

The helper:

1. refuses a private key located inside the repository tree;
2. invokes `idf.py secure-sign-data` so ESP-IDF owns the RSA-PSS v2 signature-block format;
3. verifies the resulting image with `idf.py secure-verify-signature`;
4. rejects a signed image larger than the 0x300000-byte OTA slot;
5. emits a SHA-256 release manifest next to the signed binary;
6. explicitly records that hardware Secure Boot and physical qualification are not being claimed.

Only the **signed** image is eligible for the signed-production OTA profile.

## Runtime visibility

`GET /api/ota/status` reports the compiled security policy:

- `signed_app_required`
- `signed_ota_verification`
- `hardware_secure_boot`
- `app_signature_scheme`

A controller built with the production profile reports signed-app and signed-OTA verification enabled with `rsa-v2`, while hardware Secure Boot remains false. The regular development build reports that cryptographic signed-OTA verification is not enabled.

When signed-update verification is active, the existing OTA flow remains unchanged operationally:

1. authenticate Engineering access;
2. verify product ID, chip target and secure version from the image prefix;
3. force automatic control disabled and prove safe-zero before the first flash write;
4. stream only to the inactive OTA slot;
5. allow ESP-IDF to validate the completed image, including the cryptographic signature;
6. change the boot partition only after validation succeeds;
7. reboot explicitly;
8. use pending-verification and rollback protection on first boot.

An invalid signature is rejected before the boot selection is changed.

## Development versus production

| Build | Signed OTA required | Hardware Secure Boot | Private key in build environment | Intended use |
| --- | --- | --- | --- | --- |
| normal `sdkconfig.defaults` | No | No | No | development, CI, bench diagnostics |
| `sdkconfig.signed-ota.defaults` | Yes | No | No | production candidate before remote signing |
| remotely signed production binary | Yes | No | only on controlled signing host/HSM | initial production flash and OTA distribution |

Hardware Secure Boot can be evaluated later as a separate manufacturing/security decision. It is intentionally not enabled here because first-boot eFuse changes are irreversible and require a dedicated provisioning process.

## Release evidence boundary

Software evidence that may be recorded remotely:

- exact source commit;
- production-profile compile result;
- generated signed-profile `sdkconfig` assertions;
- unsigned and signed image SHA-256 values;
- ESP-IDF signature-verification result;
- runtime `/api/ota/status` policy fields when a controller is available.

Still physical and not to be inferred from CI:

- power interruption during OTA write;
- reset during first-boot verification;
- rollback after failed first boot on intended hardware;
- long-run flash/NVS behavior;
- final production approval.
