#!/usr/bin/env python3
"""Bench-local board <-> SolTrix simulator communication acceptance runner.

This tool is intentionally for a machine on the same private bench LAN as the
controller and simulator. It does not fabricate evidence and it does not turn
on any Engineering-auth bypass. The Engineering password is read from an
environment variable so it is never embedded in source or placed in argv.

Default bench topology from docs/CHATGPT_EXECUTION_BRIEF_2026-09-06.md:
  controller: 192.168.0.107
  simulator:  192.168.0.102:1502
  meter 0: EM500 Grid, unit 31, PDU 58, INT32/ABCD, 0.00001 kW/raw
  meter 1: EM500 GEN-1, unit 32, PDU 58, INT32/ABCD, 0.00001 kW/raw
  meter 2: WM15 GEN-2, unit 41, PDU 0x0012, INT32/CDAB, 0.0001 kW/raw

Usage:
  export PVDG_ENGINEERING_PASSWORD='...'
  python3 tools/bench_board_simulator_comm_test.py --apply --label normal

After the meter map has already been applied, a controlled simulator scenario
can be measured without another configuration write or restart:
  python3 tools/bench_board_simulator_comm_test.py --measure-only --label em500-dual-conflict

Without --apply or --measure-only, the tool performs read-only preflight/probes
and prints the exact meter payload it would apply.
"""

from __future__ import annotations

import argparse
import http.cookiejar
import json
import math
import os
import socket
import statistics
import struct
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class MeterTarget:
    name: str
    unit_id: int
    role: int
    generator_index: int | None
    address: int
    word_order: int
    scale: float


TARGETS = (
    MeterTarget("Grid Meter", 31, 1, None, 58, 0, 0.00001),
    MeterTarget("GEN-1", 32, 2, 0, 58, 0, 0.00001),
    MeterTarget("GEN-2", 41, 2, 1, 0x0012, 1, 0.0001),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", default="192.168.0.107", help="controller IP/host")
    parser.add_argument("--sim-host", default="192.168.0.102", help="simulator IP/host")
    parser.add_argument("--sim-port", type=int, default=1502, help="simulator Modbus TCP port")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true", help="persist meter map, restart, and measure")
    mode.add_argument(
        "--measure-only",
        action="store_true",
        help="measure the already-commissioned board without auth, writes, or restart",
    )
    parser.add_argument(
        "--samples",
        type=int,
        default=181,
        help="number of /api/meters samples; default is about three minutes at 1 s",
    )
    parser.add_argument("--sample-interval", type=float, default=1.0, help="seconds between samples")
    parser.add_argument("--reconnect-timeout", type=float, default=60.0, help="seconds to wait after restart")
    parser.add_argument("--label", default="normal", help="evidence label, normally the simulator scenario")
    parser.add_argument(
        "--password-env",
        default="PVDG_ENGINEERING_PASSWORD",
        help="environment variable containing the Engineering password",
    )
    return parser.parse_args()


def _recv_exact(sock: socket.socket, count: int) -> bytes:
    chunks: list[bytes] = []
    remaining = count
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError(f"socket closed with {remaining} bytes still expected")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def modbus_read_holding(host: str, port: int, unit_id: int, address: int, count: int, tid: int) -> list[int]:
    request = struct.pack(">HHHBBHH", tid, 0, 6, unit_id, 3, address, count)
    with socket.create_connection((host, port), timeout=3.0) as sock:
        sock.settimeout(3.0)
        sock.sendall(request)
        mbap = _recv_exact(sock, 7)
        response_tid, protocol_id, length, response_unit = struct.unpack(">HHHB", mbap)
        if response_tid != tid:
            raise RuntimeError(f"Modbus TID mismatch: expected {tid}, got {response_tid}")
        if protocol_id != 0:
            raise RuntimeError(f"Modbus protocol-id mismatch: expected 0, got {protocol_id}")
        if response_unit != unit_id:
            raise RuntimeError(f"Modbus unit-id mismatch: expected {unit_id}, got {response_unit}")
        if length < 3:
            raise RuntimeError(f"invalid Modbus MBAP length {length}")
        pdu = _recv_exact(sock, length - 1)

    function = pdu[0]
    if function & 0x80:
        code = pdu[1] if len(pdu) > 1 else -1
        raise RuntimeError(f"Modbus exception from unit {unit_id}: function=0x{function:02x}, code={code}")
    if function != 3:
        raise RuntimeError(f"Modbus function mismatch: expected 3, got {function}")
    if len(pdu) < 2:
        raise RuntimeError("short Modbus read response")
    byte_count = pdu[1]
    expected_bytes = count * 2
    if byte_count != expected_bytes or len(pdu) != 2 + byte_count:
        raise RuntimeError(
            f"Modbus payload mismatch: expected {expected_bytes} data bytes, got {byte_count}"
        )
    return list(struct.unpack(">" + "H" * count, pdu[2:]))


def signed32(value: int) -> int:
    return value - 0x100000000 if value & 0x80000000 else value


def decode_int32(words: list[int], word_order: int, scale: float) -> tuple[int, float]:
    if len(words) != 2:
        raise ValueError("INT32 decode requires exactly two registers")
    if word_order == 0:  # ABCD: high word first
        raw_u32 = (words[0] << 16) | words[1]
    elif word_order == 1:  # CDAB: low word first
        raw_u32 = (words[1] << 16) | words[0]
    else:
        raise ValueError(f"bench runner only expects ABCD/CDAB here, got {word_order}")
    raw = signed32(raw_u32)
    return raw, raw * scale


def direct_simulator_probe(host: str, port: int) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for offset, target in enumerate(TARGETS, start=1):
        tid = 0x4100 + offset
        words = modbus_read_holding(host, port, target.unit_id, target.address, 2, tid)
        raw, kw = decode_int32(words, target.word_order, target.scale)
        results.append(
            {
                "name": target.name,
                "unit_id": target.unit_id,
                "address": target.address,
                "tid": tid,
                "words": words,
                "raw": raw,
                "decoded_kw": kw,
            }
        )
    return results


class JsonHttp:
    def __init__(self, board: str, timeout: float = 8.0):
        self.base = board if board.startswith(("http://", "https://")) else f"http://{board}"
        self.timeout = timeout
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))

    def request(self, method: str, path: str, payload: Any | None = None, timeout: float | None = None) -> Any:
        data = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(self.base + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(req, timeout=self.timeout if timeout is None else timeout) as response:
                body = response.read().decode("utf-8", errors="replace")
                if not body:
                    return None
                return json.loads(body)
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {exc.code} {method} {path}: {body}") from exc

    def get(self, path: str, timeout: float | None = None) -> Any:
        return self.request("GET", path, timeout=timeout)

    def post(self, path: str, payload: Any | None = None, timeout: float | None = None) -> Any:
        return self.request("POST", path, payload=payload, timeout=timeout)


def meter_payload(sim_host: str, sim_port: int) -> dict[str, Any]:
    meters: list[dict[str, Any]] = []
    for target in TARGETS:
        item: dict[str, Any] = {
            "name": target.name,
            "enabled": True,
            "host": sim_host,
            "port": sim_port,
            "unit_id": target.unit_id,
            "role": target.role,
            "function": 3,
            "active_power_address": target.address,
            "data_type": 3,
            "word_order": target.word_order,
            "scale": target.scale,
            "poll_ms": 1000,
            "timeout_ms": 1500,
        }
        if target.generator_index is not None:
            item["generator_index"] = target.generator_index
        meters.append(item)
    return {"meters": meters}


def wait_for_controller(http: JsonHttp, timeout_s: float) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            state = http.get("/api/meters", timeout=2.0)
            if isinstance(state, dict):
                return state
        except Exception as exc:  # controller is expected to disappear during reboot
            last_error = exc
        time.sleep(1.0)
    raise RuntimeError(f"controller did not return within {timeout_s:.0f}s; last error: {last_error}")


def require_control_disabled(http: JsonHttp) -> dict[str, Any]:
    telemetry = http.get("/api/telemetry")
    enabled = telemetry.get("control", {}).get("enabled") if isinstance(telemetry, dict) else None
    if enabled is not False:
        raise RuntimeError(f"automatic control must stay disabled during simulator testing: {enabled}")
    return telemetry


def capture_samples(
    http: JsonHttp,
    first: dict[str, Any],
    count: int,
    interval_s: float,
) -> tuple[list[dict[str, Any]], list[float]]:
    samples = [first]
    timestamps = [time.monotonic()]
    while len(samples) < count:
        time.sleep(interval_s)
        samples.append(http.get("/api/meters"))
        timestamps.append(time.monotonic())
    return samples, timestamps


def summarize_samples(
    samples: list[dict[str, Any]],
    timestamps: list[float],
    simulator_probes: list[dict[str, Any]],
    label: str,
    control_enabled_final: bool,
) -> dict[str, Any]:
    if not samples or len(samples) != len(timestamps):
        raise RuntimeError("post-restart samples/timestamps are missing or inconsistent")

    per_meter: list[dict[str, Any]] = []
    final_meters = samples[-1].get("meters", [])
    observation_duration_s = max(0.0, timestamps[-1] - timestamps[0])

    for index, target in enumerate(TARGETS):
        rows: list[dict[str, Any]] = []
        row_times: list[float] = []
        for sample, captured_at in zip(samples, timestamps):
            meters = sample.get("meters", [])
            if index < len(meters):
                rows.append(meters[index])
                row_times.append(captured_at)
        if not rows:
            raise RuntimeError(f"meter index {index} disappeared from /api/meters")

        runtimes = [row.get("runtime", {}) for row in rows]
        powers = [
            float(runtime["active_power_kw"])
            for runtime in runtimes
            if isinstance(runtime.get("active_power_kw"), (int, float))
            and math.isfinite(float(runtime["active_power_kw"]))
        ]
        first = runtimes[0]
        last = runtimes[-1]
        success_delta = int(last.get("success_count", 0)) - int(first.get("success_count", 0))
        error_first = int(first.get("response_errors", first.get("error_count", 0)))
        error_final = int(last.get("response_errors", last.get("error_count", 0)))
        error_delta = error_final - error_first
        attempt_delta = success_delta + error_delta
        sustained_success_percent = (
            (100.0 * success_delta / attempt_delta) if attempt_delta > 0 else None
        )

        first_success_observed_s = None
        baseline_successes = int(first.get("success_count", 0))
        for runtime, captured_at in zip(runtimes, row_times):
            has_data = runtime.get("has_data") is True
            current_successes = int(runtime.get("success_count", 0))
            if has_data and (baseline_successes > 0 or current_successes > baseline_successes):
                first_success_observed_s = max(0.0, captured_at - row_times[0])
                break

        expected_kw = None
        if index < len(simulator_probes):
            probe_kw = simulator_probes[index].get("decoded_kw")
            if isinstance(probe_kw, (int, float)) and math.isfinite(float(probe_kw)):
                expected_kw = float(probe_kw)
        average_kw = statistics.fmean(powers) if powers else None
        difference_kw = average_kw - expected_kw if average_kw is not None and expected_kw is not None else None
        tolerance_kw = max(0.000001, abs(expected_kw or 0.0) * 0.001)
        value_matches = difference_kw is not None and abs(difference_kw) <= tolerance_kw

        per_meter.append(
            {
                "index": index,
                "name": target.name,
                "endpoint": final_meters[index].get("endpoint", {}) if index < len(final_meters) else {},
                "online": bool(last.get("online", False)),
                "degraded": bool(last.get("degraded", False)),
                "state": last.get("state"),
                "success_count": int(last.get("success_count", 0)),
                "response_errors": error_final,
                "consecutive_failures": int(last.get("consecutive_failures", 0)),
                "recent_success_percent": last.get("recent_success_percent"),
                "current_poll_delay_ms": last.get("current_poll_delay_ms"),
                "last_response_time_ms": last.get("last_response_time_ms"),
                "age_ms": last.get("age_ms", last.get("data_age_ms")),
                "active_power_kw": last.get("active_power_kw"),
                "last_error": last.get("last_error"),
                "last_error_name": last.get("last_error_name"),
                "success_delta": success_delta,
                "error_delta": error_delta,
                "sustained_success_percent": sustained_success_percent,
                "first_success_observed_s_from_sampling_start": first_success_observed_s,
                "simulator_expected_kw": expected_kw,
                "board_power_kw": {
                    "samples": len(powers),
                    "min": min(powers) if powers else None,
                    "avg": average_kw,
                    "max": max(powers) if powers else None,
                },
                "board_minus_simulator_kw": difference_kw,
                "value_tolerance_kw": tolerance_kw,
                "value_matches_simulator": value_matches,
            }
        )

    passed = all(
        meter["online"]
        and not meter["degraded"]
        and meter["success_delta"] > 0
        and meter["error_delta"] == 0
        and meter["consecutive_failures"] == 0
        and meter["board_power_kw"]["samples"] > 0
        and meter["value_matches_simulator"]
        for meter in per_meter
    ) and control_enabled_final is False

    return {
        "result": "PASS" if passed else "FAIL",
        "label": label,
        "sample_count": len(samples),
        "observation_duration_s": observation_duration_s,
        "control_enabled_final": control_enabled_final,
        "meters": per_meter,
        "production_physical_acceptance": False,
        "note": (
            "This is the board<->bench-simulator communication check only, not the full #174 "
            "physical PASS. A first-success value of 0 means the meter already had a successful "
            "sample when HTTP sampling began; it is not claimed as boot-to-first-read latency."
        ),
    }


def main() -> int:
    args = parse_args()
    if args.samples < 2:
        raise SystemExit("--samples must be at least 2 so counter deltas are meaningful")
    if args.sample_interval <= 0:
        raise SystemExit("--sample-interval must be positive")

    print("=== Direct simulator preflight (strict TID/unit matching) ===")
    probes = direct_simulator_probe(args.sim_host, args.sim_port)
    print(json.dumps(probes, indent=2))

    payload = meter_payload(args.sim_host, args.sim_port)
    print("=== Exact full-array meter payload ===")
    print(json.dumps(payload, indent=2))

    http = JsonHttp(args.board)
    print("=== Controller preflight ===")
    before = http.get("/api/meters")
    print(json.dumps(before, indent=2))

    if not args.apply and not args.measure_only:
        print("READ-ONLY PREFLIGHT PASS: re-run with --apply or --measure-only to collect a timed report.")
        return 0

    first_after: dict[str, Any]
    if args.apply:
        password = os.environ.get(args.password_env)
        if not password:
            raise RuntimeError(
                f"--apply requires Engineering password in environment variable {args.password_env}; "
                "the password is intentionally not accepted as a CLI argument"
            )

        print("=== Engineering login (password not printed) ===")
        login = http.post("/api/engineering/login", {"password": password})
        if not isinstance(login, dict) or not login.get("authenticated"):
            raise RuntimeError(f"Engineering login was not accepted: {login}")
        print(json.dumps({k: v for k, v in login.items() if k != "password"}, indent=2))

        print("=== Persist all three meters ===")
        saved = http.post("/api/meters/config", payload)
        if not isinstance(saved, dict) or saved.get("saved") is not True:
            raise RuntimeError(f"meter save was not accepted: {saved}")
        if saved.get("meter_count") != 3:
            raise RuntimeError(f"controller persisted unexpected meter_count: {saved}")
        if saved.get("control_enabled") is not False:
            raise RuntimeError(f"safety interlock failed: meter save did not force control disabled: {saved}")
        if saved.get("restart_required") is not True:
            raise RuntimeError(f"controller did not require restart after meter-map mutation: {saved}")
        print(json.dumps(saved, indent=2))

        print("=== Restart controller ===")
        try:
            restart = http.post("/api/system/restart", {})
            print(json.dumps(restart, indent=2))
        except Exception as exc:
            # A clean restart can close the HTTP connection before the response is delivered.
            print(f"restart request connection closed/errored (allowed during reboot): {exc}")

        # Old session cookie may survive in the client jar but /api/meters is operator-readable.
        first_after = wait_for_controller(http, args.reconnect_timeout)
        print("Controller HTTP is reachable again.")
    else:
        first_after = before
        print("MEASURE-ONLY: no auth, configuration write, or restart will be performed.")

    telemetry = require_control_disabled(http)
    control_enabled_final = bool(telemetry.get("control", {}).get("enabled"))
    samples, timestamps = capture_samples(http, first_after, args.samples, args.sample_interval)
    telemetry_final = require_control_disabled(http)
    control_enabled_final = bool(telemetry_final.get("control", {}).get("enabled"))

    report = summarize_samples(samples, timestamps, probes, args.label, control_enabled_final)
    print("=== REAL board <-> simulator communication report ===")
    print(json.dumps(report, indent=2))
    return 0 if report["result"] == "PASS" else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(2)
