// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_DISPLAY_PIPELINE_H
#define STUDIO_DISPLAY_PIPELINE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CameraService;

struct DisplaySettings {
    std::string zoom_mode = "fit"; // "fit", "1x", "2x"
    float center_x = 0.5f;         // normalized [0.0 .. 1.0]
    float center_y = 0.5f;
    bool focus_peaking = false;
    std::string peaking_color = "green"; // "green", "red", "cyan"
    int peaking_threshold = 30;          // 5 .. 100
    bool zebra_stripes = false;
    int zebra_threshold = 95;            // 70 .. 100 (% of 255)
    std::string grid_mode = "none";      // "none", "rule_of_thirds", "crosshair"
    int jpeg_quality = 75;               // 30 .. 95
    int fps_limit = 30;                  // 10 .. 60
};

struct DisplayMetrics {
    double display_fps = 0.0;
    double encode_ms = 0.0;
    double focus_score = 0.0;
    double focus_score_max = 0.0;
    uint32_t active_viewers = 0;
    std::vector<uint32_t> histogram_64; // 64 bins of luma distribution
};

class DisplayPipeline {
public:
    explicit DisplayPipeline(CameraService& camera);
    ~DisplayPipeline();

    bool start();
    void stop();

    DisplaySettings get_settings() const;
    void update_settings(const DisplaySettings& s);
    DisplayMetrics get_metrics() const;

    // Called by HTTP server to fetch latest JPEG frame for MJPEG streaming.
    // Blocks up to timeout_ms waiting for a frame newer than last_seen_seq.
    // Returns true on new frame.
    bool get_latest_jpeg(std::vector<uint8_t>& out_jpeg, uint64_t& in_out_seq,
                         int timeout_ms = 200);

    void increment_viewers();
    void decrement_viewers();
    std::string capture_snapshot(const std::string& directory = "snapshots");


private:
    CameraService& camera_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    mutable std::mutex settings_mu_;
    DisplaySettings settings_;

    mutable std::mutex frame_mu_;
    std::condition_variable frame_cv_;
    std::vector<uint8_t> latest_jpeg_;
    uint64_t latest_seq_{0};

    std::mutex snapshot_mu_;
    std::condition_variable snapshot_cv_;
    bool snapshot_requested_{false};
    bool snapshot_done_{false};
    std::string snapshot_dir_;
    std::string snapshot_result_path_;

    mutable std::mutex metrics_mu_;
    DisplayMetrics metrics_;
    std::atomic<uint32_t> viewers_count_{0};

    void run();
};

#endif // STUDIO_DISPLAY_PIPELINE_H
