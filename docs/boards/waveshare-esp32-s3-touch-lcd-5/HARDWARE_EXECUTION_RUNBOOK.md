# Waveshare 800x480 stabilization — hardware execution runbook

This runbook executes Lane D against the exact combined A+B+C software candidate. It does not replace `STABILIZATION_ACCEPTANCE.md`; that matrix remains the pass/fail authority.

## Candidate pin

- Integration branch: `work/waveshare/stabilization-integration`
- Candidate commit: `ec4fb846875d67a806c2b7cc48b21d5706f91995`
- Candidate tree: `09bf66d223e91fc0a248551d0e39c02eb15a2e52`
- CI-tested PR merge ref: `a59f39bdd326d0d7a543f3da244815ac3c4fbf37`
- The candidate and tested merge ref have the same tree SHA.

If any software commit changes after this pin, stop and requalify the new combined candidate before accepting hardware evidence.

## 1. Prepare an exact clean checkout

```bash
git fetch origin
git checkout --detach ec4fb846875d67a806c2b7cc48b21d5706f91995
git status --porcelain
git rev-parse HEAD
git rev-parse HEAD^{tree}
```

Required output before flashing:

- `git status --porcelain` is empty;
- HEAD is `ec4fb846875d67a806c2b7cc48b21d5706f91995`;
- tree is `09bf66d223e91fc0a248551d0e39c02eb15a2e52`.

Do not use a locally modified tree for release evidence.

## 2. Build the exact 800x480 Product Core + LCD image

Use ESP-IDF 6.0.1, matching CI.

```bash
cd boards/waveshare_esp32_s3_touch_lcd_5/screen/product_800x480
idf.py set-target esp32s3
idf.py build
test -f build/automatrix_pvdg_waveshare_800x480.bin
```

Record a hash of the binary that is actually flashed:

```bash
sha256sum build/automatrix_pvdg_waveshare_800x480.bin 2>/dev/null || \
  shasum -a 256 build/automatrix_pvdg_waveshare_800x480.bin
```

Keep the candidate SHA, tree SHA, binary SHA-256, ESP-IDF version, board identity and test date together in the evidence record.

## 3. Flash and capture the serial session

Set `PORT` to the real Waveshare ESP32-S3 serial port discovered on the test machine.

```bash
export PORT=/dev/REPLACE_WITH_REAL_PORT
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

Capture the complete serial session using the terminal/serial logging facility available on the test machine. Do not discard boot/reset-reason lines or the periodic `Screen soak` resource lines.

Use a safe bench setup. Do not energize or command real PV, generator, grid switching or other plant equipment merely to exercise the HMI. Automatic control must remain fail-closed until the acceptance step specifically requires the canonical ARM gate and the connected test arrangement is safe for that action.

## 4. Execute display/touch gates

Run every `DISP-*` row in `STABILIZATION_ACCEPTANCE.md` and retain the requested video/serial evidence.

Minimum high-value sequences:

- idle Overview: 5 minutes;
- changing live values: 10 minutes;
- Grid/Solar updating lists: 10 minutes;
- Alarms/Events updating lists: 10 minutes;
- 100 page changes;
- browser/API traffic while LCD is updating;
- Commissioning form navigation and valid saves;
- Commissioning Review held open >=10 minutes with unchanged gate refreshes, followed by one real gate-state transition;
- >=500 deliberate touches across pages.

Any recurring flicker/shake/tearing, blank frame, page overlay, unexpected whole-list rebuild, lost/ghost touch or reset is a failure, not a cosmetic note.

## 5. Execute native backend parity/recovery gates

Compare the LCD with the same Core authority used by the browser/API for every `DATA-*` row.

Required scenarios include:

- live/status parity;
- healthy/stale/offline meter states without fabricated zeroes;
- healthy/offline/not-tested inverter states;
- representative alarm raise, clear and acknowledge lifecycle;
- alarm priority/suppression/causality state;
- recent event order/state/wording;
- device/backend loss then recovery without controller reboot;
- bounded provider failure represented as unavailable, never truncated/fabricated current data;
- Alarms held open continuously for >=30 minutes while representative events occur.

Retain all `Screen soak` lines during the 30-minute Alarms hold.

## 6. Execute persistence and ARM gates

Run every `CFG-*` row. At minimum record:

- >=20 site save/readback cycles;
- >=20 representative meter save/readback cycles;
- >=20 inverter/profile save/readback cycles;
- >=20 plant/control save/readback cycles;
- >=20 save -> reboot -> re-read cycles;
- available interrupted/failed/partial-save cases;
- ARM refusal with unmet prerequisites;
- ARM success only after the canonical commissioning gate is satisfied in a safe test setup;
- restart and commissioning re-entry with persisted authoritative values.

A failed/partial persistence operation must not leave automatic control armed.

## 7. Capture resource/control isolation evidence

Retain the full periodic resource series, not only start/end numbers. Cover every `RES-*` row:

- internal heap free/minimum;
- PSRAM free/largest block;
- internal DMA free/largest block;
- LVGL/screen task stack headroom where exposed;
- flash-dispatcher behavior during repeated saves;
- reset/watchdog reason;
- existing Product Core control cadence/jitter under UI/network/persistence load;
- operational-payload memory churn during the >=30 minute Alarms hold.

Reject progressive memory collapse, allocation failure, watchdog/reset or material control starvation.

## 8. Four-hour integrated soak

Run a minimum **4 continuous hours** on the exact board/image with LCD/touch, Wi-Fi, native backend refresh and representative navigation active.

During the soak:

- visit Overview, Grid, Solar, Alarms, Ready, Commission and Source;
- include one continuous >=30 minute Alarms hold;
- include one continuous >=10 minute Commissioning Review hold followed by a real gate transition;
- exercise representative device/network loss and recovery;
- execute bounded authenticated commissioning reads/saves;
- retain every `Screen soak` line and all reset/control-timing evidence.

The soak passes only with zero unexpected reset/WDT, zero recurring flicker/shake, zero persistent backend stall, zero persistence mismatch, no progressive heap/PSRAM/DMA collapse and no evidence of Product Core starvation.

## 9. Evidence record

For each acceptance row record:

- matrix ID;
- exact candidate commit/tree;
- binary SHA-256;
- board identity;
- start/end time;
- test action/scenario;
- PASS/FAIL;
- serial-log filename/range;
- video/screenshot filename where applicable;
- resource values/trend;
- failure/reset reason and reproduction notes if any.

Do not mark a hardware row passed without the evidence required by the acceptance matrix. Parent PR #20 remains Draft until all applicable rows pass on this exact combined candidate (or a later fully requalified candidate).