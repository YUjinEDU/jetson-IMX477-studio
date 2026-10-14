// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/sensor_recorder.h"

#include <opencv2/opencv.hpp>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <queue>
#include <thread>
#include <condition_variable>
#include <jpeglib.h>

static bool encode_jpeg_bgr_rec(const cv::Mat& bgr, std::vector<uint8_t>& out_jpeg, int quality) {
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

class CleanMjpegAviWriter {
public:
    CleanMjpegAviWriter() = default;
    ~CleanMjpegAviWriter() { close(); }

    bool open(const std::string& path, uint32_t width, uint32_t height, double fps) {
        close();
        file_.open(path, std::ios::binary);
        if (!file_.is_open()) return false;

        width_ = width;
        height_ = height;
        fps_ = fps;
        frame_count_ = 0;
        offsets_.clear();
        sizes_.clear();

        // Standard AVI header size is 224 bytes
        std::vector<uint8_t> pad(224, 0);
        file_.write(reinterpret_cast<const char*>(pad.data()), pad.size());
        movi_data_start_ = file_.tellp();
        return true;
    }

    bool write_frame(const uint8_t* jpeg_data, size_t jpeg_size) {
        if (!file_.is_open()) return false;
        uint32_t offset = (uint32_t)file_.tellp() - (uint32_t)movi_data_start_ + 4; // offset from 'movi'
        offsets_.push_back(offset);
        sizes_.push_back((uint32_t)jpeg_size);

        file_.write("00dc", 4);
        uint32_t sz = (uint32_t)jpeg_size;
        file_.write(reinterpret_cast<const char*>(&sz), 4);
        file_.write(reinterpret_cast<const char*>(jpeg_data), jpeg_size);
        if (jpeg_size % 2 != 0) {
            char pad = 0;
            file_.write(&pad, 1);
        }
        frame_count_++;
        return true;
    }

    void close() {
        if (!file_.is_open()) return;

        uint32_t movi_data_end = (uint32_t)file_.tellp();
        uint32_t movi_len = movi_data_end - (uint32_t)movi_data_start_ + 4;

        // Write index 'idx1'
        file_.write("idx1", 4);
        uint32_t idx_size = (uint32_t)offsets_.size() * 16;
        file_.write(reinterpret_cast<const char*>(&idx_size), 4);
        for (size_t i = 0; i < offsets_.size(); ++i) {
            file_.write("00dc", 4);
            uint32_t flags = 0x10; // AVIIF_KEYFRAME
            file_.write(reinterpret_cast<const char*>(&flags), 4);
            uint32_t off = offsets_[i];
            file_.write(reinterpret_cast<const char*>(&off), 4);
            uint32_t len = sizes_[i];
            file_.write(reinterpret_cast<const char*>(&len), 4);
        }
        uint32_t file_size = (uint32_t)file_.tellp();

        // Write actual header at beginning
        file_.seekp(0);
        file_.write("RIFF", 4);
        uint32_t riff_size = file_size - 8;
        file_.write(reinterpret_cast<const char*>(&riff_size), 4);
        file_.write("AVI ", 4);

        // LIST hdrl (192 bytes)
        file_.write("LIST", 4);
        uint32_t hdrl_size = 192;
        file_.write(reinterpret_cast<const char*>(&hdrl_size), 4);
        file_.write("hdrl", 4);

        // avih (56 bytes)
        file_.write("avih", 4);
        uint32_t avih_size = 56;
        file_.write(reinterpret_cast<const char*>(&avih_size), 4);
        uint32_t us_per_frame = (uint32_t)std::lround(1000000.0 / fps_);
        file_.write(reinterpret_cast<const char*>(&us_per_frame), 4);
        uint32_t max_bytes = 100000000;
        file_.write(reinterpret_cast<const char*>(&max_bytes), 4);
        uint32_t zero = 0;
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        uint32_t flags = 0x810; // AVIF_HASINDEX | AVIF_TRUSTCKTYPE
        file_.write(reinterpret_cast<const char*>(&flags), 4);
        file_.write(reinterpret_cast<const char*>(&frame_count_), 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        uint32_t streams = 1;
        file_.write(reinterpret_cast<const char*>(&streams), 4);
        uint32_t buf_size = 1048576;
        file_.write(reinterpret_cast<const char*>(&buf_size), 4);
        file_.write(reinterpret_cast<const char*>(&width_), 4);
        file_.write(reinterpret_cast<const char*>(&height_), 4);
        uint32_t reserved[4] = {0, 0, 0, 0};
        file_.write(reinterpret_cast<const char*>(reserved), 16);

        // LIST strl (116 bytes)
        file_.write("LIST", 4);
        uint32_t strl_size = 116;
        file_.write(reinterpret_cast<const char*>(&strl_size), 4);
        file_.write("strl", 4);

        // strh (56 bytes)
        file_.write("strh", 4);
        uint32_t strh_size = 56;
        file_.write(reinterpret_cast<const char*>(&strh_size), 4);
        file_.write("vids", 4);
        file_.write("MJPG", 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        uint16_t prio = 0, lang = 0;
        file_.write(reinterpret_cast<const char*>(&prio), 2);
        file_.write(reinterpret_cast<const char*>(&lang), 2);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        uint32_t scale = 1000;
        uint32_t rate = (uint32_t)std::lround(fps_ * 1000.0);
        file_.write(reinterpret_cast<const char*>(&scale), 4);
        file_.write(reinterpret_cast<const char*>(&rate), 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        file_.write(reinterpret_cast<const char*>(&frame_count_), 4);
        file_.write(reinterpret_cast<const char*>(&buf_size), 4);
        uint32_t quality = 10000;
        file_.write(reinterpret_cast<const char*>(&quality), 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        int16_t rcFrame[4] = {0, 0, (int16_t)width_, (int16_t)height_};
        file_.write(reinterpret_cast<const char*>(rcFrame), 8);

        // strf (40 bytes)
        file_.write("strf", 4);
        uint32_t strf_size = 40;
        file_.write(reinterpret_cast<const char*>(&strf_size), 4);
        uint32_t biSize = 40;
        file_.write(reinterpret_cast<const char*>(&biSize), 4);
        int32_t biWidth = width_;
        int32_t biHeight = height_;
        file_.write(reinterpret_cast<const char*>(&biWidth), 4);
        file_.write(reinterpret_cast<const char*>(&biHeight), 4);
        uint16_t biPlanes = 1;
        uint16_t biBitCount = 24;
        file_.write(reinterpret_cast<const char*>(&biPlanes), 2);
        file_.write(reinterpret_cast<const char*>(&biBitCount), 2);
        file_.write("MJPG", 4);
        uint32_t biSizeImage = width_ * height_ * 3;
        file_.write(reinterpret_cast<const char*>(&biSizeImage), 4);
        uint32_t biPels = 0;
        file_.write(reinterpret_cast<const char*>(&biPels), 4);
        file_.write(reinterpret_cast<const char*>(&biPels), 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);
        file_.write(reinterpret_cast<const char*>(&zero), 4);

        // LIST movi
        file_.write("LIST", 4);
        file_.write(reinterpret_cast<const char*>(&movi_len), 4);
        file_.write("movi", 4);

        file_.close();
    }

private:
    std::ofstream file_;
    uint32_t width_{0};
    uint32_t height_{0};
    double fps_{30.0};
    uint32_t frame_count_{0};
    std::streampos movi_data_start_{0};
    std::vector<uint32_t> offsets_;
    std::vector<uint32_t> sizes_;
};

struct RecTask {
    std::vector<uint8_t> nv12;
    FrameMetadata meta;
    bool is_eof{false};
};

struct SensorRecorder::Impl {
    mutable std::mutex mu;
    std::mutex queue_mu;
    std::condition_variable queue_cv;
    std::queue<RecTask> task_queue;
    std::thread worker_thread;
    std::atomic<bool> worker_running{false};

    CleanMjpegAviWriter avi_writer;
    std::ofstream csv_file;
    OriginalRecordingStatus status;
    std::string csv_path;
    uint64_t frame_index{0};
    uint64_t first_ts_ns{0};
    uint32_t width{0};
    uint32_t height{0};
    double target_fps{30.0};

    Impl() = default;

    ~Impl() {
        stop_internal();
    }

    void worker_loop() {
        cv::Mat y_mat(height, width, CV_8UC1);
        cv::Mat uv_mat(height / 2, width / 2, CV_8UC2);
        cv::Mat bgr;
        std::vector<uint8_t> jpg_buf;
        std::vector<int> encode_params = {cv::IMWRITE_JPEG_QUALITY, 85};

        while (true) {
            RecTask task;
            {
                std::unique_lock<std::mutex> lock(queue_mu);
                queue_cv.wait(lock, [&] {
                    return !task_queue.empty() || !worker_running.load();
                });
                if (task_queue.empty()) {
                    if (!worker_running.load()) break;
                    continue;
                }
                task = std::move(task_queue.front());
                task_queue.pop();
            }

            if (task.is_eof) {
                break;
            }

            // Decode NV12 to BGR and encode to JPEG
            size_t y_sz = (size_t)width * height;
            memcpy(y_mat.data, task.nv12.data(), y_sz);
            memcpy(uv_mat.data, task.nv12.data() + y_sz, y_sz / 2);
            cv::cvtColorTwoPlane(y_mat, uv_mat, bgr, cv::COLOR_YUV2BGR_NV12);

            jpg_buf.clear();
            if (encode_jpeg_bgr_rec(bgr, jpg_buf, 85)) {
                avi_writer.write_frame(jpg_buf.data(), jpg_buf.size());
            }

            if (csv_file.is_open()) {
                csv_file << frame_index << ","
                         << task.meta.sequence << ","
                         << task.meta.sensor_timestamp_ns << ","
                         << task.meta.exposure_us << ","
                         << task.meta.analog_gain << ","
                         << task.meta.isp_digital_gain << ","
                         << task.meta.awb_cct << "\n";
            }

            if (frame_index == 0) {
                first_ts_ns = task.meta.sensor_timestamp_ns;
            } else if (task.meta.sensor_timestamp_ns > first_ts_ns) {
                std::lock_guard<std::mutex> lock(mu);
                status.actual_fps = (double)frame_index * 1e9 / (double)(task.meta.sensor_timestamp_ns - first_ts_ns);
            }

            frame_index++;
            {
                std::lock_guard<std::mutex> lock(mu);
                status.frames = frame_index;
                status.buffered_sec = (double)task_queue.size() / std::max(1.0, target_fps);
            }
        }

        avi_writer.close();
        if (csv_file.is_open()) {
            csv_file.flush();
            csv_file.close();
        }

        std::lock_guard<std::mutex> lock(mu);
        status.recording = false;
        status.ready = false;
        status.buffered_sec = 0.0;
    }

    bool start(const std::string& filepath, uint32_t w, uint32_t h, double fps) {
        std::lock_guard<std::mutex> lock(mu);
        if (status.recording) {
            return false;
        }

        std::filesystem::path p(filepath);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        csv_path = p.parent_path().string();
        if (!csv_path.empty()) csv_path += "/";
        csv_path += p.stem().string() + ".csv";

        csv_file.open(csv_path, std::ios::out | std::ios::trunc);
        if (csv_file.is_open()) {
            csv_file << "frame_index,camera_sequence,sensor_timestamp_ns,exposure_us,analog_gain,isp_digital_gain,awb_cct\n";
        }

        width = w;
        height = h;
        target_fps = fps > 0 ? fps : 30.0;

        if (!avi_writer.open(filepath, width, height, target_fps)) {
            status.error = "Failed to open AVI file for writing";
            if (csv_file.is_open()) csv_file.close();
            return false;
        }

        {
            std::lock_guard<std::mutex> qlock(queue_mu);
            while (!task_queue.empty()) task_queue.pop();
            worker_running.store(true);
        }

        worker_thread = std::thread(&Impl::worker_loop, this);

        status.recording = true;
        status.ready = true;
        status.path = filepath;
        status.metadata_url = "/" + csv_path;
        status.error.clear();
        status.frames = 0;
        status.dropped_frames = 0;
        status.width = width;
        status.height = height;
        status.fps = target_fps;
        status.actual_fps = 0.0;
        status.buffered_sec = 0.0;
        frame_index = 0;
        first_ts_ns = 0;
        return true;
    }

    bool push_frame(const uint8_t* nv12_data, size_t size, const FrameMetadata& meta) {
        if (!status.recording) return false;

        RecTask task;
        task.nv12.assign(nv12_data, nv12_data + size);
        task.meta = meta;
        task.is_eof = false;

        {
            std::lock_guard<std::mutex> lock(queue_mu);
            // Bounded queue: 256 frames (over 2 seconds at 120fps)
            if (task_queue.size() >= 256) {
                std::lock_guard<std::mutex> slock(mu);
                status.dropped_frames++;
                return false;
            }
            task_queue.push(std::move(task));
            queue_cv.notify_one();
        }
        return true;
    }

    bool stop_internal() {
        if (!status.recording) return true;

        {
            std::lock_guard<std::mutex> lock(queue_mu);
            RecTask eof_task;
            eof_task.is_eof = true;
            task_queue.push(eof_task);
            worker_running.store(false);
            queue_cv.notify_one();
        }

        if (worker_thread.joinable()) {
            worker_thread.join();
        }

        return true;
    }

    bool stop() {
        return stop_internal();
    }
};

SensorRecorder::SensorRecorder() : impl_(std::make_unique<Impl>()) {}
SensorRecorder::~SensorRecorder() = default;

bool SensorRecorder::start(const std::string& filepath, uint32_t width, uint32_t height, double fps) {
    return impl_->start(filepath, width, height, fps);
}

bool SensorRecorder::push_frame(const uint8_t* nv12_data, size_t size, const FrameMetadata& meta) {
    return impl_->push_frame(nv12_data, size, meta);
}

bool SensorRecorder::stop() {
    return impl_->stop();
}

bool SensorRecorder::is_recording() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->status.recording;
}

OriginalRecordingStatus SensorRecorder::get_status() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->status;
}
