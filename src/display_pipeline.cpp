// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/display_pipeline.h"
#include "studio/camera_service.h"

#include <nvbufsurface.h>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <cmath>
#include <jpeglib.h>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>

static bool encode_jpeg_bgr(const cv::Mat& bgr, std::vector<uint8_t>& out_jpeg, int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* mem = nullptr;
    unsigned long mem_size = 0;
    jpeg_mem_dest(&cinfo, &mem, &mem_size);

    cinfo.image_width = bgr.cols;
    cinfo.image_height = bgr.rows;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_EXT_BGR;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row_pointer = (JSAMPROW)(bgr.data + cinfo.next_scanline * bgr.step);
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    if (mem && mem_size > 0) {
        out_jpeg.assign(mem, mem + mem_size);
        free(mem);
        return true;
    }
    return false;
}

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

DisplayPipeline::DisplayPipeline(CameraService& camera)
    : camera_(camera) {
    metrics_.histogram_64.assign(64, 0);
}

DisplayPipeline::~DisplayPipeline() {
    stop();
}

DisplaySettings DisplayPipeline::get_settings() const {
    std::lock_guard<std::mutex> lock(settings_mu_);
    return settings_;
}

void DisplayPipeline::update_settings(const DisplaySettings& s) {
    std::lock_guard<std::mutex> lock(settings_mu_);
    settings_ = s;
}

DisplayMetrics DisplayPipeline::get_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mu_);
    auto m = metrics_;
    m.active_viewers = viewers_count_.load();
    return m;
}

void DisplayPipeline::increment_viewers() {
    viewers_count_.fetch_add(1);
}

void DisplayPipeline::decrement_viewers() {
    uint32_t val = viewers_count_.load(std::memory_order_relaxed);
    while (val > 0 && !viewers_count_.compare_exchange_weak(val, val - 1, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

bool DisplayPipeline::start() {
    if (running_.load()) return true;
    running_.store(true);
    worker_ = std::thread(&DisplayPipeline::run, this);
    return true;
}

void DisplayPipeline::stop() {
    if (!running_.exchange(false)) return;
    frame_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool DisplayPipeline::get_latest_jpeg(std::vector<uint8_t>& out_jpeg,
                                      uint64_t& in_out_seq, int timeout_ms) {
    std::unique_lock<std::mutex> lock(frame_mu_);
    if (!frame_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return latest_seq_ > in_out_seq || !running_.load(); })) {
        return false;
    }
    if (!running_.load() || latest_seq_ <= in_out_seq) return false;

    out_jpeg = latest_jpeg_;
    in_out_seq = latest_seq_;
    return true;
}

void DisplayPipeline::run() {
    uint64_t prev_frame_time = now_ns();
    std::vector<double> fps_history;
    uint64_t fps_window_start = now_ns();
    int frames_in_window = 0;

    const int target_w = 960;
    const int target_h = 714;
    cv::Mat bgr, final_view;
    std::vector<uint8_t> encoded;

    while (running_.load()) {
        DisplaySettings s;
        {
            std::lock_guard<std::mutex> lock(settings_mu_);
            s = settings_;
        }

        // Rate limiter
        int fps_target = std::clamp(s.fps_limit, 10, 60);
        uint64_t frame_interval_ns = 1000000000ULL / fps_target;
        uint64_t now = now_ns();
        if (now < prev_frame_time + frame_interval_ns) {
            std::this_thread::sleep_for(std::chrono::nanoseconds((prev_frame_time + frame_interval_ns) - now));
        }
        prev_frame_time = now_ns();

        // If no one is watching and no snapshot requested, sleep lightly and loop
        bool need_snap = false;
        {
            std::lock_guard<std::mutex> lock(snapshot_mu_);
            need_snap = snapshot_requested_;
        }
        if (viewers_count_.load() == 0 && !need_snap) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        NvBufSurface* surface = nullptr;
        uint64_t seq = 0, sensor_ts = 0;
        if (!camera_.acquire_display_frame(&surface, &seq, &sensor_ts, 150)) {
            continue;
        }
        try {
        uint64_t t0 = now_ns();
        const auto& p = surface->surfaceList[0];
        const uint32_t src_w = 1332;
        const uint32_t src_h = 990;

        if (!p.mappedAddr.addr[0] || !p.mappedAddr.addr[1]) {
            fprintf(stderr, "[DBG] Null mappedAddr!\n");
            continue;
        }

        cv::Mat y_plane(src_h, src_w, CV_8UC1, (void*)p.mappedAddr.addr[0], p.planeParams.pitch[0]);
        int cx = src_w / 2, cy = src_h / 2;
        cv::Rect focus_roi_rect(cx - 120, cy - 120, 240, 240);
        cv::Mat focus_roi = y_plane(focus_roi_rect).clone();
        cv::Mat lap;
        cv::Laplacian(focus_roi, lap, CV_16S, 3);
        cv::Scalar mean, stddev;
        cv::meanStdDev(lap, mean, stddev);
        double cur_focus_score = stddev[0] * stddev[0];
        std::vector<uint32_t> hist(64, 0);
        for (int r = 0; r < (int)src_h; r += 4) {
            const uint8_t* row = y_plane.ptr<uint8_t>(r);
            for (int c = 0; c < (int)src_w; c += 4) {
                hist[row[c] >> 2]++;
            }
        }

        // 3. Zoom / Crop ROI & Color Conversion
        const uint8_t* y_data = static_cast<const uint8_t*>(p.mappedAddr.addr[0]);
        uint32_t y_pitch = p.planeParams.pitch[0];
        const uint8_t* uv_data = static_cast<const uint8_t*>(p.mappedAddr.addr[1]);
        uint32_t uv_pitch = p.planeParams.pitch[1];
        cv::Mat full_bgr;

        if (s.zoom_mode == "1x" || s.zoom_mode == "2x") {
            int crop_w = (s.zoom_mode == "1x") ? 960 : 480;
            int crop_h = (s.zoom_mode == "1x") ? 714 : 356;
            int rx = std::clamp((int)(s.center_x * src_w) - crop_w / 2, 0, (int)src_w - crop_w) & ~1;
            int ry = std::clamp((int)(s.center_y * src_h) - crop_h / 2, 0, (int)src_h - crop_h) & ~1;

            cv::Mat crop_yuv(crop_h * 3 / 2, crop_w, CV_8UC1);
            for (int r = 0; r < crop_h; ++r) {
                memcpy(crop_yuv.data + (size_t)r * crop_w, y_data + (size_t)(ry + r) * y_pitch + rx, crop_w);
            }
            size_t uv_off = (size_t)crop_w * crop_h;
            for (int r = 0; r < crop_h / 2; ++r) {
                memcpy(crop_yuv.data + uv_off + (size_t)r * crop_w, uv_data + (size_t)(ry / 2 + r) * uv_pitch + rx, crop_w);
            }
            cv::Mat crop_bgr;
            cv::cvtColor(crop_yuv, crop_bgr, cv::COLOR_YUV2BGR_NV12);
            if (s.zoom_mode == "2x") {
                cv::resize(crop_bgr, bgr, cv::Size(target_w, target_h), 0, 0, cv::INTER_NEAREST);
            } else {
                bgr = crop_bgr;
            }
        } else {
            // "fit": full-res pack and resize
            cv::Mat yuv(src_h * 3 / 2, src_w, CV_8UC1);
            for (int r = 0; r < (int)src_h; ++r) {
                memcpy(yuv.data + (size_t)r * src_w, y_data + (size_t)r * y_pitch, src_w);
            }
            size_t uv_off = (size_t)src_w * src_h;
            for (int r = 0; r < (int)src_h / 2; ++r) {
                memcpy(yuv.data + uv_off + (size_t)r * src_w, uv_data + (size_t)r * uv_pitch, src_w);
            }
            cv::cvtColor(yuv, full_bgr, cv::COLOR_YUV2BGR_NV12);
            cv::resize(full_bgr, bgr, cv::Size(target_w, target_h), 0, 0, cv::INTER_AREA);
        }
        // Snapshot handler on full-res frame
        {
            std::lock_guard<std::mutex> lock(snapshot_mu_);
            if (snapshot_requested_ && !snapshot_done_) {
                cv::Mat snap_bgr;
                if (s.zoom_mode == "fit") {
                    snap_bgr = full_bgr;
                } else {
                    cv::Mat full_yuv(src_h * 3 / 2, src_w, CV_8UC1);
                    for (int r = 0; r < (int)src_h; ++r) {
                        memcpy(full_yuv.data + (size_t)r * src_w, y_data + (size_t)r * y_pitch, src_w);
                    }
                    size_t uv_off = (size_t)src_w * src_h;
                    for (int r = 0; r < (int)src_h / 2; ++r) {
                        memcpy(full_yuv.data + uv_off + (size_t)r * src_w, uv_data + (size_t)r * uv_pitch, src_w);
                    }
                    cv::cvtColor(full_yuv, snap_bgr, cv::COLOR_YUV2BGR_NV12);
                }
                std::vector<uint8_t> snap_jpg;
                if (encode_jpeg_bgr(snap_bgr, snap_jpg, 95)) {
                    auto t = std::time(nullptr);
                    struct tm tm;
                    localtime_r(&t, &tm);
                    std::ostringstream oss;
                    oss << snapshot_dir_ << "/snapshot_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << seq;
                    std::string base = oss.str();
                    std::string jpg_p = base + ".jpg";
                    std::string json_p = base + ".json";

                    std::ofstream f(jpg_p, std::ios::binary);
                    if (f) {
                        f.write(reinterpret_cast<const char*>(snap_jpg.data()), snap_jpg.size());
                        f.close();
                        snapshot_result_path_ = jpg_p;
                        snapshot_done_ = true;

                        auto cm = camera_.get_metrics();
                        std::ofstream jf(json_p);
                        if (jf) {
                            jf << "{\n"
                               << "  \"sequence\": " << seq << ",\n"
                               << "  \"sensor_timestamp_ns\": " << sensor_ts << ",\n"
                               << "  \"exposure_us\": " << cm.actual_exposure_us << ",\n"
                               << "  \"analog_gain\": " << cm.actual_analog_gain << ",\n"
                               << "  \"isp_digital_gain\": " << cm.actual_isp_gain << ",\n"
                               << "  \"awb_cct\": " << cm.actual_cct << ",\n"
                               << "  \"width\": 1332,\n"
                               << "  \"height\": 990\n"
                               << "}\n";
                        }
                    }
                }
                snapshot_requested_ = false;
                snapshot_cv_.notify_all();
            }
        }


        // Grayscale conversion shared by Peaking and Zebra
        cv::Mat gray;
        if (s.focus_peaking || s.zebra_stripes) {
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        }

        // 4. Overlays: Focus Peaking
        if (s.focus_peaking) {
            cv::Mat edges;
            cv::Laplacian(gray, edges, CV_8U, 3);
            cv::Vec3b peak_color = (s.peaking_color == "red") ? cv::Vec3b(0, 0, 255)
                                 : (s.peaking_color == "cyan") ? cv::Vec3b(255, 255, 0)
                                 : cv::Vec3b(0, 255, 0); // default green
            int thresh = std::clamp(s.peaking_threshold, 5, 100);
            for (int r = 0; r < bgr.rows; ++r) {
                const uint8_t* edge_row = edges.ptr<uint8_t>(r);
                cv::Vec3b* bgr_row = bgr.ptr<cv::Vec3b>(r);
                for (int c = 0; c < bgr.cols; ++c) {
                    if (edge_row[c] > thresh) {
                        bgr_row[c] = peak_color;
                    }
                }
            }
        }

        // 5. Overlays: Zebra Stripes (overexposure warning)
        if (s.zebra_stripes) {
            int z_thresh = std::clamp(s.zebra_threshold, 70, 100) * 255 / 100;
            for (int r = 0; r < bgr.rows; ++r) {
                const uint8_t* g_row = gray.ptr<uint8_t>(r);
                cv::Vec3b* bgr_row = bgr.ptr<cv::Vec3b>(r);
                for (int c = 0; c < bgr.cols; ++c) {
                    if (g_row[c] >= z_thresh && ((r + c) % 12 < 6)) {
                        bgr_row[c] = cv::Vec3b(0, 0, 0); // black zebra stripes
                    }
                }
            }
        }

        // 6. Overlays: Grid / Crosshair
        if (s.grid_mode == "crosshair") {
            int mid_x = bgr.cols / 2;
            int mid_y = bgr.rows / 2;
            cv::line(bgr, cv::Point(mid_x - 15, mid_y), cv::Point(mid_x + 15, mid_y), cv::Scalar(180, 180, 180), 1);
            cv::line(bgr, cv::Point(mid_x, mid_y - 15), cv::Point(mid_x, mid_y + 15), cv::Scalar(180, 180, 180), 1);
        } else if (s.grid_mode == "rule_of_thirds") {
            int x1 = bgr.cols / 3, x2 = 2 * bgr.cols / 3;
            int y1 = bgr.rows / 3, y2 = 2 * bgr.rows / 3;
            cv::line(bgr, cv::Point(x1, 0), cv::Point(x1, bgr.rows), cv::Scalar(100, 100, 100), 1);
            cv::line(bgr, cv::Point(x2, 0), cv::Point(x2, bgr.rows), cv::Scalar(100, 100, 100), 1);
            cv::line(bgr, cv::Point(0, y1), cv::Point(bgr.cols, y1), cv::Scalar(100, 100, 100), 1);
            cv::line(bgr, cv::Point(0, y2), cv::Point(bgr.cols, y2), cv::Scalar(100, 100, 100), 1);
        }

        // 7. JPEG Encode
        encoded.clear();
        encode_jpeg_bgr(bgr, encoded, std::clamp(s.jpeg_quality, 30, 95));
        double enc_ms = (now_ns() - t0) / 1000000.0;

        // 8. Publish Frame
        {
            std::lock_guard<std::mutex> lock(frame_mu_);
            latest_jpeg_ = std::move(encoded);
            latest_seq_ = seq;
            frame_cv_.notify_all();
        }

        // 9. Update Metrics
        frames_in_window++;
        uint64_t win_now = now_ns();
        if (win_now - fps_window_start >= 1000000000ULL) {
            double dfps = frames_in_window * 1e9 / (win_now - fps_window_start);
            frames_in_window = 0;
            fps_window_start = win_now;

            std::lock_guard<std::mutex> lock(metrics_mu_);
            metrics_.display_fps = dfps;
            metrics_.encode_ms = enc_ms;
            metrics_.focus_score = cur_focus_score;
            metrics_.focus_score_max = std::max(metrics_.focus_score_max, cur_focus_score);
            metrics_.histogram_64 = std::move(hist);
        }
        } catch (const std::exception& e) {
            fprintf(stderr, "[ERROR] DisplayPipeline exception: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "[ERROR] DisplayPipeline unknown exception\n");
        }
    }
}

std::string DisplayPipeline::capture_snapshot(const std::string& directory) {
    if (!running_.load()) return "";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    std::unique_lock<std::mutex> lock(snapshot_mu_);
    snapshot_dir_ = directory;
    snapshot_result_path_.clear();
    snapshot_requested_ = true;
    snapshot_done_ = false;

    if (!snapshot_cv_.wait_for(lock, std::chrono::milliseconds(2500),
                               [&] { return snapshot_done_ || !running_.load(); })) {
        snapshot_requested_ = false;
        return "";
    }
    return snapshot_result_path_;
}
