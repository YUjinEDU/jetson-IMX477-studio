// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_HTTP_SERVER_H
#define STUDIO_HTTP_SERVER_H

#include <memory>
#include <string>
#include <thread>
#include <atomic>

class CameraService;
class DisplayPipeline;
class PresetManager;

class HttpServer {
public:
    HttpServer(CameraService& camera, DisplayPipeline& display,
               PresetManager& presets, const std::string& web_dir,
               const std::string& host = "127.0.0.1", int port = 8477);
    ~HttpServer();

    bool start();
    void stop();

    std::string get_url() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

#endif // STUDIO_HTTP_SERVER_H
