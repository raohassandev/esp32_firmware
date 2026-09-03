# Waveshare final-soak executor

This procedure captures the still-missing physical evidence for the frozen Waveshare candidate. It does **not** rebuild firmware, erase NVS, or change the accepted source/config identity.

## Candidate identity

Use only the already-qualified candidate documented in Issues #27/#87 and PR #57:

- source `87841ecee727fe1d814d4186be8c8c26e4afafb4`
- tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e`
- artifact `9843536218` / `waveshare-800x480-87841ece`
- application SHA256 `8be2a2aad5f223d8b9bca498db2e12c04f7f205feaa9908b7922c37421c46593`

Do not rebuild, substitute a branch head, perform a full erase, or erase NVS.

## Mac setup

Connect the board directly to the Mac where possible rather than through the USB hub that interrupted the previous run. Install the only physical-capture dependency:

```bash
python3 -m pip install pyserial
```

Confirm the controller is already running the exact candidate above and that the LCD/touch short gate is still visually normal.

## One-command integrated capture

If the controller backend is reachable at `192.168.1.50`:

```bash
python3 tools/waveshare_soak_capture.py \
  --backend-url http://192.168.1.50 \
  --require-backend
```

If more than one serial device is connected, select the board explicitly:

```bash
python3 tools/waveshare_soak_capture.py \
  --port /dev/cu.usbmodem1101 \
  --backend-url http://192.168.1.50 \
  --require-backend
```

Release-strict defaults are intentionally fixed at:

- target host runtime: `14400` seconds;
- required ESP-IDF timestamp span: `14400` seconds;
- required `Screen soak` samples: `240`;
- minimum checked DMA free: `20000` bytes;
- backend poll interval: `60` seconds.

The command writes a timestamped directory under `physical-evidence/waveshare/` containing raw `serial.log`, `backend-status.jsonl`, `host-events.jsonl`, `serial-validation.json`, and `capture-summary.json`.

The raw serial file is not prefixed with host timestamps because `waveshare_acceptance_check.py` must see the original ESP-IDF timestamps unchanged. Host timestamps are kept separately in JSONL evidence.

## Fail-closed behavior

The capture returns non-zero when the target duration is not completed, the serial device disconnects/closes, the serial/resource validator fails, or a required backend status round fails. A stopped/restarted capture is a new run; partial runs are not additive.

`SERIAL_EVIDENCE_PASS` is **not** final physical acceptance. During the same uninterrupted run the operator must still truthfully observe and record the required LCD/touch conditions: Alarms opens, touch remains responsive, recurring sweep/reload is absent, and visible tearing/corruption is absent.

After the run, copy `tools/waveshare_observations.example.json`, enter only observations actually made against this exact candidate/artifact, and execute the existing combined gate:

```bash
python3 tools/waveshare_final_acceptance.py \
  physical-evidence/waveshare/<run>/serial.log \
  <observations.json>
```

Only a genuine combined PASS may advance #25 backend parity/recovery and #26 persistence/ARM on the exact accepted identity.
