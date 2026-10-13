// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_CAMERA_SERVICE_H
#define STUDIO_CAMERA_SERVICE_H

#include "studio/shm_protocol.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct NvBufSurface;

struct CameraCapabilities {
    uint32_t width = 1332;
    uint32_t height = 990;
    double target_fps = 120.0;
    uint64_t min_exposure_us = 13;
    uint64_t max_exposure_us = 8333; // clamped to 120fps frame period
    float min_gain = 1.0f;
    float max_gain = 22.25f;
    float min_isp_gain = 1.0f;
    float max_isp_gain = 8.0f;
};

struct CameraControls {
    bool auto_exposure = false;
    uint64_t exposure_us = 4000;
    float gain = 2.0f;
    uint64_t ae_max_exposure_us = 8000;
    float ae_max_gain = 16.0f;
    bool ae_lock = false;
    float ev_compensation = 0.0f; // -2.0 .. +2.0
    std::string antibanding = "auto"; // "off", "auto", "50hz", "60hz"
    std::string awb_mode = "auto"; // "off", "auto", "incandescent", "fluorescent", "daylight", "cloudy", "shade"
    bool awb_lock = false;
    float awb_gains[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // R, GE, GO, B
    float isp_digital_gain = 1.0f;
    std::string denoise_mode = "fast"; // "off", "fast", "high_quality"
    float denoise_strength = 0.5f;
    std::string edge_mode = "fast"; // "off", "fast", "high_quality"
    float edge_strength = 0.5f;
};

struct CameraMetrics {
    uint64_t total_frames = 0;
    uint64_t sequence_gaps = 0;
    uint64_t bad_sensor_intervals = 0;
    double sensor_fps = 0.0;
    double host_fps = 0.0;
    double min_interval_ms = 0.0;
    double max_interval_ms = 0.0;
    uint64_t actual_exposure_us = 0;
    float actual_analog_gain = 0.0f;
    float actual_isp_gain = 0.0f;
    uint32_t actual_cct = 0;
    float actual_awb_gains[4] = {0};
    uint64_t last_sensor_ts_ns = 0;
    uint64_t last_host_ts_ns = 0;
    uint64_t last_sequence = 0;
    uint64_t uptime_ms = 0;
};

class CameraService {
public:
    CameraService();
    ~CameraService();

    bool start(int ai_width = 320, const std::string& shm_name = STUDIO_SHM_NAME);
    void stop();
    bool is_running() const { return running_.load(); }

    CameraCapabilities get_capabilities() const;
    CameraControls get_controls() const;
    CameraMetrics get_metrics() const;
    void update_controls(const CameraControls& c);

    // Called by DisplayPipeline to get a mapped NV12 surface.
    // Blocks up to timeout_ms waiting for the next capture frame.
    // Returns true if new frame acquired.
    bool acquire_display_frame(NvBufSurface** out_surface, uint64_t* out_seq,
                               uint64_t* out_sensor_ts, int timeout_ms = 100);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
};

#endif // STUDIO_CAMERA_SERVICE_H
