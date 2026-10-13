# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
"""High-performance shared memory camera reader for Jetson IMX477 Studio."""
import ctypes as C
import math
import os
from pathlib import Path
import time
import cv2
import numpy as np

# Locate libstudioclient.so
_LIB_DIRS = [
    Path(__file__).resolve().parents[2] / "build/bin",
    Path(__file__).resolve().parents[2] / "bin",
    Path("/usr/local/lib"),
    Path("/usr/lib"),
]


class _StudioFrameMeta(C.Structure):
    _fields_ = [
        ("sequence", C.c_uint64),
        ("sensor_timestamp_ns", C.c_uint64),
        ("host_timestamp_ns", C.c_uint64),
        ("exposure_ns", C.c_uint64),
        ("analog_gain", C.c_float),
        ("isp_digital_gain", C.c_float),
        ("awb_cct", C.c_uint32),
        ("flags", C.c_uint32),
        ("width", C.c_uint32),
        ("height", C.c_uint32),
        ("payload_bytes", C.c_uint32),
    ]


def _find_lib():
    for d in _LIB_DIRS:
        p = d / "libstudioclient.so"
        if p.is_file():
            return C.CDLL(str(p))
    try:
        return C.CDLL("libstudioclient.so")
    except OSError:
        pass
    raise RuntimeError(
        "libstudioclient.so not found. Run 'make' in jetson-IMX477-studio first."
    )


class StudioCamera:
    """Connects to the jetson-IMX477-studio background service via shared memory.

    Args:
        mode: "fifo" (sequential, will report drops if reader is slow) or
              "latest" (always returns newest captured frame).
        shm_name: POSIX shared memory name (default "/imx477_studio_ai").
        output_format: "bgr" (default, uint8 numpy array) or "nv12".
    """

    def __init__(self, mode="fifo", shm_name=None, output_format="bgr"):
        if mode not in ("fifo", "latest"):
            raise ValueError("mode must be 'fifo' or 'latest'")
        if output_format not in ("bgr", "nv12"):
            raise ValueError("output_format must be 'bgr' or 'nv12'")

        self.lib = _find_lib()
        self._setup_lib()

        c_mode = 1 if mode == "fifo" else 0
        b_name = shm_name.encode() if shm_name else None
        self._client = self.lib.studio_client_open(b_name, c_mode)
        if not self._client:
            raise ConnectionError(
                "Failed to connect to IMX477 Studio shared memory. "
                "Is the studio service running?"
            )

        self.output_format = output_format
        w = C.c_uint32(0)
        h = C.c_uint32(0)
        if self.lib.studio_client_get_format(self._client, C.byref(w), C.byref(h)) != 0:
            self.close()
            raise RuntimeError("Failed to read studio stream geometry")

        self.width = w.value
        self.height = h.value
        self.payload_bytes = self.width * self.height * 3 // 2
        self._raw_buf = bytearray(self.payload_bytes)
        self._raw_ptr = (C.c_uint8 * self.payload_bytes).from_buffer(self._raw_buf)
        self._meta = _StudioFrameMeta()

    def _setup_lib(self):
        self.lib.studio_client_open.argtypes = [C.c_char_p, C.c_int]
        self.lib.studio_client_open.restype = C.c_void_p

        self.lib.studio_client_close.argtypes = [C.c_void_p]
        self.lib.studio_client_close.restype = None

        self.lib.studio_client_get_format.argtypes = [
            C.c_void_p,
            C.POINTER(C.c_uint32),
            C.POINTER(C.c_uint32),
        ]
        self.lib.studio_client_get_format.restype = C.c_int

        self.lib.studio_client_get_dropped.argtypes = [C.c_void_p]
        self.lib.studio_client_get_dropped.restype = C.c_uint64

        self.lib.studio_client_read.argtypes = [
            C.c_void_p,
            C.c_void_p,
            C.c_size_t,
            C.POINTER(_StudioFrameMeta),
            C.c_int,
        ]
        self.lib.studio_client_read.restype = C.c_int

    def read(self, timeout_ms=2000):
        """Read one frame. Returns (image, metadata).

        Raises:
            TimeoutError: if no frame was produced within timeout_ms.
            RuntimeError: if the camera service disconnected or unrecoverable error.
        """
        if not self._client:
            raise RuntimeError("Camera client is closed")

        t0 = time.monotonic_ns()
        r = self.lib.studio_client_read(
            self._client,
            C.byref(self._raw_ptr),
            self.payload_bytes,
            C.byref(self._meta),
            int(timeout_ms),
        )

        if r == 1:
            raise TimeoutError(f"Studio camera frame timed out after {timeout_ms}ms")
        elif r < 0:
            raise RuntimeError("Studio camera read failed (service closed?)")

        # r == 0: normal; r == 2: overrun/dropped detected in FIFO mode
        w = self._meta.width
        h = self._meta.height

        # Reshape NV12
        nv12 = np.frombuffer(self._raw_buf, dtype=np.uint8).reshape((h * 3 // 2, w))

        if self.output_format == "bgr":
            t_conv_start = time.monotonic_ns()
            frame = cv2.cvtColor(nv12, cv2.COLOR_YUV2BGR_NV21)
            t_conv_end = time.monotonic_ns()
            color_ms = (t_conv_end - t_conv_start) / 1e6
        else:
            frame = nv12.copy()
            color_ms = 0.0

        meta = {
            "sequence": self._meta.sequence,
            "sensor_timestamp_ns": self._meta.sensor_timestamp_ns,
            "host_receive_ns": self._meta.host_timestamp_ns,
            "host_color_done_ns": time.monotonic_ns(),
            "color_ms": color_ms,
            "exposure_ns": self._meta.exposure_ns,
            "analog_gain": self._meta.analog_gain,
            "isp_digital_gain": self._meta.isp_digital_gain,
            "awb_cct": self._meta.awb_cct,
            "flags": self._meta.flags,
            "overrun": (r == 2),
            "width": w,
            "height": h,
        }
        return frame, meta

    def get_dropped_count(self):
        """Total frame drops observed by this reader."""
        if not self._client:
            return 0
        return self.lib.studio_client_get_dropped(self._client)

    def close(self):
        if self._client:
            self.lib.studio_client_close(self._client)
            self._client = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        self.close()
