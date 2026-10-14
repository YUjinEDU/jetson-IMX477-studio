// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_SENSOR_RECORDER_H
#define STUDIO_SENSOR_RECORDER_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct OriginalRecordingStatus {
    bool recording{false};
    bool ready{false};
    std::string path;
    std::string metadata_url;
    std::string error;
    uint64_t frames{0};
    uint64_t dropped_frames{0};
    uint32_t width{0};
    uint32_t height{0};
    double fps{0.0};
    double actual_fps{0.0};
    double buffered_sec{0.0};
};

struct FrameMetadata {
    uint64_t sequence{0};
    uint64_t sensor_timestamp_ns{0};
    uint64_t exposure_us{0};
    float analog_gain{0.0f};
    float isp_digital_gain{0.0f};
    uint32_t awb_cct{0};
};

class SensorRecorder {
public:
    SensorRecorder();
    ~SensorRecorder();

    bool start(const std::string& filepath, uint32_t width, uint32_t height, double fps);
    bool push_frame(const uint8_t* nv12_data, size_t size, const FrameMetadata& meta);
    bool stop();

    bool is_recording() const;
    OriginalRecordingStatus get_status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // STUDIO_SENSOR_RECORDER_H
