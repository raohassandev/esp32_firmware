# ESP32 PV-DG Field-Test Release Checklist

Branch: `feature/multibrand-inverter-profiles`

Purpose: controlled field commissioning and read-only qualification. This release is not approval for automatic PV-DG control or live inverter writes.

## 1. Before flashing

- Confirm the checked-out commit matches the latest branch head.
- Use ESP-IDF v6.0.1.
- Close all programs using COM5.
- Export the current configuration from the System page.
- Keep automatic control disabled.
- Do not erase flash or NVS.

## 2. Flash and boot

1. Build for ESP32-S3.
2. Flash through COM5 without erase.
3. Capture the serial boot log.
4. Confirm no panic, watchdog loop, repeated reset or recovery AP activation.
5. Confirm the controller reconnects to the commissioned Wi-Fi network.

## 3. Meter qualification

- Open Meters > Meter profiles.
- For the commissioned EM500 profile, confirm active-power scale is `0.00001` when the previous value was `0.01`.
- Save and restart after correcting the scale.
- Confirm displayed grid power is physically plausible and matches the meter display within the expected tolerance.
- Review Live measurements, Energy, History and Settings M01-M18.
- Confirm raw Modbus words and PDU addresses are visible.
- Confirm no meter-setting write or reset action is available.

## 4. Inverter endpoint commissioning

- Open Inverters > Inverter endpoints and ratings.
- Add or edit up to 12 inverter channels.
- Enter name, host/IP, port, unit ID, timeout and rated kW.
- Do not enable a channel until its endpoint and rating are verified.
- Save all channels; verify automatic control is forced disabled.
- Restart the controller.
- Confirm duplicate enabled host/port/unit combinations are rejected.

## 5. Profile assignment

- Select the inverter channel.
- Select manufacturer and model family.
- Apply the profile and restart.
- Confirm the qualification state and write-lock state are displayed.
- Pending profiles must remain non-commandable.

## 6. Read-only inverter communication test

- Use `Test connection (read-only)`.
- Confirm the result reports `writes_issued: false`.
- Record identity and active-power raw register responses when supported.
- Do not interpret unsupported or pending profile data as valid telemetry.

## 7. Stability test

- Run for at least 30 minutes.
- Confirm meter data remains fresh.
- Confirm Wi-Fi remains connected or reconnects cleanly after a controlled router interruption.
- Confirm no spontaneous restart, panic, watchdog event or heap exhaustion.
- Confirm automatic control remains disabled and no inverter command is issued.

## 8. Evidence to capture

- Git commit SHA.
- ESP-IDF version.
- Application binary SHA256.
- Serial boot log.
- Exported pre-test and post-test configuration.
- Screenshots of Dashboard, complete Meter workspace and Inverter pages.
- Meter display comparison.
- Read-only inverter probe JSON.
- 30-minute stability notes.

## 9. Stop conditions

Stop testing immediately if any of these occur:

- Automatic control becomes enabled unexpectedly.
- Any pending profile reports write eligibility.
- A read-only probe reports a write.
- Grid power remains scaled incorrectly.
- Reboot loop, panic, watchdog reset or recovery AP appears unexpectedly.
- Configuration is lost or Wi-Fi credentials change unexpectedly.

## Release boundary

Passing this checklist qualifies the build for field commissioning and read-only device integration work only. Live inverter commands, automatic PV-DG operation and production approval require exact manual-backed profiles plus physical command/readback qualification.
