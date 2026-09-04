#!/usr/bin/env python3
"""Capture one uninterrupted Waveshare serial/backend soak on a real controller.

This tool records evidence; it never invents or substitutes the human LCD/touch
observations required by ``waveshare_final_acceptance.py``. Raw ESP-IDF serial
lines are preserved byte-for-byte so the existing deterministic validator can
consume the resulting log without host-timestamp prefixes changing its syntax.

Typical final-soak command on macOS::

    python3 tools/waveshare_soak_capture.py \
      --backend-url http://192.168.1.50 --require-backend

The default target is 14,400 seconds with >=240 Screen-soak samples and >=20 kB
DMA free. A serial disconnect, short run, fatal firmware marker, timestamp
rollback, or failed serial/resource gate returns non-zero. Backend failures are
also fatal when ``--require-backend`` is selected.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time
import urllib.error
import urllib.request
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import BinaryIO, Iterable


DEFAULT_PORT_PATTERNS = (
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def discover_serial_ports(patterns: Iterable[str] = DEFAULT_PORT_PATTERNS) -> list[str]:
    ports: set[str] = set()
    for pattern in patterns:
        ports.update(glob.glob(pattern))
    return sorted(ports)


def choose_serial_port(explicit: str | None, candidates: list[str] | None = None) -> str:
    if explicit:
        return explicit
    found = discover_serial_ports() if candidates is None else sorted(set(candidates))
    if not found:
        raise RuntimeError(
            "no serial port found; connect the board directly and pass --port /dev/cu.usbmodem..."
        )
    if len(found) != 1:
        raise RuntimeError("multiple serial ports found; pass --port explicitly: " + ", ".join(found))
    return found[0]


def normalize_backend_url(value: str | None) -> str | None:
    if value is None:
        return None
    value = value.strip().rstrip("/")
    if not value:
        return None
    if not value.startswith(("http://", "https://")):
        value = "http://" + value
    return value


@dataclass
class BackendProbe:
    timestamp_utc: str
    ok: bool
    status: int | None
    latency_ms: float
    error: str | None


def probe_backend(base_url: str, timeout_seconds: float = 5.0) -> BackendProbe:
    url = base_url.rstrip("/") + "/api/status"
    started = time.monotonic()
    status: int | None = None
    error: str | None = None
    ok = False
    try:
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout_seconds) as response:
            status = int(response.status)
            body = response.read()
        if status == 200:
            # A malformed HTML/error page must not be counted as a healthy API round.
            json.loads(body.decode("utf-8"))
            ok = True
        else:
            error = f"HTTP {status}"
    except (urllib.error.URLError, TimeoutError, OSError, ValueError, json.JSONDecodeError) as exc:
        error = f"{type(exc).__name__}: {exc}"
    latency_ms = (time.monotonic() - started) * 1000.0
    return BackendProbe(utc_now(), ok, status, round(latency_ms, 3), error)


def _open_serial(port: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pyserial is required for physical capture: python3 -m pip install pyserial") from exc
    return serial.Serial(port=port, baudrate=baud, timeout=1.0)


def _write_jsonl(handle, payload: dict) -> None:
    handle.write(json.dumps(payload, sort_keys=True) + "\n")
    handle.flush()


def _load_acceptance_module():
    # Running this file directly places tools/ on sys.path. Keeping the import
    # lazy also lets CI exercise --help/port/backend helpers without pyserial.
    import waveshare_acceptance_check  # type: ignore

    return waveshare_acceptance_check


def validate_serial_log(
    log_path: Path,
    min_dma_free: int,
    min_dma_largest: int,
    min_soak_samples: int,
    min_runtime_seconds: int,
) -> dict:
    mod = _load_acceptance_module()
    result = mod.analyse(
        log_path.read_text(errors="replace"),
        min_dma_free=min_dma_free,
        min_dma_largest=min_dma_largest,
        min_soak_samples=min_soak_samples,
        min_runtime_seconds=min_runtime_seconds,
    )
    return asdict(result)


def make_output_dir(root: Path | None) -> Path:
    if root is None:
        root = Path("physical-evidence") / "waveshare"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = root / f"soak-{stamp}"
    path.mkdir(parents=True, exist_ok=False)
    return path


def _safe_flush(handle: BinaryIO) -> None:
    handle.flush()
    try:
        os.fsync(handle.fileno())
    except (AttributeError, OSError):
        pass


def run_capture(args: argparse.Namespace) -> tuple[int, dict]:
    port = choose_serial_port(args.port)
    backend_url = normalize_backend_url(args.backend_url)
    if args.require_backend and not backend_url:
        raise RuntimeError("--require-backend requires --backend-url")

    output_dir = make_output_dir(args.output_dir)
    raw_log = output_dir / "serial.log"
    host_events = output_dir / "host-events.jsonl"
    backend_log = output_dir / "backend-status.jsonl"
    validation_file = output_dir / "serial-validation.json"
    summary_file = output_dir / "capture-summary.json"

    start_utc = utc_now()
    start_mono = time.monotonic()
    deadline = start_mono + args.duration_seconds
    next_backend = start_mono
    serial_bytes = 0
    serial_lines = 0
    backend_attempts = 0
    backend_successes = 0
    disconnected = False
    interrupted = False
    failure: str | None = None

    ser = _open_serial(port, args.baud)
    try:
        with raw_log.open("wb", buffering=0) as serial_handle, host_events.open(
            "w", encoding="utf-8", buffering=1
        ) as event_handle, backend_log.open("w", encoding="utf-8", buffering=1) as backend_handle:
            _write_jsonl(
                event_handle,
                {
                    "event": "capture_started",
                    "timestamp_utc": start_utc,
                    "port": port,
                    "baud": args.baud,
                    "duration_seconds": args.duration_seconds,
                    "backend_url": backend_url,
                },
            )

            while time.monotonic() < deadline:
                now = time.monotonic()
                if backend_url and now >= next_backend:
                    probe = probe_backend(backend_url, args.backend_timeout_seconds)
                    backend_attempts += 1
                    backend_successes += int(probe.ok)
                    _write_jsonl(backend_handle, asdict(probe))
                    # Advance by whole intervals to avoid a slow request creating a
                    # burst of catch-up probes.
                    next_backend = max(next_backend + args.backend_poll_seconds, time.monotonic())

                try:
                    chunk = ser.readline()
                except Exception as exc:  # pyserial exception type is optional at import time
                    disconnected = True
                    failure = f"serial_read_failed:{type(exc).__name__}:{exc}"
                    _write_jsonl(
                        event_handle,
                        {"event": "serial_disconnected", "timestamp_utc": utc_now(), "error": failure},
                    )
                    break

                if chunk:
                    serial_handle.write(chunk)
                    serial_bytes += len(chunk)
                    serial_lines += chunk.count(b"\n")

                # Some USB-driver failures surface as a closed handle rather than
                # raising from readline(). Treat either case as an interrupted run.
                if hasattr(ser, "is_open") and not ser.is_open:
                    disconnected = True
                    failure = "serial_port_closed"
                    _write_jsonl(
                        event_handle,
                        {"event": "serial_disconnected", "timestamp_utc": utc_now(), "error": failure},
                    )
                    break

            _safe_flush(serial_handle)
            _write_jsonl(
                event_handle,
                {
                    "event": "capture_stopped",
                    "timestamp_utc": utc_now(),
                    "serial_bytes": serial_bytes,
                    "serial_lines": serial_lines,
                },
            )
    except KeyboardInterrupt:
        interrupted = True
        failure = "operator_interrupt"
    finally:
        try:
            ser.close()
        except Exception:
            pass

    end_mono = time.monotonic()
    elapsed = max(0.0, end_mono - start_mono)
    completed_target = not disconnected and not interrupted and elapsed >= args.duration_seconds

    validation = validate_serial_log(
        raw_log,
        min_dma_free=args.min_dma_free,
        min_dma_largest=args.min_dma_largest,
        min_soak_samples=args.min_soak_samples,
        min_runtime_seconds=args.min_runtime_seconds,
    )
    validation_file.write_text(json.dumps(validation, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    backend_ok = True
    if args.require_backend:
        backend_ok = backend_attempts > 0 and backend_successes == backend_attempts

    serial_ok = bool(validation.get("passed"))
    capture_ok = completed_target and serial_ok and backend_ok
    summary = {
        "schema": 1,
        "candidate_acceptance": "SERIAL_EVIDENCE_PASS" if capture_ok else "SERIAL_EVIDENCE_INCOMPLETE_OR_FAIL",
        "final_physical_acceptance": "NOT_EVALUATED_REQUIRES_HUMAN_LCD_TOUCH_OBSERVATIONS",
        "start_utc": start_utc,
        "end_utc": utc_now(),
        "port": port,
        "baud": args.baud,
        "target_duration_seconds": args.duration_seconds,
        "elapsed_host_seconds": round(elapsed, 3),
        "completed_target_duration": completed_target,
        "serial_disconnected": disconnected,
        "operator_interrupted": interrupted,
        "failure": failure,
        "serial_bytes": serial_bytes,
        "serial_lines": serial_lines,
        "serial_validation_passed": serial_ok,
        "backend_url": backend_url,
        "backend_required": args.require_backend,
        "backend_attempts": backend_attempts,
        "backend_successes": backend_successes,
        "backend_all_required_rounds_passed": backend_ok,
        "files": {
            "serial_log": raw_log.name,
            "host_events": host_events.name,
            "backend_status": backend_log.name,
            "serial_validation": validation_file.name,
        },
    }
    summary_file.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    print(f"Evidence directory: {output_dir}")

    if disconnected or interrupted or not completed_target:
        return 2, summary
    if not serial_ok or not backend_ok:
        return 1, summary
    return 0, summary


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Capture uninterrupted Waveshare physical-soak serial/backend evidence")
    p.add_argument("--port", help="Serial device; omitted only when exactly one supported macOS port is present")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--duration-seconds", type=int, default=14_400)
    p.add_argument("--backend-url", help="Controller base URL/IP; polls public GET /api/status")
    p.add_argument("--require-backend", action="store_true", help="Fail capture if any scheduled backend round fails")
    p.add_argument("--backend-poll-seconds", type=float, default=60.0)
    p.add_argument("--backend-timeout-seconds", type=float, default=5.0)
    p.add_argument("--min-soak-samples", type=int, default=240)
    p.add_argument("--min-runtime-seconds", type=int, default=14_400)
    p.add_argument("--min-dma-free", type=int, default=20_000)
    p.add_argument("--min-dma-largest", type=int, default=0)
    p.add_argument("--output-dir", type=Path, help="Parent directory; a timestamped child is created")
    return p


def main() -> int:
    args = build_parser().parse_args()
    if args.duration_seconds <= 0 or args.min_runtime_seconds < 0 or args.min_soak_samples <= 0:
        raise SystemExit("duration/minimum arguments must be positive")
    if args.backend_poll_seconds <= 0 or args.backend_timeout_seconds <= 0:
        raise SystemExit("backend timing arguments must be positive")
    try:
        code, _ = run_capture(args)
        return code
    except RuntimeError as exc:
        print(f"CAPTURE SETUP FAIL: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
