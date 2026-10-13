#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""60-second soak test exercising live HTTP control changes and verifying 120fps throughput."""
import json
import math
import sys
import threading
import time
import urllib.request

BASE_URL = "http://127.0.0.1:8477"


def post_json(path, data):
    req = urllib.request.Request(
        f"{BASE_URL}{path}",
        data=json.dumps(data).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=3) as resp:
        return json.loads(resp.read().decode("utf-8"))


def get_json(path):
    with urllib.request.urlopen(f"{BASE_URL}{path}", timeout=3) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    duration_s = 65.0  # >60s soak test
    print(f"Starting 60s+ Studio Soak Test (duration: {duration_s}s)...")

    # Verify studio is reachable
    try:
        init_state = get_json("/api/state")
        init_cm = init_state["camera_metrics"]
        init_frames = init_cm["total_frames"]
        init_gaps = init_cm["sequence_gaps"]
        init_anomalies = init_cm["bad_intervals"]
        print(f"Connected to Studio! Capabilities: {init_state['capabilities']['width']}x{init_state['capabilities']['height']}@{init_state['capabilities']['target_fps']}fps")
        print(f"Trial baseline: initial frame={init_frames}, gaps={init_gaps}")
    except Exception as e:
        print(f"Failed to connect to studio: {e}")
        return 1

    stop_event = threading.Event()
    control_changes = 0
    errors = []

    # Thread 1: Control exercising worker (modifies exposure & gain every 3s)
    def control_worker():
        nonlocal control_changes
        steps = [
            {"exposure_us": 1000, "gain": 4.0},
            {"exposure_us": 2000, "gain": 2.0},
            {"exposure_us": 4000, "gain": 2.0},
            {"exposure_us": 6000, "gain": 1.5},
            {"exposure_us": 1000, "gain": 8.0},
            {"exposure_us": 4000, "gain": 2.0},
        ]
        idx = 0
        while not stop_event.wait(3.0):
            step = steps[idx % len(steps)]
            idx += 1
            try:
                post_json("/api/controls", step)
                control_changes += 1
            except Exception as e:
                errors.append(f"Control POST failed: {e}")

    worker_thread = threading.Thread(target=control_worker, daemon=True)
    worker_thread.start()

    # Thread 2: Monitor stream and stats
    t0 = time.monotonic()
    t_end = t0 + duration_s
    last_frames = 0
    samples = []

    print("Soak test running...")
    while time.monotonic() < t_end:
        time.sleep(2.0)
        try:
            state = get_json("/api/state")
            cm = state["camera_metrics"]
            dm = state["display_metrics"]
            elapsed = time.monotonic() - t0
            print(f"  [{elapsed:4.1f}s] Frames: {cm['total_frames']} | Sensor FPS: {cm['sensor_fps']:.2f} | Host FPS: {cm['host_fps']:.2f} | Gaps: {cm['sequence_gaps']} | Exp: {cm['actual_exposure_us']}us | Gain: {cm['actual_analog_gain']:.1f}x")
            samples.append(cm)
        except Exception as e:
            errors.append(f"State GET failed: {e}")

    stop_event.set()
    worker_thread.join(timeout=2)

    # Final verification of interval deltas
    t_elapsed = time.monotonic() - t0
    final_state = get_json("/api/state")
    final_cm = final_state["camera_metrics"]

    interval_frames = final_cm["total_frames"] - init_frames
    interval_gaps = final_cm["sequence_gaps"] - init_gaps
    interval_anomalies = final_cm["bad_intervals"] - init_anomalies
    interval_fps = interval_frames / t_elapsed

    print("\n==========================================")
    print("     65s INTERVAL SOAK TEST RESULTS       ")
    print("==========================================")
    print(f"Interval Elapsed Time : {t_elapsed:.2f} s")
    print(f"Interval Frames Added : {interval_frames}")
    print(f"Trial-Local FPS       : {interval_fps:.3f} fps")
    print(f"Interval Gaps (lost)  : {interval_gaps}")
    print(f"Interval Anomalies    : {interval_anomalies}")
    print(f"Control Changes Made  : {control_changes}")
    print(f"API Errors            : {len(errors)}")
    print(f"Daemon Lifetime Frames: {final_cm['total_frames']}")

    min_expected_frames = math.floor(duration_s * 118.0)
    passed = (
        interval_gaps == 0
        and len(errors) == 0
        and 118.0 <= interval_fps <= 122.0
        and control_changes >= 15
        and interval_frames >= min_expected_frames
    )
    print(f"FINAL RESULT: {'PASS' if passed else 'FAIL'}")
    print("==========================================")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
