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
- **Full-Resolution Snapshots**: Saves 1332&times;990 JPEG images with timestamped JSON metadata sidecars.
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
