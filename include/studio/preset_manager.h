// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU
#ifndef STUDIO_PRESET_MANAGER_H
#define STUDIO_PRESET_MANAGER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class PresetManager {
public:
    explicit PresetManager(const std::string& custom_dir = "");

    std::vector<std::string> list_presets() const;
    bool save_preset(const std::string& name, const nlohmann::json& data);
    bool load_preset(const std::string& name, nlohmann::json& out_data) const;
    bool delete_preset(const std::string& name);

    std::string get_storage_dir() const { return storage_dir_; }

private:
    std::string storage_dir_;
    void ensure_defaults();
};

#endif // STUDIO_PRESET_MANAGER_H
