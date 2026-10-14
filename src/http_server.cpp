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

struct HttpServer::Impl {
    CameraService& camera;
    DisplayPipeline& display;
    PresetManager& presets;
    std::string web_dir;
    std::string host;
    int port;
    httplib::Server svr;
    std::mutex sensor_mode_tx_mu;

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
                    {"controls", {
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
                    }},
                    {"display_settings", {
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
                        {"false_color", ds.false_color},
                    }},
                    {"capabilities", {
                        {"width", caps.width},
                        {"height", caps.height},
                        {"target_fps", caps.target_fps},
                        {"min_exposure_us", caps.min_exposure_us},
                        {"max_exposure_us", caps.max_exposure_us},
                        {"min_gain", caps.min_gain},
                        {"max_gain", caps.max_gain},
                        {"active_sensor_mode", caps.active_sensor_mode}
                    }},
                    {"system_metrics", {
                        {"cpu_temp_c", get_cpu_temp_c()},
                        {"ram_used_mb", get_ram_used_mb()}
                    }}
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
            {"controls", {
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
            }},
            {"display_settings", {
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
                {"false_color", ds.false_color},
            }},
            {"capabilities", {
                {"width", caps.width},
                {"height", caps.height},
                {"target_fps", caps.target_fps},
                {"min_exposure_us", caps.min_exposure_us},
                {"max_exposure_us", caps.max_exposure_us},
                {"min_gain", caps.min_gain},
                {"max_gain", caps.max_gain},
                {"active_sensor_mode", caps.active_sensor_mode}
            }},
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
            bool was_recording = impl_->display.is_recording();
            if (was_recording) {
                impl_->display.stop_recording();
            }
            impl_->display.stop(); // Quiesce display worker: joined before camera teardown

            bool ok = impl_->camera.set_sensor_mode(mode);

            impl_->display.start(); // Restart display pipeline with new resolution
            if (was_recording && ok) {
                impl_->display.start_recording("recordings");
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
        if (impl_->display.start_recording("recordings")) {
            json resp = {
                {"status", "ok"},
                {"recording", true},
                {"path", impl_->display.get_recording_path()}
            };
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"status\":\"error\",\"error\":\"Failed to start recording\"}", "application/json");
        }
    });

    impl_->svr.Post("/api/record/stop", [this](const httplib::Request&, httplib::Response& res) {
        std::string path = impl_->display.get_recording_path();
        uint64_t frames = impl_->display.get_recorded_frames();
        bool ok = impl_->display.stop_recording();
        json resp = {
            {"status", ok ? "ok" : "error"},
            {"recording", false},
            {"path", path},
            {"frames", frames}
        };
        res.set_content(resp.dump(), "application/json");
    });

    impl_->svr.Get("/api/record/status", [this](const httplib::Request&, httplib::Response& res) {
        json resp = {
            {"recording", impl_->display.is_recording()},
            {"path", impl_->display.get_recording_path()},
            {"frames", impl_->display.get_recorded_frames()}
        };
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
                        list.push_back({
                            {"filename", entry.path().filename().string()},
                            {"url", "/recordings/" + entry.path().filename().string()},
                            {"size", (uint64_t)entry.file_size(ec)}
                        });
                    }
                }
            }
        }
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
