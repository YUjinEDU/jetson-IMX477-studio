// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/camera_service.h"
#include "studio/display_pipeline.h"
#include "studio/preset_manager.h"
#include "studio/http_server.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static std::atomic<bool> g_shutdown{false};
static void on_signal(int sig) {
    (void)sig;
    g_shutdown.store(true);
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --host HOST         Bind host (default: 127.0.0.1; use 0.0.0.0 for remote access)\n"
              << "  --port PORT         HTTP server port (default: 8477)\n"
              << "  --ai-width W        AI stream width in pixels: 0, 320, 640 (default: 320)\n"
              << "  --shm-name NAME     Shared memory name (default: " << STUDIO_SHM_NAME << ")\n"
              << "  --web-dir DIR       Static web directory (default: ./web)\n"
              << "  --exposure-us US    Initial exposure in microseconds (default: 4000)\n"
              << "  --gain GAIN         Initial analog gain multiplier (default: 2.0)\n"
              << "  --help              Show this help message\n";
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8477;
    int ai_width = 320;
    std::string shm_name = STUDIO_SHM_NAME;
    std::string web_dir = "web";
    uint64_t init_exp_us = 4000;
    float init_gain = 2.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--ai-width" && i + 1 < argc) {
            ai_width = std::atoi(argv[++i]);
        } else if (arg == "--shm-name" && i + 1 < argc) {
            shm_name = argv[++i];
        } else if (arg == "--web-dir" && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (arg == "--exposure-us" && i + 1 < argc) {
            init_exp_us = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--gain" && i + 1 < argc) {
            init_gain = std::strtof(argv[++i], nullptr);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }

    // Resolve web_dir
    if (!fs::exists(web_dir)) {
        // Try relative to binary
        fs::path bin_dir = fs::path(argv[0]).parent_path();
        if (fs::exists(bin_dir / "../web")) {
            web_dir = (bin_dir / "../web").lexically_normal().string();
        } else if (fs::exists(bin_dir / "web")) {
            web_dir = (bin_dir / "web").lexically_normal().string();
        }
    }

    std::cout << "====================================================\n"
              << "  Jetson IMX477 Camera Studio (120fps ISP Service)  \n"
              << "====================================================\n"
              << "  Sensor Mode : 2 (1332x990 @ 120fps)\n"
              << "  AI Stream   : " << (ai_width ? (std::to_string(ai_width) + "x" + std::to_string((int)std::lround(990.0 * ai_width / 1332) & ~1)) : "Disabled") << "\n"
              << "  AI SHM Ring : " << (ai_width ? shm_name : "None") << "\n"
              << "  Exposure    : " << init_exp_us << " us (" << (init_exp_us / 1000.0) << " ms)\n"
              << "  Analog Gain : " << init_gain << "x\n"
              << "  Web Root    : " << web_dir << "\n"
              << "  Web URL     : http://" << host << ":" << port << "\n"
              << "====================================================\n";

    std::signal(SIGINT, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, on_signal);

    CameraService camera;
    CameraControls ctrl = camera.get_controls();
    ctrl.exposure_us = init_exp_us;
    ctrl.gain = init_gain;
    ctrl.auto_exposure = false;
    ctrl.awb_mode = "auto";
    camera.update_controls(ctrl);

    std::cout << "Starting Argus Camera Service..." << std::endl;
    if (!camera.start(ai_width, shm_name)) {
        std::cerr << "ERROR: Failed to start Argus camera session!\n"
                  << "Check that:\n"
                  << "  1. No other camera process is running (pgrep -fa 'argus|isp')\n"
                  << "  2. Sony IMX477 mode 2 device tree and NITO are loaded\n";
        return 1;
    }
    std::cout << "Camera service connected and streaming at 120fps." << std::endl;

    DisplayPipeline display(camera);
    if (!display.start()) {
        std::cerr << "ERROR: Failed to start display pipeline" << std::endl;
        camera.stop();
        return 1;
    }

    PresetManager presets;
    HttpServer server(camera, display, presets, web_dir, host, port);
    if (!server.start()) {
        std::cerr << "ERROR: Failed to bind HTTP server to " << host << ":" << port << std::endl;
        display.stop();
        camera.stop();
        return 1;
    }

    std::cout << "\n>>> Studio UI is LIVE at " << server.get_url() << " <<<\n"
              << "Press Ctrl+C to terminate cleanly.\n" << std::endl;

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nShutting down Studio cleanly..." << std::endl;
    server.stop();
    display.stop();
    camera.stop();
    std::cout << "All resources released. Goodbye!" << std::endl;
    return 0;
}
