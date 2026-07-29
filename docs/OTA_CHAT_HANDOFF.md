# Secure Web OTA Handoff

**Updated:** 2026-07-29  
**Branch:** `feature/secure-web-ota`  
**Base:** `dev`

## Current status

The stale OTA branch has been rebased onto the current `dev` branch. The Engineering authentication cookie lifetime fix from `dev` is now present, and the branch is zero commits behind `dev`.

The rebase was executed with the equivalent of:

```bash
git fetch origin
git checkout feature/secure-web-ota
git rebase origin/dev
```

The rebase completed all 26 OTA commits. The final ancestry check reported:

```text
git rev-list --left-right --count origin/dev...HEAD
0 26
```

The branch was then updated with additional OTA hardening and tests.

## Rebase conflict resolution

Two files conflicted during the actual rebase:

1. `sdkconfig.defaults`
2. `.github/workflows/esp-idf-build.yml`

The configuration resolution keeps both required settings:

```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_PVDG_DEFAULT_ZLAN_HOST="192.168.100.200"
```

The workflow resolution keeps the current `dev` checks and adds the OTA JavaScript, OTA source contract, OTA behavior proof, and Engineering HTTP cookie contract.

## Engineering authentication result

The `engineering_auth.c` fix from `dev` is present. The login handler now owns the `Set-Cookie` buffer until the HTTP response is sent. The session token is 32 random bytes encoded as 64 lowercase hexadecimal characters.

An executable host HTTP contract now verifies the production source requirements and runs the exact manual-cookie curl flow. It does not rely on `curl -c/-b` jars.

CI produced:

```text
Set-Cookie: eng_session=<64 lowercase hex characters>; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800
SESSION_HEX_LENGTH=64
EXPLICIT_COOKIE_HEADER=Cookie: eng_session=<same 64-character token>
HTTP/1.0 200 OK
{"state":"idle","authenticated":true}
Engineering auth host HTTP contract passed
```

Test file:

```text
tests/engineering_auth_http_contract.py
```

This is actual curl output from the host HTTP contract tied to the production authentication source. It is not a physical-controller result.

## Image identity verification

An OTA image is rejected before `esp_ota_begin()` and before any firmware byte is written unless all of these checks pass:

- ESP image magic is valid.
- Product ID is exactly `automatrix_pvdg`.
- Running firmware product ID is also `automatrix_pvdg`.
- Image target chip ID equals `CONFIG_IDF_FIRMWARE_CHIP_ID` for the ESP32-S3 build.
- Candidate secure version is not lower than the running secure version.
- Complete image size fits the inactive OTA partition.

The identity check is performed twice:

1. In `ota_manager_validate_prefix()` while the image prefix is still in RAM.
2. Again in `ota_manager_begin()` immediately before `esp_ota_begin()` can erase the inactive slot.

A wrong product or wrong target therefore reaches neither `esp_ota_begin()` nor `esp_ota_write()`.

## Interrupted-upload safety

Each OTA session records the selected boot partition before the upload starts.

When the HTTP body is interrupted or incomplete:

1. `ota_manager_abort()` calls `esp_ota_abort()`.
2. The abort path never calls `esp_ota_set_boot_partition()`.
3. The boot partition is read again.
4. Its address must equal the original boot partition address.
5. The result is exposed as `boot_partition_preserved_after_abort`.

The source-coupled executable behavior proof produced:

```text
INTERRUPTED_TRACE=validate_identity(product=automatrix_pvdg,target=esp32s3) -> esp_ota_begin(ota_1) -> esp_ota_write(4096) -> esp_ota_write(4096) -> connection_interrupted -> esp_ota_abort -> boot_preserved(ota_0)
INTERRUPTED_BOOT_PARTITION=ota_0
INTERRUPTED_RUNNING_PARTITION=ota_0
INTERRUPTED_EXISTING_FIRMWARE_BOOTABLE=true
```

Test file:

```text
tests/ota_update_behavior_contract.py
```

This proves the production source ordering and an executable state model. It is not a physical flash-interruption or power-loss test.

## Validation before boot switch

`ota_manager_finish()` now uses this strict order:

```text
esp_ota_end()
esp_ota_get_partition_description()
match staged descriptor to the accepted candidate
confirm boot partition is still unchanged
mark image_validated=true
esp_ota_set_boot_partition()
```

The staged descriptor match covers:

- project name
- firmware version
- secure version
- application ELF SHA-256 identity

The reboot endpoint refuses to restart unless all three states are true:

- `update_staged`
- `image_identity_verified`
- `image_validated`

The executable behavior proof produced:

```text
COMPLETED_TRACE=validate_identity(product=automatrix_pvdg,target=esp32s3) -> esp_ota_begin(ota_1) -> esp_ota_write(8192) -> esp_ota_end_validate_complete_image -> esp_ota_get_partition_description_match -> esp_ota_set_boot_partition
VALIDATION_BEFORE_BOOT_SWITCH=true
```

## CI result

The full web/source contract suite passed. The ESP-IDF 6.0.1 ESP32-S3 build also passed with no compiler warnings.

```text
automatrix_pvdg.bin binary size 0x16af40 bytes
Smallest app partition is 0x300000 bytes
0x1950c0 bytes (53%) free
Application binary bytes: 1486656
Compiler warnings: 0
```

## Physical controller verification still required

The following were not performed because flashing and device testing require the physical controller and are outside this implementation task:

- Flashing this branch to the ESP32-S3 controller.
- Confirming the real controller login response issues the 64-character cookie.
- Confirming the real `/api/ota/status` endpoint returns HTTP 200 with that cookie.
- Uploading a mismatched product or wrong-target image to the real controller.
- Interrupting a real TCP upload while flash writing is in progress.
- Removing power during a real OTA upload.
- Confirming the bootloader still starts the previous slot after those interruptions.
- Rebooting into a staged image and observing first-boot validation.
- Deliberately failing first-boot diagnostics and observing hardware rollback.

## Real-controller authentication check

Do not use a cookie jar for this test. Capture `Set-Cookie` and pass the cookie explicitly:

```bash
curl -i -X POST http://<controller>/api/engineering/login \
  -H 'Content-Type: application/json' \
  -d '{"password":"<engineering password>"}'

# Copy only: eng_session=<64 hex characters>

curl -i \
  -H 'Cookie: eng_session=<64 hex characters>' \
  http://<controller>/api/engineering/session

curl -i \
  -H 'Cookie: eng_session=<64 hex characters>' \
  http://<controller>/api/ota/status
```

Expected real-device results:

```text
Set-Cookie: eng_session=<64 hex characters>; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800
/api/engineering/session -> authenticated:true
/api/ota/status -> HTTP 200, not 401
```

A passing build and host contract are not physical qualification. The hardware tests above must pass before secure web OTA is called field-ready.
