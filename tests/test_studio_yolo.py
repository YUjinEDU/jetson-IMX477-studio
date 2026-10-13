#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""Test running live YOLO TensorRT inference concurrently with Jetson IMX477 Studio."""
import math
import os
import sys
import time
from pathlib import Path
import cv2
import numpy as np

STUDIO_ROOT = Path(__file__).resolve().parents[1]
RESEARCH_ROOT = STUDIO_ROOT.parent / "baseball-trajectory-tracking"

sys.path.insert(0, str(STUDIO_ROOT / "python"))
sys.path.insert(0, str(RESEARCH_ROOT / "src/inference"))

from jetson_camera_studio import StudioCamera
from yolo_benchmark import TRT, prepare, decode


def main():
    engine_path = RESEARCH_ROOT / "models/yolo26n-320.engine"
    if not engine_path.is_file():
        print(f"Engine not found: {engine_path}")
        return 1

    print(f"Loading TensorRT engine: {engine_path.name}")
    backend = TRT(str(engine_path))
    size = backend.input_shape[2]

    # Warmup
    dummy = np.zeros(backend.input_shape, np.float32)
    for _ in range(10):
        backend.run(dummy)

    frames_to_test = 600  # ~5 seconds of 120fps
    print(f"Connecting to StudioCamera via SHM (target: {frames_to_test} frames)...")

    with StudioCamera(mode="fifo") as cam:
        print(f"Connected! Stream geometry: {cam.width}x{cam.height}")
        t0 = time.monotonic()
        inferred = 0
        first_seq = None
        last_seq = None
        prev_seq = None
        gaps = 0

        for i in range(frames_to_test):
            frame, meta = cam.read(timeout_ms=1000)
            seq = meta["sequence"]
            if first_seq is None:
                first_seq = seq
            if prev_seq is not None and seq != prev_seq + 1:
                gaps += (seq - prev_seq - 1)
            prev_seq = seq
            last_seq = seq

            # Preprocess, infer, decode
            x, transform = prepare(frame, size)
            raw = backend.run(x)
            dets = decode(raw, frame.shape, transform)
            inferred += 1

        elapsed = time.monotonic() - t0
        fps = inferred / elapsed
        dropped = cam.get_dropped_count()

        print("\n=== STUDIO + YOLO CONCURRENT RESULTS ===")
        print(f"Inferred frames : {inferred} / {frames_to_test}")
        print(f"Sequence range  : {first_seq} .. {last_seq} (span: {last_seq - first_seq + 1})")
        print(f"Sequence gaps   : {gaps}")
        print(f"Client drops    : {dropped}")
        print(f"Inference FPS   : {fps:.2f} fps (sustained)")
        print(f"Detections sample : {len(dets)} on final frame")

        backend.close()

        passed = (inferred == frames_to_test and gaps == 0 and dropped == 0 and fps >= 118.0)
        print(f"STATUS: {'PASS' if passed else 'FAIL'}")
        return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
