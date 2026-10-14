# Jetson IMX477 Studio

High-performance camera control studio and real-time 120fps ISP viewer for the **Sony IMX477** on **NVIDIA Jetson Orin Nano**.

Provides an intuitive browser-based control dashboard, zero-drop frame acquisition, live shooting assists, and low-latency shared-memory fan-out for concurrent AI model inference (e.g. YOLO, TrackNet).

---

## Key Features

- **120fps Hardware ISP Capture**: Runs Sony IMX477 sensor mode 2 (1332&times;990 @ 120fps) via Direct Libargus without GStreamer lockstep bottlenecks.
- **Full Decoupling**: 120fps hardware ingestion loop is isolated from display rendering and downstream consumers. Zero dropped camera frames even during slow network clients.
- **Web Studio Interface**: Control camera parameters from local display or remote laptop via browser (`http://<jetson-ip>:8477`).
- **Live Runtime Controls**:
  - Exposure time (13&micro;s to 8,333&micro;s) and analog gain (1.0&times; to 22.25&times;) updated live without capture restarts.
  - Auto-Exposure (AE) with customizable exposure & gain ceiling, AE lock, and EV compensation.
  - Antibanding / Anti-flicker (Auto, 60Hz, 50Hz, Off) for indoor AC lighting.
  - Auto White Balance (AWB) presets, AWB lock, and correlated color temperature (CCT) telemetry.
  - ISP digital gain, hardware denoise, and edge enhancement controls.
- **Shooting & Optics Assists**:
  - **Focus Peaking**: Real-time edge highlighting in green, red, or cyan with adjustable sensitivity.
  - **Zebra Striping**: Diagonal zebra pattern overexposed highlight warning.
  - **Focus Sharpness Meter**: Live Laplacian variance score with peak-hold indicator for manual lens focusing.
  - **Luma Histogram**: 64-bin distribution bar graph updated at 4Hz.
  - **Motion Blur Calculator**: Enter object speed (e.g., 140 km/h baseball) to calculate theoretical motion streak in centimeters and sensor pixels.
  - **Pan & Zoom**: Fit, 1:1 Native pixel crop, and 2&times; digital crop with click-to-center navigation.
- **Full-Resolution Snapshots**: Saves sensor-resolution JPEG images with timestamped JSON metadata sidecars, including 3840&times;2160 in 4K mode.
- **Multi-Process AI Fanout (`StudioCamera`)**: High-speed POSIX shared-memory ring buffer (64 slots) with seqlock synchronization and futex wakeups. Allows Python AI pipelines (YOLO) to consume raw BGR frames concurrently at a sustained **120+ fps with 0 drops**.

---

## Architecture

```text
               +-------------------------------------------+
               |     Sony IMX477 Sensor (Mode 2)           |
               |             1332x990 @ 120fps             |
               +---------------------+---------------------+
                                     |
                                     v
               +-------------------------------------------+
               |      NVIDIA Jetson HW ISP / Libargus      |
               +----------+---------------------+----------+
                          |                     |
     [Stream A: 1332x990] |                     | [Stream B: 320x238]
                          v                     v
+------------------------------------+  +------------------------------------+
| C++ Main Consumer Thread (120 fps) |  | C++ AI Consumer Thread (120 fps)   |
| - Sensor interval & gap telemetry  |  | - Reader heartbeat detection       |
| - Live request updates via repeat()|  | - Pitch-linear NV12 NvBuffer copy  |
+-----------------+------------------+  +-----------------+------------------+
                  |                                       |
    [30fps copy]  v                                       v [Zero-copy write]
+------------------------------------+  +------------------------------------+
| Display Pipeline Thread (30 fps)   |  | POSIX Shared Memory Ring Buffer    |
| - Fast CPU color conversion        |  | (/dev/shm/imx477_studio_ai)        |
| - Focus peaking, zebra, zoom/crop  |  | - 64 slots, seqlock synchronization|
| - libjpeg-turbo memory encode      |  +-----------------+------------------+
+-----------------+------------------+                    |
                  |                                       v
                  v                             +-------------------+
+------------------------------------+          | Python Client     |
| Embedded HTTP Server (cpp-httplib) |          | (StudioCamera)    |
| - MJPEG Stream (/stream.mjpg)      |          | - YOLO Inference  |
| - SSE Telemetry (/events)          |          | - 120.3 fps PASS  |
| - REST Control API (/api/controls) |          +-------------------+
+-----------------+------------------+
                  |
                  v
       +----------------------+
       | Web Browser Studio   |
       | Localhost or Remote  |
       +----------------------+
```

---

## Prerequisites

1. **Sony IMX477 120fps Driver & NITO**:
   The camera driver and mode 2 ISP tuning file must be installed on your Jetson. See the sibling repository:
   [IMX477-120fps](https://github.com/YUjinEDU/IMX477-120fps)
2. **System Dependencies**:
   - JetPack 7.2.1 / L4T R39.2.1 (kernel `6.8.12-1021-tegra`)
   - OpenCV 4.6 (`libopencv-core-dev`, `libopencv-imgproc-dev`, `libopencv-imgcodecs-dev`)
   - Libjpeg development headers (`libjpeg-dev`)
   - NVIDIA Jetson Multimedia API (`/usr/src/jetson_multimedia_api`)

---

## Build & Quickstart

Clone the repository and build using standard `make`:

```bash
git clone https://github.com/YUjinEDU/jetson-IMX477-studio.git
cd jetson-IMX477-studio
make -j4
```

### Launch Studio Service

```bash
# Local access only (binds 127.0.0.1:8477)
./bin/studio

# Remote access (allow browser connection from laptop on same LAN)
./bin/studio --host 0.0.0.0 --port 8477
```

### Open the Web Studio

Open your browser and navigate to:
```text
http://<jetson-ip>:8477
```

### Preview, Sensor Modes & Replay

- Sensor modes: **4K/30**, **1080/60**, and **1332×990/120**. A mode change
  releases the Argus session and provider before reopening the camera; the UI
  serializes mode changes. Failed startup cleans up its partial session, and
  stopped/stale capture reports zero FPS rather than the previous live value.
- Preview targets **30fps independently of sensor FPS**. Pitched Y/chroma planes
  are resized before RGB conversion; full-resolution RGB is generated only for
  snapshots. After the surface has been read, capture is rearmed while overlays
  and JPEG encoding process the independent RGB image.
- Browsers retain native MJPEG when it has painted a frame. Single-JPEG fallback
  is decode-aware and capped at the selected preview FPS; reloading the stream
  cancels the old fallback loop.
- **MEDIA → SLOW-MO REPLAY** decodes recorded AVI frames through the clip API,
  avoiding the browser's lack of native MJPEG-in-AVI playback. Replay prefetches
  a bounded set of decoded frames, supports 0.1×/0.25×/0.5×/1× playback and exact
  one-frame stepping, and ignores cancelled scrub/close/clip-switch responses.
- Recordings and pitch clips contain **preview frames**, not the full-rate
  120fps sensor stream. Replay timing uses the clip's actual container FPS.
  Network/decode delays can reduce playback speed; frames are not advanced
  before they have decoded. Motion detection uses clean pre-overlay grayscale.
- `GET /api/clip/info?path=/recordings/<file>.avi` returns FPS, frame count,
  dimensions and duration. `GET /api/clip/frame?path=...&frame=N` returns JPEG.
  Both resolve validated files strictly beneath `recordings/`; traversal and
  escaping symlinks are rejected. Malformed/negative/overflowing frame indices
  return **400**; missing files or frames return **404**. A bounded decoder
  cache reuses consecutive reads and invalidates on file size/mtime changes.

Local hardware/Firefox verification (2026-10-14): mode transitions 120→60→30→120,
4K preview 29.84fps, 1080 preview 30.10fps, and 120-mode preview 30.13fps.
A 66.06s hardware soak captured 7,938 frames at 120.160fps with zero sequence
gaps, one sensor-interval anomaly, and no API errors. Gallery clicks, all replay
speeds, one-frame stepping, delayed rapid scrubbing and close cancellation were
exercised in Firefox. Remote Windows playback latency was not measured.

---

## Python Client (`StudioCamera`)

Downstream AI inference models can connect to the live camera stream concurrently via shared memory without opening the camera hardware directly:

```python
import sys
sys.path.insert(0, "path/to/jetson-IMX477-studio/python")
from jetson_camera_studio import StudioCamera

# Open client in FIFO mode (sequential frames)
with StudioCamera(mode="fifo") as cam:
    print(f"Connected to stream: {cam.width}x{cam.height}")
    while True:
        # Returns BGR numpy array and full hardware metadata
        frame, meta = cam.read(timeout_ms=1000)

        seq = meta["sequence"]
        exp_us = meta["exposure_ns"] // 1000
        gain = meta["analog_gain"]

        # Run your detector or processing pipeline
        # cv2.imshow("Preview", frame)
```

Supported modes:
- `mode="fifo"`: Strict sequential delivery. Raises overrun warning if reader processing time consistently falls behind 120fps.
- `mode="latest"`: Always returns the newest available frame. Skips intermediate frames if consumer is slower than 120fps.

---

## Verified Benchmarks

Tested on **NVIDIA Jetson Orin Nano Super 8GB**, L4T R39.2.1, Sony IMX477 sensor:

| Test Scenario | Duration | Total Frames | Sustained FPS | Sequence Gaps | Client Drops | Result |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **60s Stress Soak** (22 live HTTP control changes) | 65.0s | 12,274 | 119.89 fps | **0** | **0** | **PASS** |
| **Concurrent YOLO26n** (Live Web Stream + TRT FP16) | 5.0s | 600 | 120.35 fps | **0** | **0** | **PASS** |
| **SHM Ring Concurrency** (Writer + Reader Threads) | — | 1,000 | 120.00 fps | **0** | **0** | **PASS** |

---

## Safety & Best Practices

- **Single Camera Owner**: NVIDIA Argus only permits one active process to own `/dev/video0`. Close any standalone capture tools before starting `studio`:
  ```bash
  pgrep -fa 'argus_fps_probe|isp_pipeline|capture120|studio'
  ```
- **Driver Module**: Never hot-unload or unbind `nv_imx477`. Kernel modules are loaded during system boot.
- **Clean Exit**: Terminate the studio service using `Ctrl+C` or `SIGTERM` to allow clean release of Argus sessions and unmapping of shared memory segments.

---

## License

Licensed under the **Apache License, Version 2.0**. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for details.
Third-party components: `cpp-httplib` (MIT), `nlohmann/json` (MIT).
