#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""Exercise the Python reader against an isolated, real NV12 SHM frame."""
import ctypes as C
from contextlib import contextmanager
import mmap
import os
from pathlib import Path
import sys
import tempfile
import unittest
import uuid

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from jetson_camera_studio import StudioCamera


# Fixture layout mirrors include/studio/shm_protocol.h. Publication is
# synchronous, before read(): this fixture does not simulate atomic races.
class ShmHeader(C.Structure):
    _pack_ = 8
    _fields_ = [
        ("magic", C.c_char * 8), ("version", C.c_uint32),
        ("num_slots", C.c_uint32), ("width", C.c_uint32),
        ("height", C.c_uint32), ("slot_stride_bytes", C.c_uint32),
        ("writer_pid", C.c_uint32), ("session_id", C.c_uint64),
        ("latest_seq", C.c_uint64), ("futex_word", C.c_uint32),
        ("reader_heartbeat_ns", C.c_uint64),
    ]


class SlotHeader(C.Structure):
    _pack_ = 8
    _fields_ = [
        ("seqlock", C.c_uint64), ("sequence", C.c_uint64),
        ("sensor_timestamp_ns", C.c_uint64), ("host_timestamp_ns", C.c_uint64),
        ("exposure_ns", C.c_uint64), ("analog_gain", C.c_float),
        ("isp_digital_gain", C.c_float), ("awb_cct", C.c_uint32),
        ("flags", C.c_uint32), ("width", C.c_uint32),
        ("height", C.c_uint32), ("payload_bytes", C.c_uint32),
    ]


@contextmanager
def color_frame_reader():
    width, height = 12, 4
    # BT.601 limited-range NV12: four-pixel red, blue and neutral-gray bars.
    y = np.tile(np.repeat([82, 41, 128], 4), (height, 1)).astype(np.uint8)
    uv = np.tile([90, 240, 90, 240, 240, 110, 240, 110, 128, 128, 128, 128],
                 (height // 2, 1)).astype(np.uint8)
    payload = y.tobytes() + uv.tobytes()
    stride = (C.sizeof(SlotHeader) + len(payload) + 7) & ~7
    fd, path = tempfile.mkstemp(prefix="studio_color_", dir="/dev/shm")
    memory = header = slot = camera = None
    try:
        os.ftruncate(fd, C.sizeof(ShmHeader) + 2 * stride)
        memory = mmap.mmap(fd, 0)
        header = ShmHeader.from_buffer(memory)
        header.magic = b"IMX477S1"
        header.version = 1
        header.num_slots = 2
        header.width, header.height = width, height
        header.slot_stride_bytes = stride
        header.writer_pid = os.getpid()
        header.session_id = 1
        camera = StudioCamera(mode="fifo", shm_name="/" + Path(path).name)
        offset = C.sizeof(ShmHeader) + stride  # sequence 1 uses slot 1
        slot = SlotHeader.from_buffer(memory, offset)
        slot.seqlock = 1
        slot.sequence = 1
        slot.width, slot.height = width, height
        slot.payload_bytes = len(payload)
        start = offset + C.sizeof(SlotHeader)
        memory[start:start + len(payload)] = payload
        slot.seqlock = 2
        header.latest_seq = 1
        yield camera
    finally:
        if camera is not None:
            camera.close()
        slot = header = None
        if memory is not None:
            memory.close()
        os.close(fd)
        os.unlink(path)


class StudioCameraTest(unittest.TestCase):
    def test_nv12_preserves_red_blue_and_neutral_gray(self):
        with color_frame_reader() as camera:
            frame, _ = camera.read(timeout_ms=0)
        red = frame[:, :4]
        blue = frame[:, 4:8]
        gray = frame[:, 8:]
        self.assertTrue(np.all(red[:, :, 2] >= 250))
        self.assertTrue(np.all(red[:, :, :2] <= 3))
        self.assertTrue(np.all(blue[:, :, 0] >= 250))
        self.assertTrue(np.all(blue[:, :, 1:] <= 3))
        self.assertTrue(np.all(np.ptp(gray, axis=2) == 0))
        self.assertTrue(np.all((gray >= 128) & (gray <= 132)))

    def test_missing_stream_raises_connection_error(self):
        with self.assertRaises(ConnectionError):
            StudioCamera(shm_name="/studio_missing_" + uuid.uuid4().hex)


if __name__ == "__main__":
    unittest.main()
