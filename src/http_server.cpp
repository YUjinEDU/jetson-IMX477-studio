// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/http_server.h"
#include "studio/camera_service.h"
#include "studio/display_pipeline.h"
#include "studio/preset_manager.h"

#define CPPHTTPLIB_THREAD_POOL_COUNT 16
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <fstream>
#include <cmath>
#include <cctype>
#include <limits>
#include <filesystem>
#include <unordered_map>

enum class ResolveStatus {
    Ok,
    InvalidInput, // 400
    NotFound      // 404
};

struct ResolvedRecording {
    std::string filename;
    std::filesystem::path canonical_path;
    uint64_t file_size = 0;
    std::filesystem::file_time_type mtime{};
};

static ResolveStatus resolve_recording_path(const std::string& raw_path, ResolvedRecording& out) {
    if (raw_path.empty()) {
        return ResolveStatus::InvalidInput;
    }
    for (char c : raw_path) {
        if (c == '\0' || c == '\\') {
            return ResolveStatus::InvalidInput;
        }
    }
    if (raw_path.find("..") != std::string::npos) {
        return ResolveStatus::InvalidInput;
    }

    std::string filename;
    if (raw_path.rfind("/recordings/", 0) == 0) {
        filename = raw_path.substr(12);
    } else if (raw_path.rfind("recordings/", 0) == 0) {
        filename = raw_path.substr(11);
    } else if (raw_path[0] == '/') {
        return ResolveStatus::InvalidInput;
    } else {
        filename = raw_path;
    }

    if (filename.empty()) {
        return ResolveStatus::InvalidInput;
    }
    if (filename.find('/') != std::string::npos) {
        return ResolveStatus::InvalidInput;
    }

    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return ResolveStatus::InvalidInput;
    }
    std::string ext = filename.substr(dot_pos);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext != ".avi" && ext != ".mp4") {
        return ResolveStatus::InvalidInput;
    }

    for (char c : filename) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127) {
            return ResolveStatus::InvalidInput;
        }
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path recordings_dir = fs::canonical("recordings", ec);
    if (ec || !fs::is_directory(recordings_dir, ec)) {
        return ResolveStatus::NotFound;
    }

    fs::path candidate = recordings_dir / filename;
    if (!fs::exists(candidate, ec) || ec) {
        return ResolveStatus::NotFound;
    }

    fs::path canon_file = fs::canonical(candidate, ec);
    if (ec) {
        return ResolveStatus::NotFound;
    }

    if (!fs::is_regular_file(canon_file, ec) || ec) {
        return ResolveStatus::NotFound;
    }

    if (canon_file.parent_path() != recordings_dir) {
        return ResolveStatus::InvalidInput;
    }

    out.filename = filename;
    out.canonical_path = canon_file;
    out.file_size = fs::file_size(canon_file, ec);
    if (ec) return ResolveStatus::NotFound;
    out.mtime = fs::last_write_time(canon_file, ec);
    if (ec) return ResolveStatus::NotFound;

    return ResolveStatus::Ok;
}

static bool parse_nonnegative_int(const std::string& str, int& out) {
    if (str.empty()) return false;
    for (char c : str) {
        if (c < '0' || c > '9') return false;
    }
    uint64_t val = 0;
    for (char c : str) {
        val = val * 10 + static_cast<uint64_t>(c - '0');
        if (val > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return false;
        }
    }
    out = static_cast<int>(val);
    return true;
}

struct CaptureSession {
    std::mutex mu;
    std::string canon_path;
    uint64_t file_size = 0;
    std::filesystem::file_time_type mtime{};
    cv::VideoCapture cap;
    int current_frame_pos = -1;
    double fps = 0.0;
    int total_frames = 0;
    int width = 0;
    int height = 0;
    std::chrono::steady_clock::time_point last_used{};

    bool read_frame(int target_frame, cv::Mat& out_frame) {
        if (!cap.isOpened()) return false;
        if (target_frame < 0) return false;
        if (total_frames > 0 && target_frame >= total_frames) return false;

        if (current_frame_pos != target_frame) {
            if (!cap.set(cv::CAP_PROP_POS_FRAMES, target_frame)) return false;
            current_frame_pos = target_frame;
        }

        if (!cap.read(out_frame) || out_frame.empty()) {
            current_frame_pos = -1;
            return false;
        }

        current_frame_pos = target_frame + 1;
        return true;
    }
};

class ClipCache {
public:
    static constexpr size_t MAX_SESSIONS = 8;

    std::shared_ptr<CaptureSession> get_or_open(
        const std::string& canon_path,
        uint64_t file_size,
        std::filesystem::file_time_type mtime)
    {
        std::unique_lock<std::mutex> lock(mu_);
        auto now = std::chrono::steady_clock::now();

        auto it = sessions_.find(canon_path);
        if (it != sessions_.end()) {
            auto session = it->second;
            if (session->file_size == file_size && session->mtime == mtime && session->cap.isOpened()) {
                session->last_used = now;
                return session;
            }
            sessions_.erase(it);
        }

        if (sessions_.size() >= MAX_SESSIONS) {
            auto oldest_it = sessions_.begin();
            for (auto sit = sessions_.begin(); sit != sessions_.end(); ++sit) {
                if (sit->second->last_used < oldest_it->second->last_used) {
                    oldest_it = sit;
                }
            }
            if (oldest_it != sessions_.end()) {
                sessions_.erase(oldest_it);
            }
        }

        auto new_session = std::make_shared<CaptureSession>();
        new_session->canon_path = canon_path;
        new_session->file_size = file_size;
        new_session->mtime = mtime;
        new_session->last_used = now;

        if (!new_session->cap.open(canon_path)) {
            return nullptr;
        }

        double raw_fps = new_session->cap.get(cv::CAP_PROP_FPS);
        if (std::isnan(raw_fps) || std::isinf(raw_fps) || raw_fps < 0.0) {
            new_session->fps = 0.0;
        } else {
            new_session->fps = raw_fps;
        }
        new_session->total_frames = static_cast<int>(new_session->cap.get(cv::CAP_PROP_FRAME_COUNT));
        new_session->width = static_cast<int>(new_session->cap.get(cv::CAP_PROP_FRAME_WIDTH));
        new_session->height = static_cast<int>(new_session->cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        new_session->current_frame_pos = 0;

        sessions_[canon_path] = new_session;
        return new_session;
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<CaptureSession>> sessions_;
};

static float get_cpu_temp_c() {
    std::ifstream f("/sys/devices/virtual/thermal/thermal_zone0/temp");
    if (f) {
        long t = 0;
        if (f >> t) return (float)t / 1000.0f;
    }
    return 0.0f;
}

static uint64_t get_ram_used_mb() {
    std::ifstream f("/proc/meminfo");
    if (f) {
        std::string line;
        uint64_t total = 0, avail = 0;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) sscanf(line.c_str(), "MemTotal: %lu", &total);
            else if (line.rfind("MemAvailable:", 0) == 0) sscanf(line.c_str(), "MemAvailable: %lu", &avail);
        }
        if (total > avail) return (total - avail) / 1024;
    }
    return 0;
}

using json = nlohmann::json;

static json controls_to_json(const CameraControls& ctrl) {
    return {
        {"auto_exposure", ctrl.auto_exposure},
        {"exposure_us", ctrl.exposure_us},
        {"gain", ctrl.gain},
        {"ae_max_exposure_us", ctrl.ae_max_exposure_us},
        {"ae_max_gain", ctrl.ae_max_gain},
        {"ae_lock", ctrl.ae_lock},
        {"ev_compensation", ctrl.ev_compensation},
        {"antibanding", ctrl.antibanding},
        {"awb_mode", ctrl.awb_mode},
        {"awb_lock", ctrl.awb_lock},
        {"isp_digital_gain", ctrl.isp_digital_gain},
        {"denoise_mode", ctrl.denoise_mode},
        {"edge_mode", ctrl.edge_mode}
    };
}

static json display_settings_to_json(const DisplaySettings& ds) {
    return {
        {"zoom_mode", ds.zoom_mode},
        {"center_x", ds.center_x},
        {"center_y", ds.center_y},
        {"focus_peaking", ds.focus_peaking},
        {"peaking_color", ds.peaking_color},
        {"peaking_threshold", ds.peaking_threshold},
        {"zebra_stripes", ds.zebra_stripes},
        {"zebra_threshold", ds.zebra_threshold},
        {"grid_mode", ds.grid_mode},
        {"jpeg_quality", ds.jpeg_quality},
        {"fps_limit", ds.fps_limit},
        {"false_color", ds.false_color}
    };
}

static json capabilities_to_json(const CameraCapabilities& caps) {
    return {
        {"width", caps.width},
        {"height", caps.height},
        {"target_fps", caps.target_fps},
        {"min_exposure_us", caps.min_exposure_us},
        {"max_exposure_us", caps.max_exposure_us},
        {"min_gain", caps.min_gain},
        {"max_gain", caps.max_gain},
        {"active_sensor_mode", caps.active_sensor_mode}
    };
}

static json recording_status_to_json(const OriginalRecordingStatus& st) {
    json j = {
        {"recording", st.recording},
        {"ready", st.ready},
        {"path", st.path},
        {"error", st.error},
        {"frames", st.frames},
        {"dropped_frames", st.dropped_frames},
        {"width", st.width},
        {"height", st.height},
        {"fps", st.fps},
        {"actual_fps", st.actual_fps},
        {"buffered_sec", st.buffered_sec}
    };
    if (!st.path.empty()) {
        std::string meta = st.metadata_url;
        if (meta.empty()) {
            std::filesystem::path p(st.path);
            std::string stem = p.stem().string();
            meta = "/recordings/" + stem + ".csv";
        }
        j["metadata_url"] = meta;
    }
    return j;
}

static json ai_status_to_json(const AiStatus& st) {
    return {
        {"enabled", st.enabled},
        {"reader_active", st.reader_active},
        {"width", st.width},
        {"height", st.height},
        {"last_sequence", st.last_sequence},
        {"shm_name", st.shm_name}
    };
}

struct HttpServer::Impl {
    CameraService& camera;
    DisplayPipeline& display;
    PresetManager& presets;
    std::string web_dir;
    std::string host;
    int port;
    httplib::Server svr;
    std::mutex sensor_mode_tx_mu;
    ClipCache clip_cache;

    Impl(CameraService& c, DisplayPipeline& d, PresetManager& p,
         const std::string& wd, const std::string& h, int pt)
        : camera(c), display(d), presets(p), web_dir(wd), host(h), port(pt) {}
};

HttpServer::HttpServer(CameraService& camera, DisplayPipeline& display,
                       PresetManager& presets, const std::string& web_dir,
                       const std::string& host, int port)
    : impl_(std::make_unique<Impl>(camera, display, presets, web_dir, host, port)) {}

HttpServer::~HttpServer() {
    stop();
}

std::string HttpServer::get_url() const {
    return "http://" + impl_->host + ":" + std::to_string(impl_->port);
}

bool HttpServer::start() {
    if (running_.load()) return true;

    // 1. Static Files
    if (!impl_->web_dir.empty()) {
        impl_->svr.set_mount_point("/", impl_->web_dir);
    }
    std::error_code ec_snap;
    std::filesystem::create_directories("snapshots", ec_snap);
    impl_->svr.set_mount_point("/snapshots", "snapshots");
    std::error_code ec_rec;
    std::filesystem::create_directories("recordings", ec_rec);
    impl_->svr.set_mount_point("/recordings", "recordings");


    // 2. MJPEG Stream
    impl_->svr.Get("/stream.mjpg", [this](const httplib::Request&, httplib::Response& res) {
        impl_->display.increment_viewers();
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        res.set_header("Pragma", "no-cache");
        res.set_header("Expires", "0");
        res.set_header("Connection", "keep-alive");
        res.set_header("Keep-Alive", "timeout=60, max=10000");

        res.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [this, seq = uint64_t(0), buf = std::vector<uint8_t>()](size_t, httplib::DataSink& sink) mutable {
                if (!sink.is_writable || !sink.is_writable()) return false;

                if (!impl_->display.get_latest_jpeg(buf, seq, 150)) {
                    // Send keepalive or wait
                    return true;
                }

                std::ostringstream header;
                header << "--frame\r\n"
                       << "Content-Type: image/jpeg\r\n"
                       << "Content-Length: " << buf.size() << "\r\n\r\n";
                std::string h_str = header.str();

                if (!sink.write(h_str.data(), h_str.size())) return false;
                if (!sink.write((const char*)buf.data(), buf.size())) return false;
                if (!sink.write("\r\n", 2)) return false;
                return true;
            },
            [this](bool) {
                impl_->display.decrement_viewers();
            }
        );
    });

    // 2b. Single JPEG Frame API (Instant snapshot/fallback)
    impl_->svr.Get("/api/frame.jpg", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<uint8_t> buf;
        uint64_t seq = 0;
        if (impl_->display.get_latest_jpeg(buf, seq, 100)) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
            res.set_content((const char*)buf.data(), buf.size(), "image/jpeg");
        } else {
            res.status = 503;
            res.set_content("No frame available", "text/plain");
        }
    });

    // 3. Server-Sent Events (Stats 4Hz)
    impl_->svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t, httplib::DataSink& sink) {
                if (!sink.is_writable || !sink.is_writable()) return false;

                auto cm = impl_->camera.get_metrics();
                auto dm = impl_->display.get_metrics();
                auto ctrl = impl_->camera.get_controls();
                auto ds = impl_->display.get_settings();
                auto caps = impl_->camera.get_capabilities();

                json payload = {
                    {"camera_metrics", {
                        {"sensor_fps", cm.sensor_fps},
                        {"host_fps", cm.host_fps},
                        {"total_frames", cm.total_frames},
                        {"sequence_gaps", cm.sequence_gaps},
                        {"bad_intervals", cm.bad_sensor_intervals},
                        {"min_interval_ms", cm.min_interval_ms},
                        {"max_interval_ms", cm.max_interval_ms},
                        {"actual_exposure_us", cm.actual_exposure_us},
                        {"actual_analog_gain", cm.actual_analog_gain},
                        {"actual_isp_gain", cm.actual_isp_gain},
                        {"actual_cct", cm.actual_cct},
                        {"uptime_ms", cm.uptime_ms}
                    }},
                    {"display_metrics", {
                        {"display_fps", dm.display_fps},
                        {"encode_ms", dm.encode_ms},
                        {"focus_score", dm.focus_score},
                        {"focus_score_max", dm.focus_score_max},
                        {"active_viewers", dm.active_viewers},
                        {"histogram_64", dm.histogram_64}
                    }},
                    {"controls", controls_to_json(ctrl)},
                    {"display_settings", display_settings_to_json(ds)},
                    {"capabilities", capabilities_to_json(caps)},
                    {"system_metrics", {
                        {"cpu_temp_c", get_cpu_temp_c()},
                        {"ram_used_mb", get_ram_used_mb()}
                    }},
                    {"recording_status", recording_status_to_json(impl_->camera.get_recording_status())},
                    {"ai_status", ai_status_to_json(impl_->camera.get_ai_status())}
                };

                std::string sse_msg = "data: " + payload.dump() + "\n\n";
                if (!sink.write(sse_msg.data(), sse_msg.size())) return false;

                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                return true;
            }
        );
    });

    // 4. API: GET /api/state
    impl_->svr.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        auto cm = impl_->camera.get_metrics();
        auto dm = impl_->display.get_metrics();
        auto ctrl = impl_->camera.get_controls();
        auto ds = impl_->display.get_settings();
        auto caps = impl_->camera.get_capabilities();

        json state = {
            {"controls", controls_to_json(ctrl)},
            {"display_settings", display_settings_to_json(ds)},
            {"capabilities", capabilities_to_json(caps)},
            {"camera_metrics", {
                {"sensor_fps", cm.sensor_fps},
                {"host_fps", cm.host_fps},
                {"total_frames", cm.total_frames},
                {"sequence_gaps", cm.sequence_gaps},
                {"bad_intervals", cm.bad_sensor_intervals},
                {"actual_exposure_us", cm.actual_exposure_us},
                {"actual_analog_gain", cm.actual_analog_gain},
                {"actual_isp_gain", cm.actual_isp_gain},
                {"actual_cct", cm.actual_cct}
            }},
            {"display_metrics", {
                {"display_fps", dm.display_fps},
                {"active_viewers", dm.active_viewers}
            }},
            {"sensor_modes", {
                {{"mode", 0}, {"label", "4K UHD (3840×2160 @ 30 FPS)"}, {"width", 3840}, {"height", 2160}, {"fps", 30.0}},
                {{"mode", 1}, {"label", "Full HD (1920×1080 @ 60 FPS)"}, {"width", 1920}, {"height", 1080}, {"fps", 60.0}},
                {{"mode", 2}, {"label", "120 FPS 초고속 (1332×990 @ 120 FPS)"}, {"width", 1332}, {"height", 990}, {"fps", 120.0}}
            }},
            {"recording_status", recording_status_to_json(impl_->camera.get_recording_status())},
            {"ai_status", ai_status_to_json(impl_->camera.get_ai_status())}
        };
        res.set_content(state.dump(2), "application/json");
    });

    // 5. API: POST /api/controls
    impl_->svr.Post("/api/controls", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            auto ctrl = impl_->camera.get_controls();
            if (j.contains("auto_exposure")) ctrl.auto_exposure = j["auto_exposure"].get<bool>();
            if (j.contains("exposure_us")) ctrl.exposure_us = j["exposure_us"].get<uint64_t>();
            if (j.contains("gain")) ctrl.gain = j["gain"].get<float>();
            if (j.contains("ae_max_exposure_us")) ctrl.ae_max_exposure_us = j["ae_max_exposure_us"].get<uint64_t>();
            if (j.contains("ae_max_gain")) ctrl.ae_max_gain = j["ae_max_gain"].get<float>();
            if (j.contains("ae_lock")) ctrl.ae_lock = j["ae_lock"].get<bool>();
            if (j.contains("ev_compensation")) ctrl.ev_compensation = j["ev_compensation"].get<float>();
            if (j.contains("antibanding")) ctrl.antibanding = j["antibanding"].get<std::string>();
            if (j.contains("awb_mode")) ctrl.awb_mode = j["awb_mode"].get<std::string>();
            if (j.contains("awb_lock")) ctrl.awb_lock = j["awb_lock"].get<bool>();
            if (j.contains("isp_digital_gain")) ctrl.isp_digital_gain = j["isp_digital_gain"].get<float>();
            if (j.contains("denoise_mode")) ctrl.denoise_mode = j["denoise_mode"].get<std::string>();
            if (j.contains("edge_mode")) ctrl.edge_mode = j["edge_mode"].get<std::string>();

            impl_->camera.update_controls(ctrl);
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 5b. API: POST /api/sensor_mode
    impl_->svr.Post("/api/sensor_mode", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            std::unique_lock<std::mutex> tx_lock(impl_->sensor_mode_tx_mu);
            json j = json::parse(req.body);
            int mode = j.value("mode", 2);
            if (mode < 0 || mode > 2) {
                res.status = 400;
                res.set_content("{\"status\":\"error\",\"error\":\"mode must be 0, 1, or 2\"}", "application/json");
                return;
            }
            int current_mode = impl_->camera.get_sensor_mode();
            if (current_mode == mode) {
                auto caps = impl_->camera.get_capabilities();
                json out = {
                    {"status", "ok"},
                    {"mode", mode},
                    {"width", caps.width},
                    {"height", caps.height},
                    {"target_fps", caps.target_fps}
                };
                res.set_content(out.dump(), "application/json");
                return;
            }

            bool was_recording = impl_->camera.get_recording_status().recording;
            if (was_recording) {
                impl_->camera.stop_recording();
            }
            impl_->display.stop(); // Quiesce display worker: joined before camera teardown

            bool ok = impl_->camera.set_sensor_mode(mode);

            impl_->display.start(); // Restart display pipeline with new resolution
            if (was_recording && ok) {
                impl_->camera.start_recording("recordings");
            }
            if (ok) {
                auto caps = impl_->camera.get_capabilities();
                json out = {
                    {"status", "ok"},
                    {"mode", mode},
                    {"width", caps.width},
                    {"height", caps.height},
                    {"target_fps", caps.target_fps}
                };
                res.set_content(out.dump(), "application/json");
            } else {
                res.status = 500;
                res.set_content("{\"status\":\"error\",\"error\":\"Failed to switch sensor mode\"}", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"status\":\"error\",\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 6. API: POST /api/display
    impl_->svr.Post("/api/display", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            auto ds = impl_->display.get_settings();
            if (j.contains("zoom_mode")) ds.zoom_mode = j["zoom_mode"].get<std::string>();
            if (j.contains("center_x")) ds.center_x = j["center_x"].get<float>();
            if (j.contains("center_y")) ds.center_y = j["center_y"].get<float>();
            if (j.contains("focus_peaking")) ds.focus_peaking = j["focus_peaking"].get<bool>();
            if (j.contains("peaking_color")) ds.peaking_color = j["peaking_color"].get<std::string>();
            if (j.contains("peaking_threshold")) ds.peaking_threshold = j["peaking_threshold"].get<int>();
            if (j.contains("zebra_stripes")) ds.zebra_stripes = j["zebra_stripes"].get<bool>();
            if (j.contains("zebra_threshold")) ds.zebra_threshold = j["zebra_threshold"].get<int>();
            if (j.contains("grid_mode")) ds.grid_mode = j["grid_mode"].get<std::string>();
            if (j.contains("jpeg_quality")) ds.jpeg_quality = j["jpeg_quality"].get<int>();
            if (j.contains("fps_limit")) ds.fps_limit = j["fps_limit"].get<int>();
            if (j.contains("false_color")) ds.false_color = j["false_color"].get<bool>();

            impl_->display.update_settings(ds);
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 7. API: POST /api/snapshot
    impl_->svr.Post("/api/snapshot", [this](const httplib::Request&, httplib::Response& res) {
        try {
            std::string path = impl_->display.capture_snapshot("snapshots");
            if (path.empty()) {
                res.status = 500;
                res.set_content("{\"error\":\"Snapshot capture failed\"}", "application/json");
            } else {
                json resp = {{"status", "ok"}, {"path", path}};
                res.set_content(resp.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } catch (...) {
            res.status = 500;
            res.set_content("{\"error\":\"Unknown snapshot exception\"}", "application/json");
        }
    });
    // 7b. API: GET /api/snapshots
    impl_->svr.Get("/api/snapshots", [](const httplib::Request&, httplib::Response& res) {
        namespace fs = std::filesystem;
        json list = json::array();
        std::error_code ec;
        if (fs::exists("snapshots", ec)) {
            for (const auto& entry : fs::directory_iterator("snapshots", ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
                    list.push_back({
                        {"filename", entry.path().filename().string()},
                        {"url", "/snapshots/" + entry.path().filename().string()},
                        {"size", (uint64_t)entry.file_size(ec)}
                    });
                }
            }
        }
        res.set_content(list.dump(), "application/json");
    });

    // 7c. API: Video Recording
    impl_->svr.Post("/api/record/start", [this](const httplib::Request&, httplib::Response& res) {
        std::unique_lock<std::mutex> tx_lock(impl_->sensor_mode_tx_mu);
        bool ok = impl_->camera.start_recording("recordings");
        auto st = impl_->camera.get_recording_status();
        json resp = recording_status_to_json(st);
        if (ok) {
            resp["status"] = "ok";
            res.set_content(resp.dump(), "application/json");
        } else {
            resp["status"] = "error";
            if (resp["error"].get<std::string>().empty()) {
                resp["error"] = "Failed to start recording";
            }
            res.status = 500;
            res.set_content(resp.dump(), "application/json");
        }
    });

    impl_->svr.Post("/api/record/stop", [this](const httplib::Request&, httplib::Response& res) {
        std::unique_lock<std::mutex> tx_lock(impl_->sensor_mode_tx_mu);
        bool ok = impl_->camera.stop_recording();
        auto st = impl_->camera.get_recording_status();
        json resp = recording_status_to_json(st);
        resp["status"] = ok ? "ok" : "error";
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Get("/api/record/status", [this](const httplib::Request&, httplib::Response& res) {
        auto st = impl_->camera.get_recording_status();
        json resp = recording_status_to_json(st);
        resp["status"] = st.error.empty() ? "ok" : "error";
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Get("/api/recordings", [](const httplib::Request&, httplib::Response& res) {
        namespace fs = std::filesystem;
        json list = json::array();
        std::error_code ec;
        if (fs::exists("recordings", ec)) {
            for (const auto& entry : fs::directory_iterator("recordings", ec)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".avi" || ext == ".mp4") {
                        json item = {
                            {"filename", entry.path().filename().string()},
                            {"url", "/recordings/" + entry.path().filename().string()},
                            {"size", (uint64_t)entry.file_size(ec)}
                        };
                        fs::path csv_candidate = entry.path().parent_path() / (entry.path().stem().string() + ".csv");
                        std::error_code ec_csv;
                        if (fs::exists(csv_candidate, ec_csv) && fs::is_regular_file(csv_candidate, ec_csv)) {
                            item["metadata_url"] = "/recordings/" + entry.path().stem().string() + ".csv";
                        }
                        list.push_back(item);
                    }
                }
            }
        }
        std::sort(list.begin(), list.end(), [](const json& a, const json& b) {
            return a.value("filename", "") > b.value("filename", "");
        });
        res.set_content(list.dump(), "application/json");
    });

    // 7d. API: Camera Calibration
    impl_->svr.Post("/api/calibration/capture", [this](const httplib::Request&, httplib::Response& res) {
        bool found = impl_->display.capture_calibration_frame();
        json resp = {
            {"status", "ok"},
            {"chessboard_found", found},
            {"frame_count", impl_->display.get_calibration_frame_count()}
        };
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Post("/api/calibration/compute", [this](const httplib::Request&, httplib::Response& res) {
        json result = impl_->display.compute_calibration();
        res.set_content(result.dump(2), "application/json");
    });

    impl_->svr.Post("/api/calibration/clear", [this](const httplib::Request&, httplib::Response& res) {
        impl_->display.clear_calibration_frames();
        res.set_content("{\"status\":\"ok\",\"frame_count\":0}", "application/json");
    });

    impl_->svr.Get("/api/calibration", [this](const httplib::Request&, httplib::Response& res) {
        json calib = impl_->display.get_calibration();
        res.set_content(calib.dump(2), "application/json");
    });

    // 7e. API: Pitch Detection & Auto-Clipping
    impl_->svr.Get("/api/pitch/status", [this](const httplib::Request&, httplib::Response& res) {
        auto ps = impl_->display.get_pitch_status();
        auto cfg = impl_->display.get_pitch_config();
        json resp = {
            {"enabled", ps.enabled},
            {"is_clipping", ps.is_clipping},
            {"motion_level", ps.current_motion_level},
            {"total_clips", ps.total_clips},
            {"last_clip_path", ps.last_clip_path},
            {"fps", ps.fps},
            {"width", ps.width},
            {"height", ps.height},
            {"buffered_sec", ps.buffered_sec},
            {"error", ps.error},
            {"config", {
                {"threshold", cfg.motion_threshold},
                {"min_motion_pixels", cfg.min_motion_pixels},
                {"pre_roll_sec", cfg.pre_roll_sec},
                {"post_roll_sec", cfg.post_roll_sec},
                {"cooldown_sec", cfg.cooldown_sec}
            }}
        };
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Post("/api/pitch/config", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);
            auto cfg = impl_->display.get_pitch_config();
            if (j.contains("enabled")) cfg.enabled = j["enabled"].get<bool>();
            if (j.contains("threshold")) cfg.motion_threshold = j["threshold"].get<int>();
            if (j.contains("min_motion_pixels")) cfg.min_motion_pixels = j["min_motion_pixels"].get<int>();
            if (j.contains("pre_roll_sec")) cfg.pre_roll_sec = j["pre_roll_sec"].get<float>();
            if (j.contains("post_roll_sec")) cfg.post_roll_sec = j["post_roll_sec"].get<float>();
            if (j.contains("cooldown_sec")) cfg.cooldown_sec = j["cooldown_sec"].get<float>();
            impl_->display.update_pitch_config(cfg);
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    impl_->svr.Get("/api/pitch/clips", [](const httplib::Request&, httplib::Response& res) {
        namespace fs = std::filesystem;
        json list = json::array();
        std::error_code ec;
        if (fs::exists("recordings", ec)) {
            for (const auto& entry : fs::directory_iterator("recordings", ec)) {
                if (entry.is_regular_file()) {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind("pitch_", 0) == 0 && entry.path().extension() == ".avi") {
                        list.push_back({
                            {"filename", fname},
                            {"url", "/recordings/" + fname},
                            {"size", (uint64_t)entry.file_size(ec)}
                        });
                    }
                }
            }
        }
        std::sort(list.begin(), list.end(), [](const json& a, const json& b) {
            return a.value("filename", "") > b.value("filename", "");
        });
        res.set_content(list.dump(), "application/json");
    });

    impl_->svr.Post("/api/pitch/trigger", [this](const httplib::Request&, httplib::Response& res) {
        if (impl_->display.trigger_pitch_clip("manual")) {
            auto st = impl_->display.get_pitch_status();
            json resp = {
                {"status", "ok"},
                {"path", st.last_clip_path}
            };
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 409;
            res.set_content("{\"status\":\"error\",\"error\":\"Clip trigger failed or already clipping\"}", "application/json");
        }
    });

    // 7f. API: Frame-Accurate Video Clip Replay Proxy
    impl_->svr.Get("/api/clip/info", [this](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content("{\"error\":\"missing path\"}", "application/json");
            return;
        }
        std::string raw_path = req.get_param_value("path");
        ResolvedRecording resolved;
        ResolveStatus st = resolve_recording_path(raw_path, resolved);
        if (st == ResolveStatus::InvalidInput) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid filename\"}", "application/json");
            return;
        }
        if (st == ResolveStatus::NotFound) {
            res.status = 404;
            res.set_content("{\"error\":\"cannot open video clip\"}", "application/json");
            return;
        }

        auto session = impl_->clip_cache.get_or_open(
            resolved.canonical_path.string(), resolved.file_size, resolved.mtime);
        if (!session) {
            res.status = 404;
            res.set_content("{\"error\":\"cannot open video clip\"}", "application/json");
            return;
        }

        double fps = session->fps;
        int total = session->total_frames;
        int w = session->width;
        int h = session->height;
        json info = {
            {"path", raw_path},
            {"filename", resolved.filename},
            {"fps", fps},
            {"total_frames", total},
            {"width", w},
            {"height", h},
            {"duration_sec", (fps > 0.0 ? static_cast<double>(total) / fps : 0.0)}
        };
        res.set_content(info.dump(), "application/json");
    });

    impl_->svr.Get("/api/clip/frame", [this](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content("missing path", "text/plain");
            return;
        }
        std::string raw_path = req.get_param_value("path");
        ResolvedRecording resolved;
        ResolveStatus st = resolve_recording_path(raw_path, resolved);
        if (st == ResolveStatus::InvalidInput) {
            res.status = 400;
            res.set_content("invalid filename", "text/plain");
            return;
        }
        if (st == ResolveStatus::NotFound) {
            res.status = 404;
            res.set_content("cannot open video", "text/plain");
            return;
        }

        int frame_idx = 0;
        if (req.has_param("frame")) {
            if (!parse_nonnegative_int(req.get_param_value("frame"), frame_idx)) {
                res.status = 400;
                res.set_content("invalid frame index", "text/plain");
                return;
            }
        }

        auto session = impl_->clip_cache.get_or_open(
            resolved.canonical_path.string(), resolved.file_size, resolved.mtime);
        if (!session) {
            res.status = 404;
            res.set_content("cannot open video", "text/plain");
            return;
        }

        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(session->mu);
            if (!session->read_frame(frame_idx, frame)) {
                res.status = 404;
                res.set_content("frame not found", "text/plain");
                return;
            }
        }

        std::vector<uint8_t> buf;
        cv::imencode(".jpg", frame, buf);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_content(reinterpret_cast<const char*>(buf.data()), buf.size(), "image/jpeg");
    });


    // 8. API: Presets
    impl_->svr.Get("/api/presets", [this](const httplib::Request&, httplib::Response& res) {
        auto list = impl_->presets.list_presets();
        json resp = list;
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Get(R"(/api/presets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        json data;
        if (impl_->presets.load_preset(name, data)) {
            res.set_content(data.dump(2), "application/json");
        } else {
            res.status = 404;
            res.set_content("{\"error\":\"Preset not found\"}", "application/json");
        }
    });

    impl_->svr.Post(R"(/api/presets/([^/]+)/apply)", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            std::unique_lock<std::mutex> tx_lock(impl_->sensor_mode_tx_mu);
            std::string name = req.matches[1];
            json preset_data;
            if (!impl_->presets.load_preset(name, preset_data)) {
                res.status = 404;
                res.set_content("{\"status\":\"error\",\"error\":\"Preset not found\"}", "application/json");
                return;
            }

            int target_mode = -1;
            if (preset_data.contains("sensor_mode")) {
                if (!preset_data["sensor_mode"].is_number_integer()) {
                    res.status = 400;
                    res.set_content("{\"status\":\"error\",\"error\":\"sensor_mode must be an integer\"}", "application/json");
                    return;
                }
                target_mode = preset_data["sensor_mode"].get<int>();
                if (target_mode < 0 || target_mode > 2) {
                    res.status = 400;
                    res.set_content("{\"status\":\"error\",\"error\":\"sensor_mode must be 0, 1, or 2\"}", "application/json");
                    return;
                }
            }

            CameraControls ctrl = impl_->camera.get_controls();
            if (preset_data.contains("controls") && preset_data["controls"].is_object()) {
                const auto& c = preset_data["controls"];
                if (c.contains("auto_exposure")) ctrl.auto_exposure = c["auto_exposure"].get<bool>();
                if (c.contains("exposure_us")) ctrl.exposure_us = c["exposure_us"].get<uint64_t>();
                if (c.contains("gain")) ctrl.gain = c["gain"].get<float>();
                if (c.contains("ae_max_exposure_us")) ctrl.ae_max_exposure_us = c["ae_max_exposure_us"].get<uint64_t>();
                if (c.contains("ae_max_gain")) ctrl.ae_max_gain = c["ae_max_gain"].get<float>();
                if (c.contains("ae_lock")) ctrl.ae_lock = c["ae_lock"].get<bool>();
                if (c.contains("ev_compensation")) ctrl.ev_compensation = c["ev_compensation"].get<float>();
                if (c.contains("antibanding")) ctrl.antibanding = c["antibanding"].get<std::string>();
                if (c.contains("awb_mode")) ctrl.awb_mode = c["awb_mode"].get<std::string>();
                if (c.contains("awb_lock")) ctrl.awb_lock = c["awb_lock"].get<bool>();
                if (c.contains("isp_digital_gain")) ctrl.isp_digital_gain = c["isp_digital_gain"].get<float>();
                if (c.contains("denoise_mode")) ctrl.denoise_mode = c["denoise_mode"].get<std::string>();
                if (c.contains("edge_mode")) ctrl.edge_mode = c["edge_mode"].get<std::string>();
            }

            DisplaySettings ds = impl_->display.get_settings();
            const json* disp_ptr = nullptr;
            if (preset_data.contains("display") && preset_data["display"].is_object()) {
                disp_ptr = &preset_data["display"];
            } else if (preset_data.contains("display_settings") && preset_data["display_settings"].is_object()) {
                disp_ptr = &preset_data["display_settings"];
            }
            if (disp_ptr) {
                const auto& d = *disp_ptr;
                if (d.contains("zoom_mode")) ds.zoom_mode = d["zoom_mode"].get<std::string>();
                if (d.contains("center_x")) ds.center_x = d["center_x"].get<float>();
                if (d.contains("center_y")) ds.center_y = d["center_y"].get<float>();
                if (d.contains("focus_peaking")) ds.focus_peaking = d["focus_peaking"].get<bool>();
                if (d.contains("peaking_color")) ds.peaking_color = d["peaking_color"].get<std::string>();
                if (d.contains("peaking_threshold")) ds.peaking_threshold = d["peaking_threshold"].get<int>();
                if (d.contains("zebra_stripes")) ds.zebra_stripes = d["zebra_stripes"].get<bool>();
                if (d.contains("zebra_threshold")) ds.zebra_threshold = d["zebra_threshold"].get<int>();
                if (d.contains("grid_mode")) ds.grid_mode = d["grid_mode"].get<std::string>();
                if (d.contains("jpeg_quality")) ds.jpeg_quality = d["jpeg_quality"].get<int>();
                if (d.contains("fps_limit")) ds.fps_limit = d["fps_limit"].get<int>();
                if (d.contains("false_color")) ds.false_color = d["false_color"].get<bool>();
            }

            // Apply sensor_mode if present and different from active mode
            if (target_mode >= 0 && target_mode != impl_->camera.get_sensor_mode()) {
                bool was_recording = impl_->camera.get_recording_status().recording;
                if (was_recording) {
                    impl_->camera.stop_recording();
                }
                impl_->display.stop();

                bool ok = impl_->camera.set_sensor_mode(target_mode);

                impl_->display.start();
                if (was_recording && ok) {
                    impl_->camera.start_recording("recordings");
                }
                if (!ok) {
                    res.status = 500;
                    res.set_content("{\"status\":\"error\",\"error\":\"Failed to switch sensor mode during preset apply\"}", "application/json");
                    return;
                }
            }

            // Apply camera controls
            if (preset_data.contains("controls")) {
                impl_->camera.update_controls(ctrl);
            }

            // Apply display settings
            if (disp_ptr != nullptr) {
                impl_->display.update_settings(ds);
            }

            auto caps = impl_->camera.get_capabilities();
            auto rec_st = impl_->camera.get_recording_status();
            auto ai_st = impl_->camera.get_ai_status();

            json resp = {
                {"status", "ok"},
                {"preset", name},
                {"name", name},
                {"mode", caps.active_sensor_mode},
                {"width", caps.width},
                {"height", caps.height},
                {"target_fps", caps.target_fps},
                {"controls", controls_to_json(ctrl)},
                {"display_settings", display_settings_to_json(ds)},
                {"capabilities", capabilities_to_json(caps)},
                {"recording_status", recording_status_to_json(rec_st)},
                {"ai_status", ai_status_to_json(ai_st)}
            };
            res.set_content(resp.dump(2), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"status\":\"error\",\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    impl_->svr.Post(R"(/api/presets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        try {
            json data = json::parse(req.body);
            if (impl_->presets.save_preset(name, data)) {
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } else {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to save preset\"}", "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        }
    });

    impl_->svr.Delete(R"(/api/presets/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        if (impl_->presets.delete_preset(name)) {
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } else {
            res.status = 404;
            res.set_content("{\"error\":\"Failed to delete preset\"}", "application/json");
        }
    });

    running_.store(true);
    thread_ = std::thread([this]() {
        impl_->svr.listen(impl_->host.c_str(), impl_->port);
        running_.store(false);
    });

    // Wait up to 1 second for socket to bind
    for (int i = 0; i < 20 && !impl_->svr.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return impl_->svr.is_running();
}

void HttpServer::stop() {
    if (running_.exchange(false)) {
        impl_->svr.stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}
