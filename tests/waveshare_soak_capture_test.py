#!/usr/bin/env python3
import importlib.util
import json
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("waveshare_soak_capture", ROOT / "tools" / "waveshare_soak_capture.py")
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/api/status":
            self.send_response(404)
            self.end_headers()
            return
        body = json.dumps({"network_online": True, "control_enabled": False}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def main() -> None:
    assert MOD.choose_serial_port("/dev/cu.manual", []) == "/dev/cu.manual"
    assert MOD.choose_serial_port(None, ["/dev/cu.usbmodem1101"]) == "/dev/cu.usbmodem1101"

    try:
        MOD.choose_serial_port(None, [])
        raise AssertionError("zero candidates must fail")
    except RuntimeError as exc:
        assert "no serial port" in str(exc)

    try:
        MOD.choose_serial_port(None, ["/dev/cu.usbmodem2", "/dev/cu.usbmodem1"])
        raise AssertionError("ambiguous candidates must fail")
    except RuntimeError as exc:
        assert "multiple serial ports" in str(exc)
        assert "/dev/cu.usbmodem1" in str(exc)

    assert MOD.normalize_backend_url("192.168.1.50/") == "http://192.168.1.50"
    assert MOD.normalize_backend_url("https://controller/") == "https://controller"
    assert MOD.normalize_backend_url("   ") is None

    server = HTTPServer(("127.0.0.1", 0), StatusHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        probe = MOD.probe_backend(f"http://127.0.0.1:{server.server_port}", timeout_seconds=2)
        assert probe.ok, probe
        assert probe.status == 200
        assert probe.error is None
        assert probe.latency_ms >= 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

    # Validation is delegated to the existing acceptance gate. Prove the new
    # executor calls that gate with caller thresholds rather than inventing a
    # second serial parser.
    log = """
I (1000) app: Before LCD DMA reservation: internal DMA free=140000 largest=130000
I (2000) app: After LCD DMA reservation: internal DMA free=105000 largest=90000
I (3000) app: Before Product Core init: internal DMA free=104000 largest=89000
I (5000) core: After Product Core init: internal DMA free=34000 largest=30000
I (5050) waveshare_product: Shared Product Core started
I (5100) waveshare_product: Espressif flash dispatcher ready; PSRAM-stacked HMI persistence is routed through internal RAM
I (6000) lcd: LVGL activation stage 1/6
I (6100) lcd: LVGL activation stage 2/6
I (6200) lcd: LVGL activation stage 3/6
I (6300) lcd: LVGL activation stage 4/6
I (6400) lcd: LVGL activation stage 5/6
I (6500) lcd: LVGL activation stage 6/6
I (6600) lcd: Native LCD/LVGL/touch ready
I (7000) app: After LVGL/UI activation: internal DMA free=28000 largest=24000
I (7100) waveshare_product: Local Engineering commissioning backend bound to touchscreen
I (7200) waveshare_product: Local source-evidence commissioning backend bound to touchscreen
I (7300) waveshare_product: Screen refresh task created in PSRAM
I (61000) lcd: Screen soak: DMA free=27000 largest=23000
I (121000) lcd: Screen soak: DMA free=26000 largest=22000
"""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "serial.log"
        path.write_text(log)
        good = MOD.validate_serial_log(path, 20_000, 20_000, 2, 120)
        assert good["passed"], good["failures"]
        bad = MOD.validate_serial_log(path, 20_000, 20_000, 3, 120)
        assert not bad["passed"]
        assert any(item.startswith("screen_soak=") for item in bad["failures"])

    parser = MOD.build_parser()
    defaults = parser.parse_args([])
    assert defaults.duration_seconds == 14_400
    assert defaults.min_runtime_seconds == 14_400
    assert defaults.min_soak_samples == 240
    assert defaults.min_dma_free == 20_000
    assert defaults.require_backend is False

    print("Waveshare soak capture tests passed")


if __name__ == "__main__":
    main()
