#pragma once

#include <string>
#include <vector>

#include "gui_setup_panel.hpp"

namespace inop {
namespace gui {

struct SavedConfigInfo {
    std::string filename;
    std::string path;
    std::string suite_code;
};

struct DeveloperPreset {
    std::string name;
    PanelState state;
};

const std::vector<DeveloperPreset>& developer_presets();

constexpr int kMaxSavedPerSuite = 360;

std::vector<SavedConfigInfo> list_configs();

bool config_exists(const std::string& filename);

std::string suggest_filename(const PanelState& state);

bool save_config(const PanelState& state, const std::string& filename, std::string* error);

bool load_config(const std::string& path, PanelState& out, std::string* error);

bool delete_config(const std::string& path, std::string* error);

}
}

