#!/usr/bin/env python3
"""Provision the controller against the lab simulator and measure the closed loop.

Usage:  python lab_run.py <engineering_password> [--provision-only]

The password is taken as an argument and never written to disk or echoed.

What this does, in order:
  1. Authenticate to the engineering API.
  2. Read the CURRENT full configuration and keep it, so the commissioned real
     meter is preserved rather than overwritten -- its role is changed to
     unassigned and the simulated meter becomes the grid source.
  3. Point inverter 0 at the simulator, assign the measured lab profile, and
     declare the endpoint a simulator so LAB write authority applies.
  4. Restart, because a profile change requires it.
  5. Verify the commissioning gate reports LAB scope -- never production.
  6. Enable control and measure: requested vs applied PV, write-confirmation
     state, and whether the grid meter tracks the plant balance.

Every value written for the generator is a LAB value for a simulated 70 kW load,
not site data. Real generator ratings have not been supplied.
"""

import json
import sys
import time
import urllib.error
import urllib.request

CONTROLLER = "http://192.168.100.14"
SIM_HOST = "192.168.100.11"
SIM_PORT = 1502
SIM_INVERTER_UNIT = 1
SIM_METER_UNIT = 10
LAB_PROFILE = "soltrix.sim.huawei.v3"
SIM_LOAD_KW = 70.0          # from config.esp-firmware-lab.json
SIM_INVERTER_RATED_KW = 100.0

session_cookie = None


def call(path, payload=None, method=None, timeout=15):
    global session_cookie
    data = json.dumps(payload).encode() if payload is not None else None
    request = urllib.request.Request(
        CONTROLLER + path, data=data,
        method=method or ("POST" if data else "GET"))
    request.add_header("Content-Type", "application/json")
    if session_cookie:
        request.add_header("Cookie", session_cookie)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8", "replace")
            for header, value in response.getheaders():
                if header.lower() == "set-cookie":
                    session_cookie = value.split(";")[0]
            return response.status, (json.loads(raw) if raw.strip().startswith(("{", "[")) else raw)
    except urllib.error.HTTPError as error:
        raw = error.read().decode("utf-8", "replace")
        return error.code, raw
    except Exception as error:  # noqa: BLE001 - report and continue
        return 0, str(error)


def step(label):
    print(f"\n--- {label} ---")


def require(ok, message):
    if not ok:
        print(f"FAILED: {message}")
        sys.exit(1)
    print(f"ok: {message}")


def login(password):
    step("authenticate")
    status, body = call("/api/engineering/login", {"password": password})
    require(status == 200, f"login returned {status}")
    require(session_cookie is not None, "session cookie received")


def provision():
    step("read current configuration")
    status, config = call("/api/config")
    require(status == 200 and isinstance(config, dict), f"GET /api/config returned {status}")
    meters = config.get("meters", [])
    print(f"    existing meters: {len(meters)}")

    # Preserve meter 0 exactly, only dropping its grid role, then add the
    # simulated EM500 as the grid source. Its decode matches the real EM500:
    # FC03 @57, 2 words, signed int32, high word first, scale 1e-5.
    step("configure meters: keep the real meter, add the simulated grid meter")
    meter_payload = []
    for index, meter in enumerate(meters):
        entry = dict(meter)
        entry["role"] = 0  # METER_ROLE_UNASSIGNED
        meter_payload.append(entry)
        print(f"    meter {index} '{meter.get('name')}' role -> unassigned")
    meter_payload.append({
        "enabled": True,
        "name": "LAB SIM EM500",
        "host": SIM_HOST,
        "port": SIM_PORT,
        "unit_id": SIM_METER_UNIT,
        "role": 1,  # METER_ROLE_GRID
        "function": 3,
        "active_power_address": 57,
        # modbus_data_type_t: UINT16=0, INT16=1, UINT32=2, INT32=3, FLOAT32=4.
        # INT32 is required: grid power is negative when exporting, and UINT32
        # would decode export as a huge positive import.
        "data_type": 3,
        # modbus_word_order_t: ABCD=0 (high word first), matching the measured
        # simulator response and the real EM500.
        "word_order": 0,
        "scale": 0.00001,
        "poll_ms": 250,
        "timeout_ms": 300,
    })
    status, body = call("/api/meters/config", {"meters": meter_payload})
    require(status == 200, f"meter configuration returned {status}: {body}")

    step("point inverter 0 at the simulator")
    status, body = call("/api/inverters/config", {"inverters": [{
        "enabled": True,
        "name": "LAB SIM HUAWEI",
        "host": SIM_HOST,
        "port": SIM_PORT,
        "unit_id": SIM_INVERTER_UNIT,
        "rated_kw": SIM_INVERTER_RATED_KW,
        "timeout_ms": 300,
    }]})
    require(status == 200, f"inverter configuration returned {status}: {body}")

    step("assign the measured lab profile and declare the endpoint a simulator")
    status, body = call("/api/inverter-profile-assignment", {
        "inverter_index": 0, "profile_id": LAB_PROFILE, "lab_target": True})
    require(status == 200, f"profile assignment returned {status}: {body}")
    if isinstance(body, dict):
        print(f"    lab_target={body.get('lab_target')} "
              f"permission={body.get('write_permission_after_restart')}")
        require(body.get("lab_target") is True, "endpoint is declared a lab simulator")
        require(body.get("write_permission_after_restart") == "lab_simulator_only",
                "write authority is lab-only, NOT production")

    step("configure the grid/generator policy (LAB values for a simulated plant)")
    status, body = call("/api/solar-grid/config", {
        "enabled": True,
        "minimum_import_kw": 5.0,
        "export_limit_kw": 0.0,
        "generator_rated_kw": SIM_LOAD_KW,
        "generator_minimum_loading_percent": 30.0,
        "generator_reserve_kw": 5.0,
        "generator_reverse_power_margin_kw": 2.0,
    })
    print(f"    solar-grid config -> {status}: {str(body)[:200]}")


def restart_and_wait():
    step("restart (a profile change requires it)")
    status, _ = call("/api/system/restart", {})
    print(f"    restart returned {status}")
    for attempt in range(40):
        time.sleep(3)
        status, _ = call("/api/status", timeout=5)
        if status == 200:
            print(f"    controller back after ~{(attempt + 1) * 3}s")
            return
    require(False, "controller did not come back after restart")


def verify_scope(password):
    login(password)
    step("verify the commissioning gate reports LAB, never production")
    status, gate = call("/api/commissioning/gate")
    require(status == 200 and isinstance(gate, dict), f"gate returned {status}: {gate}")
    print(f"    commissioned={gate.get('commissioned')} scope={gate.get('scope')} "
          f"production_qualified={gate.get('production_qualified')}")
    require(gate.get("production_qualified") is not True,
            "gate must NOT report production qualification")
    for prereq in gate.get("prerequisites", []):
        if not prereq.get("satisfied"):
            print(f"    UNMET  {prereq.get('id')}: {prereq.get('message') or prereq.get('reason')}")
    return gate


def sim_read():
    """Read the simulator directly, as an independent witness to the firmware."""
    import socket
    import struct

    def read(unit, address, words):
        with socket.create_connection((SIM_HOST, SIM_PORT), timeout=3) as sock:
            frame = struct.pack(">HHHBBHH", 1, 0, 6, unit, 3, address, words)
            sock.sendall(frame)
            reply = sock.recv(256)
            return reply[9:]

    pv = struct.unpack(">i", read(1, 32080, 2))[0] / 1000.0
    limit = struct.unpack(">h", read(1, 40125, 1))[0] / 10.0
    grid = struct.unpack(">i", read(10, 57, 2))[0] * 0.00001
    return pv, limit, grid


def measure(seconds=90):
    step(f"measure the closed loop for {seconds}s")
    print("  t    ctrl_mode           req_kW  app_kW  confirm     "
          "sim_limit%  sim_PV_kW  sim_grid_kW  balance")
    start = time.time()
    while time.time() - start < seconds:
        _, status = call("/api/solar-grid/status", timeout=6)
        _, base = call("/api/status", timeout=6)
        try:
            pv, limit, grid = sim_read()
        except Exception as error:  # noqa: BLE001
            pv = limit = grid = float("nan")
            print(f"    simulator read failed: {error}")
        authority = (base or {}).get("control_authority", {}) if isinstance(base, dict) else {}
        s = status if isinstance(status, dict) else {}
        balance = "ok" if abs((SIM_LOAD_KW - pv) - grid) < 0.1 else "MISMATCH"
        print(f"  {int(time.time() - start):3d}  "
              f"{str(authority.get('mode_label'))[:18]:<18}  "
              f"{str(s.get('requested_pv_kw'))[:6]:>6}  "
              f"{str(s.get('applied_pv_kw'))[:6]:>6}  "
              f"{str(s.get('write_confirmation'))[:10]:<10}  "
              f"{limit:>9.1f}  {pv:>9.3f}  {grid:>11.3f}  {balance}")
        if s.get("lab_simulator_mode") is not True:
            print("    WARNING: status does not report lab_simulator_mode")
        time.sleep(5)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    password = sys.argv[1]
    provision_only = "--provision-only" in sys.argv

    login(password)
    provision()
    restart_and_wait()
    gate = verify_scope(password)
    if provision_only:
        print("\nprovision-only: stopping before enabling control")
        return
    if not gate.get("commissioned"):
        print("\nGate is not satisfied; not enabling control. Unmet items are listed above.")
        return
    measure()


if __name__ == "__main__":
    main()
