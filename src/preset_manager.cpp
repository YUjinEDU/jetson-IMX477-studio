// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#include "studio/preset_manager.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cctype>

static bool is_valid_preset_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}


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
    // 1. indoor-preview (1080p 60fps sensor mode 1)
    auto indoor_prev_path = storage_dir_ + "/indoor-preview.json";
    if (!fs::exists(indoor_prev_path)) {
        nlohmann::json j = {
            {"description", "Indoor preview: 1080p 60fps sensor mode, auto exposure (max 8ms, max gain 16x)"},
            {"sensor_mode", 1},
            {"controls", {
                {"auto_exposure", true},
                {"ae_max_exposure_us", 8000},
                {"ae_max_gain", 16.0},
                {"awb_mode", "auto"},
                {"antibanding", "60hz"}
            }},
            {"display", {
                {"zoom_mode", "fit"},
                {"fps_limit", 30},
                {"grid_mode", "none"},
                {"false_color", false},
                {"focus_peaking", false},
                {"zebra_stripes", false}
            }}
        };
        save_preset("indoor-preview", j);
    } else {
        nlohmann::json j;
        if (load_preset("indoor-preview", j) && !j.contains("sensor_mode")) {
            j["sensor_mode"] = 1;
            save_preset("indoor-preview", j);
        }
    }

    // 2. pitch-ai-120fps (120fps sensor mode 2)
    auto pitch_ai_path = storage_dir_ + "/pitch-ai-120fps.json";
    if (!fs::exists(pitch_ai_path)) {
        nlohmann::json j = {
            {"description", "Pitch AI 120fps tracking: 120fps sensor mode, auto exposure (max 1ms, max gain 22.25x)"},
            {"sensor_mode", 2},
            {"controls", {
                {"auto_exposure", true},
                {"ae_max_exposure_us", 1000},
                {"ae_max_gain", 22.25},
                {"awb_mode", "auto"},
                {"antibanding", "60hz"}
            }},
            {"display", {
                {"zoom_mode", "fit"},
                {"fps_limit", 30},
                {"grid_mode", "crosshair"},
                {"false_color", false},
                {"focus_peaking", false},
                {"zebra_stripes", false}
            }}
        };
        save_preset("pitch-ai-120fps", j);
    } else {
        nlohmann::json j;
        if (load_preset("pitch-ai-120fps", j) && !j.contains("sensor_mode")) {
            j["sensor_mode"] = 2;
            save_preset("pitch-ai-120fps", j);
        }
    }

    // 3. baseball-outdoor-120fps
    auto outdoor_path = storage_dir_ + "/baseball-outdoor-120fps.json";
    if (!fs::exists(outdoor_path)) {
        nlohmann::json j = {
            {"description", "120fps high-speed baseball tracking: low motion blur (1ms)"},
            {"sensor_mode", 2},
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
    } else {
        nlohmann::json j;
        if (load_preset("baseball-outdoor-120fps", j) && !j.contains("sensor_mode")) {
            j["sensor_mode"] = 2;
            save_preset("baseball-outdoor-120fps", j);
        }
    }

    // 4. default-120fps
    auto default_path = storage_dir_ + "/default-120fps.json";
    if (!fs::exists(default_path)) {
        nlohmann::json j = {
            {"description", "Default IMX477 120fps baseline: 4ms exposure, 2x gain"},
            {"sensor_mode", 2},
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
    } else {
        nlohmann::json j;
        if (load_preset("default-120fps", j) && !j.contains("sensor_mode")) {
            j["sensor_mode"] = 2;
            save_preset("default-120fps", j);
        }
    }

    // 5. indoor-office-120fps
    auto indoor_path = storage_dir_ + "/indoor-office-120fps.json";
    if (!fs::exists(indoor_path)) {
        nlohmann::json j = {
            {"description", "Indoor 60Hz room lighting: 8ms exposure, 8x gain, fluorescent white balance"},
            {"sensor_mode", 2},
            {"controls", {
                {"auto_exposure", false},
                {"exposure_us", 8000},
                {"gain", 8.0},
                {"awb_mode", "fluorescent"},
                {"antibanding", "60hz"}
            }},
            {"display", {
                {"zoom_mode", "fit"},
                {"fps_limit", 30},
                {"grid_mode", "none"}
            }}
        };
        save_preset("indoor-office-120fps", j);
    } else {
        nlohmann::json j;
        if (load_preset("indoor-office-120fps", j) && !j.contains("sensor_mode")) {
            j["sensor_mode"] = 2;
            save_preset("indoor-office-120fps", j);
        }
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
    if (!is_valid_preset_name(name)) return false;
    std::string path = storage_dir_ + "/" + name + ".json";
    std::ofstream out(path);
    if (!out) return false;
    out << data.dump(2) << std::endl;
    return true;
}

bool PresetManager::load_preset(const std::string& name, nlohmann::json& out_data) const {
    if (!is_valid_preset_name(name)) return false;
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
    if (!is_valid_preset_name(name)) return false;
    std::string path = storage_dir_ + "/" + name + ".json";
    std::error_code ec;
    return fs::remove(path, ec);
}
