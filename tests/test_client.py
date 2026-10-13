#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""Unit test for Python StudioCamera wrapper."""
import ctypes as C
import multiprocessing as mp
import os
import sys
import time
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from jetson_camera_studio import StudioCamera


def mock_writer(shm_name, num_frames=100):
    import posix_ipc  # or ctypes shm

    # Use C test helper or ctypes to initialize shm
    # We can also just use libstudioclient / ctypes
    pass


def main():
    print("Testing StudioCamera imports and error handling...")
    try:
        # Non-existent shm should raise ConnectionError
        StudioCamera(shm_name="/non_existent_shm_test")
        assert False, "Should have raised ConnectionError"
    except ConnectionError:
        print("  Properly raised ConnectionError on missing SHM")

    print("Python binding basic test passed!")


if __name__ == "__main__":
    main()
