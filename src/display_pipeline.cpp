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
    std::lock_guard<std::mutex> lock(lifecycle_mu_);
    if (running_.load()) return true;
    running_.store(true);
    clip_worker_running_.store(true);
    clip_worker_ = std::thread(&DisplayPipeline::clip_worker_loop, this);
    worker_ = std::thread(&DisplayPipeline::run, this);
    return true;
}

void DisplayPipeline::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mu_);
    if (!running_.exchange(false)) return;
    frame_cv_.notify_all();
    if (worker_.joinable()) worker_.join();

    clip_worker_running_.store(false);
    clip_queue_cv_.notify_all();
    if (clip_worker_.joinable()) clip_worker_.join();
}

bool DisplayPipeline::get_latest_jpeg(std::vector<uint8_t>& out_jpeg,
                                      uint64_t& in_out_seq, int timeout_ms) {
    std::unique_lock<std::mutex> lock(frame_mu_);
    if (in_out_seq == 0 && latest_seq_ > 0 && !latest_jpeg_.empty()) {
        out_jpeg = latest_jpeg_;
        in_out_seq = latest_seq_;
        return true;
    }
    if (!frame_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return (latest_seq_ != in_out_seq && latest_seq_ > 0) || !running_.load(); })) {
        return false;
    }
    if (!running_.load() || latest_seq_ == in_out_seq || latest_seq_ == 0 || latest_jpeg_.empty()) return false;

    out_jpeg = latest_jpeg_;
    in_out_seq = latest_seq_;
    return true;
}

void DisplayPipeline::run() {
    uint64_t next_frame_time = now_ns();
    std::vector<double> fps_history;
    uint64_t fps_window_start = now_ns();
    int frames_in_window = 0;

    const int target_w = 960;
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
        if (now < next_frame_time) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(next_frame_time - now));
        }
        next_frame_time = std::max(now_ns(), next_frame_time + frame_interval_ns);


        NvBufSurface* surface = nullptr;
        uint64_t seq = 0, sensor_ts = 0;
        if (!camera_.acquire_display_frame(&surface, &seq, &sensor_ts, 150)) {
            continue;
        }
        try {
        uint64_t t0 = now_ns();
        const auto& p = surface->surfaceList[0];
        const uint32_t src_w = p.width;
        const uint32_t src_h = p.height;
        const int target_h = (int)std::lround(960.0 * src_h / src_w) & ~1;

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

        if (s.zoom_mode == "1x" || s.zoom_mode == "2x" || s.zoom_mode == "4x") {
            int crop_w = ((s.zoom_mode == "1x") ? target_w : src_w / (s.zoom_mode == "4x" ? 4 : 2)) & ~1;
            int crop_h = ((s.zoom_mode == "1x") ? target_h : src_h / (s.zoom_mode == "4x" ? 4 : 2)) & ~1;
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
            cv::cvtColor(crop_yuv, crop_bgr, cv::COLOR_YUV2BGR_NV21);
            if (s.zoom_mode == "2x" || s.zoom_mode == "4x") {
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
            cv::cvtColor(yuv, full_bgr, cv::COLOR_YUV2BGR_NV21);
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
                    cv::cvtColor(full_yuv, snap_bgr, cv::COLOR_YUV2BGR_NV21);
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
                               << "  \"width\": " << src_w << ",\n"
                               << "  \"height\": " << src_h << "\n"
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
        // 6. Overlays: Grid / Splits / Crosshair / Safe Area
        if (s.grid_mode == "crosshair") {
            int mid_x = bgr.cols / 2;
            int mid_y = bgr.rows / 2;
            cv::line(bgr, cv::Point(mid_x - 20, mid_y), cv::Point(mid_x + 20, mid_y), cv::Scalar(0, 255, 255), 1);
            cv::line(bgr, cv::Point(mid_x, mid_y - 20), cv::Point(mid_x, mid_y + 20), cv::Scalar(0, 255, 255), 1);
            cv::circle(bgr, cv::Point(mid_x, mid_y), 3, cv::Scalar(0, 255, 255), 1);
        } else if (s.grid_mode == "rule_of_thirds") {
            int x1 = bgr.cols / 3, x2 = 2 * bgr.cols / 3;
            int y1 = bgr.rows / 3, y2 = 2 * bgr.rows / 3;
            cv::line(bgr, cv::Point(x1, 0), cv::Point(x1, bgr.rows), cv::Scalar(180, 180, 180), 1);
            cv::line(bgr, cv::Point(x2, 0), cv::Point(x2, bgr.rows), cv::Scalar(180, 180, 180), 1);
            cv::line(bgr, cv::Point(0, y1), cv::Point(bgr.cols, y1), cv::Scalar(180, 180, 180), 1);
            cv::line(bgr, cv::Point(0, y2), cv::Point(bgr.cols, y2), cv::Scalar(180, 180, 180), 1);
        } else if (s.grid_mode == "quad") {
            int mid_x = bgr.cols / 2;
            int mid_y = bgr.rows / 2;
            cv::line(bgr, cv::Point(mid_x, 0), cv::Point(mid_x, bgr.rows), cv::Scalar(180, 180, 180), 1);
            cv::line(bgr, cv::Point(0, mid_y), cv::Point(bgr.cols, mid_y), cv::Scalar(180, 180, 180), 1);
        } else if (s.grid_mode == "center_split_v") {
            int mid_x = bgr.cols / 2;
            cv::line(bgr, cv::Point(mid_x, 0), cv::Point(mid_x, bgr.rows), cv::Scalar(0, 220, 255), 1);
        } else if (s.grid_mode == "center_split_h") {
            int mid_y = bgr.rows / 2;
            cv::line(bgr, cv::Point(0, mid_y), cv::Point(bgr.cols, mid_y), cv::Scalar(0, 220, 255), 1);
        } else if (s.grid_mode == "safe_area") {
            int mx = bgr.cols / 10, my = bgr.rows / 10;
            cv::rectangle(bgr, cv::Rect(mx, my, bgr.cols - 2 * mx, bgr.rows - 2 * my), cv::Scalar(160, 160, 160), 1);
            cv::line(bgr, cv::Point(bgr.cols / 2 - 12, bgr.rows / 2), cv::Point(bgr.cols / 2 + 12, bgr.rows / 2), cv::Scalar(160, 160, 160), 1);
            cv::line(bgr, cv::Point(bgr.cols / 2, bgr.rows / 2 - 12), cv::Point(bgr.cols / 2, bgr.rows / 2 + 12), cv::Scalar(160, 160, 160), 1);
        }

        // Video recording frame write
        if (is_recording_.load()) {
            std::lock_guard<std::mutex> rlock(record_mu_);
            if (record_writer_.isOpened()) {
                record_writer_.write(bgr);
                recorded_frames_.fetch_add(1);
            }
        }

        // Calibration capture check
        {
            std::lock_guard<std::mutex> clock(calib_mu_);
            if (calib_request_capture_) {
                cv::Mat calib_gray;
                if (gray.empty()) cv::cvtColor(bgr, calib_gray, cv::COLOR_BGR2GRAY);
                else calib_gray = gray;
                cv::Size board_size(9, 6);
                std::vector<cv::Point2f> corners;
                bool found = cv::findChessboardCorners(calib_gray, board_size, corners,
                                                       cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK | cv::CALIB_CB_NORMALIZE_IMAGE);
                if (found) {
                    cv::cornerSubPix(calib_gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                                     cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
                    calib_corners_all_.push_back(corners);
                    calib_image_size_ = calib_gray.size();
                }
                calib_corners_found_last_ = found;
                calib_capture_done_ = true;
                calib_request_capture_ = false;
                calib_cv_.notify_all();
            }
        }
        // Pitch Motion Detection & Auto-Clipping
        {
            std::lock_guard<std::mutex> plock(pitch_mu_);
            current_fps_target_ = fps_target;
            size_t max_pre_roll = (size_t)std::max(10, (int)(pitch_config_.pre_roll_sec * fps_target));
            pre_roll_buffer_.push_back(bgr.clone());
            while (pre_roll_buffer_.size() > max_pre_roll) {
                pre_roll_buffer_.pop_front();
            }

            // Auto Motion Detection
            if (pitch_config_.enabled) {
                cv::Mat det_gray;
                if (gray.empty()) cv::cvtColor(bgr, det_gray, cv::COLOR_BGR2GRAY);
                else det_gray = gray;

                if (!prev_detect_gray_.empty() && prev_detect_gray_.size() == det_gray.size()) {
                    cv::Mat diff, thresh_mask;
                    cv::absdiff(det_gray, prev_detect_gray_, diff);
                    cv::threshold(diff, thresh_mask, pitch_config_.motion_threshold, 255, cv::THRESH_BINARY);
                    int motion_pixels = cv::countNonZero(thresh_mask);
                    int motion_pct = std::min(100, (motion_pixels * 100) / std::max(1, pitch_config_.min_motion_pixels * 2));
                    pitch_status_.current_motion_level = motion_pct;

                    uint64_t now = now_ns();
                    uint64_t cooldown_ns = (uint64_t)(pitch_config_.cooldown_sec * 1000000000ULL);

                    if (!pitch_status_.is_clipping && motion_pixels >= pitch_config_.min_motion_pixels &&
                        (now - last_pitch_trigger_time_ns_ >= cooldown_ns)) {
                        last_pitch_trigger_time_ns_ = now;
                        pitch_status_.is_clipping = true;
                        post_roll_remaining_ = (int)std::lround(pitch_config_.post_roll_sec * fps_target);

                        std::error_code ec;
                        std::filesystem::create_directories("recordings", ec);
                        auto t = std::time(nullptr);
                        struct tm tm;
                        localtime_r(&t, &tm);
                        std::ostringstream ss;
                        ss << "recordings/pitch_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".avi";
                        std::string clip_path = ss.str();
                        pitch_status_.last_clip_path = clip_path;

                        {
                            std::lock_guard<std::mutex> qlock(clip_queue_mu_);
                            while (!clip_queue_.empty()) clip_queue_.pop();
                            active_clip_path_ = clip_path;
                            active_clip_fps_ = (double)fps_target;
                            active_clip_size_ = cv::Size(target_w, target_h);
                            for (const auto& f : pre_roll_buffer_) {
                                clip_queue_.push(f.clone());
                            }
                            clip_queue_cv_.notify_one();
                        }
                    }
                }
                prev_detect_gray_ = det_gray.clone();
            } else {
                pitch_status_.current_motion_level = 0;
            }

            // Feed post-roll frames to worker while clipping
            if (pitch_status_.is_clipping) {
                if (post_roll_remaining_ > 0) {
                    {
                        std::lock_guard<std::mutex> qlock(clip_queue_mu_);
                        clip_queue_.push(bgr.clone());
                        clip_queue_cv_.notify_one();
                    }
                    post_roll_remaining_--;
                } else if (post_roll_remaining_ == 0) {
                    {
                        std::lock_guard<std::mutex> qlock(clip_queue_mu_);
                        clip_queue_.push(cv::Mat()); // EOF sentinel
                        clip_queue_cv_.notify_one();
                    }
                    post_roll_remaining_ = -1;
                }
            }
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
    std::unique_lock<std::mutex> exec_lock(snapshot_exec_mu_);

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

bool DisplayPipeline::start_recording(const std::string& directory) {
    if (!running_.load()) return false;
    std::lock_guard<std::mutex> lock(record_mu_);
    if (is_recording_.load()) return false;

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    auto t = std::time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    std::ostringstream ss;
    ss << directory << "/rec_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".avi";
    record_path_ = ss.str();

    auto caps = camera_.get_capabilities();
    int target_w = 960;
    int target_h = (int)std::lround(960.0 * caps.height / caps.width) & ~1;

    bool ok = record_writer_.open(record_path_, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                                  30.0, cv::Size(target_w, target_h));
    if (!ok) {
        record_path_.clear();
        return false;
    }
    recorded_frames_.store(0);
    record_start_time_ns_ = now_ns();
    is_recording_.store(true);
    return true;
}

bool DisplayPipeline::stop_recording() {
    std::lock_guard<std::mutex> lock(record_mu_);
    if (!is_recording_.load()) return false;
    is_recording_.store(false);
    if (record_writer_.isOpened()) {
        record_writer_.release();
    }
    return true;
}

bool DisplayPipeline::is_recording() const {
    return is_recording_.load();
}

std::string DisplayPipeline::get_recording_path() const {
    std::lock_guard<std::mutex> lock(record_mu_);
    return record_path_;
}

uint64_t DisplayPipeline::get_recorded_frames() const {
    return recorded_frames_.load();
}

bool DisplayPipeline::capture_calibration_frame() {
    if (!running_.load()) return false;
    std::unique_lock<std::mutex> lock(calib_mu_);
    calib_request_capture_ = true;
    calib_capture_done_ = false;
    if (!calib_cv_.wait_for(lock, std::chrono::milliseconds(2000), [&] { return calib_capture_done_ || !running_.load(); })) {
        calib_request_capture_ = false;
        return false;
    }
    return calib_corners_found_last_;
}

void DisplayPipeline::clear_calibration_frames() {
    std::lock_guard<std::mutex> lock(calib_mu_);
    calib_corners_all_.clear();
}

int DisplayPipeline::get_calibration_frame_count() const {
    std::lock_guard<std::mutex> lock(calib_mu_);
    return (int)calib_corners_all_.size();
}

nlohmann::json DisplayPipeline::compute_calibration() {
    std::lock_guard<std::mutex> lock(calib_mu_);
    if (calib_corners_all_.size() < 3) {
        return {{"status", "error"}, {"error", "Need at least 3 calibration frames with detected chessboard"}};
    }

    cv::Size board_size(9, 6);
    float square_size_mm = 25.0f;

    std::vector<std::vector<cv::Point3f>> object_points(calib_corners_all_.size());
    for (size_t i = 0; i < calib_corners_all_.size(); ++i) {
        for (int r = 0; r < board_size.height; ++r) {
            for (int c = 0; c < board_size.width; ++c) {
                object_points[i].emplace_back(c * square_size_mm, r * square_size_mm, 0.0f);
            }
        }
    }

    cv::Mat camera_matrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat dist_coeffs = cv::Mat::zeros(8, 1, CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;

    double rms = 0.0;
    try {
        rms = cv::calibrateCamera(object_points, calib_corners_all_, calib_image_size_,
                                  camera_matrix, dist_coeffs, rvecs, tvecs);
    } catch (const cv::Exception& e) {
        return {{"status", "error"}, {"error", std::string("Calibration computation failed: ") + e.what()}};
    }
    current_calibration_ = {
        {"status", "ok"},
        {"rms_reprojection_error", rms},
        {"frame_count", calib_corners_all_.size()},
        {"image_size", {{"width", calib_image_size_.width}, {"height", calib_image_size_.height}}},
        {"camera_matrix", {
            {"fx", camera_matrix.at<double>(0, 0)},
            {"fy", camera_matrix.at<double>(1, 1)},
            {"cx", camera_matrix.at<double>(0, 2)},
            {"cy", camera_matrix.at<double>(1, 2)}
        }},
        {"distortion", {
            {"k1", dist_coeffs.at<double>(0)},
            {"k2", dist_coeffs.at<double>(1)},
            {"p1", dist_coeffs.at<double>(2)},
            {"p2", dist_coeffs.at<double>(3)},
            {"k3", dist_coeffs.at<double>(4)}
        }}
    };
    return current_calibration_;
}

void DisplayPipeline::update_pitch_config(const PitchDetectConfig& cfg) {
    std::lock_guard<std::mutex> lock(pitch_mu_);
    pitch_config_ = cfg;
    pitch_status_.enabled = cfg.enabled;
    if (!cfg.enabled) {
        pitch_status_.is_clipping = false;
        pre_roll_buffer_.clear();
        prev_detect_gray_.release();
    }
}

PitchDetectConfig DisplayPipeline::get_pitch_config() const {
    std::lock_guard<std::mutex> lock(pitch_mu_);
    return pitch_config_;
}

PitchDetectStatus DisplayPipeline::get_pitch_status() const {
    std::lock_guard<std::mutex> lock(pitch_mu_);
    return pitch_status_;
}

nlohmann::json DisplayPipeline::get_calibration() const {
    std::lock_guard<std::mutex> lock(calib_mu_);
    if (current_calibration_.empty()) {
        return {{"status", "uncalibrated"}, {"frame_count", calib_corners_all_.size()}};
    }
    return current_calibration_;
}

void DisplayPipeline::clip_worker_loop() {
    while (clip_worker_running_.load()) {
        std::string clip_path;
        double fps = 30.0;
        cv::Size size(960, 714);
        cv::VideoWriter writer;

        {
            std::unique_lock<std::mutex> lock(clip_queue_mu_);
            clip_queue_cv_.wait(lock, [&] {
                return !clip_queue_.empty() || !clip_worker_running_.load();
            });
            if (!clip_worker_running_.load()) break;

            clip_path = active_clip_path_;
            fps = active_clip_fps_;
            size = active_clip_size_;
        }

        std::error_code ec;
        std::filesystem::create_directories("recordings", ec);
        if (!clip_path.empty()) {
            writer.open(clip_path, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, size);
        }

        bool active = true;
        while (active && clip_worker_running_.load()) {
            cv::Mat frame;
            {
                std::unique_lock<std::mutex> lock(clip_queue_mu_);
                clip_queue_cv_.wait(lock, [&] {
                    return !clip_queue_.empty() || !clip_worker_running_.load();
                });
                if (!clip_worker_running_.load()) break;
                frame = std::move(clip_queue_.front());
                clip_queue_.pop();
            }

            if (frame.empty()) {
                active = false;
                break;
            }

            if (writer.isOpened()) {
                writer.write(frame);
            }
        }

        if (writer.isOpened()) {
            writer.release();
            std::lock_guard<std::mutex> plock(pitch_mu_);
            pitch_status_.total_clips++;
            pitch_status_.is_clipping = false;
        }
    }
}

bool DisplayPipeline::trigger_pitch_clip(const std::string& custom_label) {
    std::lock_guard<std::mutex> lock(pitch_mu_);
    if (!running_.load()) return false;
    if (pitch_status_.is_clipping) return false;

    int fps = current_fps_target_ > 0 ? current_fps_target_ : 30;
    pitch_status_.is_clipping = true;
    post_roll_remaining_ = (int)std::lround(pitch_config_.post_roll_sec * fps);

    std::error_code ec;
    std::filesystem::create_directories("recordings", ec);
    auto t = std::time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    std::ostringstream ss;
    ss << "recordings/pitch_" << (custom_label.empty() ? "" : custom_label + "_")
       << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".avi";
    std::string clip_path = ss.str();
    pitch_status_.last_clip_path = clip_path;

    auto caps = camera_.get_capabilities();
    int target_w = 960;
    int target_h = (int)std::lround(960.0 * caps.height / caps.width) & ~1;

    {
        std::lock_guard<std::mutex> qlock(clip_queue_mu_);
        while (!clip_queue_.empty()) clip_queue_.pop();
        active_clip_path_ = clip_path;
        active_clip_fps_ = (double)fps;
        active_clip_size_ = cv::Size(target_w, target_h);
        for (const auto& f : pre_roll_buffer_) {
            clip_queue_.push(f.clone());
        }
        clip_queue_cv_.notify_one();
    }
    return true;
}
