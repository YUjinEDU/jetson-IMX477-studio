#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""Serial hardware regression: interrupt a pitch clip, switch mode, trigger again.

Requires an idle studio at localhost:8477, launched from the repository root.
Creates private probe clips, verifies every frame, then removes those files and
restores the initial mode/display/pitch configuration. Does not open the camera.
"""
import hashlib
import json
from pathlib import Path
import time
import urllib.parse
import urllib.request

import cv2

BASE = "http://127.0.0.1:8477"
ROOT = Path(__file__).resolve().parents[1]


def get(path, params=None):
    url = BASE + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=15) as response:
        return json.loads(response.read())


def post(path, data=None):
    request = urllib.request.Request(
        BASE + path, data=json.dumps(data or {}).encode(),
        headers={"Content-Type": "application/json"}, method="POST",
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.loads(response.read())


def clip_file(path):
    recording = ROOT / path
    assert recording.parent.resolve() == (ROOT / "recordings").resolve()
    return recording


def verify_frames(path, expected_height, minimum_frames):
    # Use the exact gallery URL, not a different filesystem-only path string.
    info = get("/api/clip/info", {"path": "/" + path})
    assert info["width"] == 960 and info["height"] == expected_height, info
    assert info["total_frames"] >= minimum_frames, info
    capture = cv2.VideoCapture(str(clip_file(path)))
    try:
        for index in range(info["total_frames"]):
            ok, frame = capture.read()
            assert ok and frame.shape[:2] == (expected_height, 960), (path, index)
    finally:
        capture.release()
    return info


def main():
    initial = get("/api/state")
    pitch = get("/api/pitch/status")
    assert not pitch["is_clipping"], "Run this regression only with no active pitch clip"
    assert not get("/api/record/status")["recording"], "Run with manual recording stopped"
    owned = []
    try:
        post("/api/pitch/config", {"enabled": False, "pre_roll_sec": 1.0, "post_roll_sec": 1.0})
        post("/api/display", {"fps_limit": 30})
        post("/api/sensor_mode", {"mode": 1})
        time.sleep(1.2)  # Populate one second of 960x540 pre-roll.
        first = post("/api/pitch/trigger")["path"]
        owned.append(first)
        post("/api/sensor_mode", {"mode": 2})  # Interrupt post-roll immediately.
        first_info = verify_frames(first, 540, 30)
        first_digest = hashlib.sha256(clip_file(first).read_bytes()).digest()
        second = post("/api/pitch/trigger")["path"]
        owned.append(second)
        assert first != second
        deadline = time.monotonic() + 10
        while get("/api/pitch/status")["is_clipping"]:
            assert time.monotonic() < deadline, "Second clip did not finalize"
            time.sleep(0.05)
        second_info = verify_frames(second, 714, 30)
        assert hashlib.sha256(clip_file(first).read_bytes()).digest() == first_digest, "First clip was reopened/truncated"
        assert get("/api/pitch/status")["total_clips"] == pitch["total_clips"] + 2
        print("PASS: immediate mode switch preserves first clip and produces a distinct, fully decoded second clip")
        print(json.dumps({"first": first_info, "second": second_info}, indent=2))
    finally:
        post("/api/sensor_mode", {"mode": initial["capabilities"]["active_sensor_mode"]})
        post("/api/display", initial["display_settings"])
        post("/api/pitch/config", {"enabled": pitch["enabled"], **pitch["config"]})
        for path in owned:
            clip_file(path).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
