// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/preset_manager.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

PresetManager::PresetManager(const std::string& custom_dir) {
    if (!custom_dir.empty()) {
        storage_dir_ = custom_dir;
    } else {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) {
            storage_dir_ = std::string(xdg) + "/jetson-imx477-studio/presets";
        } else {
            const char* home = std::getenv("HOME");
            storage_dir_ = (home ? std::string(home) : ".") + "/.config/jetson-imx477-studio/presets";
        }
    }
    std::error_code ec;
    fs::create_directories(storage_dir_, ec);
    ensure_defaults();
}

void PresetManager::ensure_defaults() {
    auto outdoor_path = storage_dir_ + "/baseball-outdoor-120fps.json";
    if (!fs::exists(outdoor_path)) {
        nlohmann::json j = {
            {"description", "120fps high-speed baseball tracking: low motion blur (1ms)"},
            {"controls", {
                {"auto_exposure", false},
                {"exposure_us", 1000},
                {"gain", 4.0},
                {"awb_mode", "auto"},
                {"antibanding", "auto"}
            }},
            {"display", {
                {"zoom_mode", "fit"},
                {"fps_limit", 30},
                {"grid_mode", "crosshair"}
            }}
        };
        save_preset("baseball-outdoor-120fps", j);
    }

    auto default_path = storage_dir_ + "/default-120fps.json";
    if (!fs::exists(default_path)) {
        nlohmann::json j = {
            {"description", "Default IMX477 120fps baseline: 4ms exposure, 2x gain"},
            {"controls", {
                {"auto_exposure", false},
                {"exposure_us", 4000},
                {"gain", 2.0},
                {"awb_mode", "auto"},
                {"antibanding", "auto"}
            }},
            {"display", {
                {"zoom_mode", "fit"},
                {"fps_limit", 30},
                {"grid_mode", "none"}
            }}
        };
        save_preset("default-120fps", j);
    }
}

std::vector<std::string> PresetManager::list_presets() const {
    std::vector<std::string> list;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(storage_dir_, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            list.push_back(entry.path().stem().string());
        }
    }
    return list;
}

bool PresetManager::save_preset(const std::string& name, const nlohmann::json& data) {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return false;
    }
    std::string path = storage_dir_ + "/" + name + ".json";
    std::ofstream out(path);
    if (!out) return false;
    out << data.dump(2) << std::endl;
    return true;
}

bool PresetManager::load_preset(const std::string& name, nlohmann::json& out_data) const {
    std::string path = storage_dir_ + "/" + name + ".json";
    std::ifstream in(path);
    if (!in) return false;
    try {
        in >> out_data;
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::delete_preset(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return false;
    std::string path = storage_dir_ + "/" + name + ".json";
    std::error_code ec;
    return fs::remove(path, ec);
}
