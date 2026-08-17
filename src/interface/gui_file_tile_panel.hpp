#pragma once

#include <string>

#include "gui_setup_panel.hpp"
#include "gui_widgets.hpp"

namespace inop {
namespace gui {

enum class TilePanelMode { Load, Overwrite };

struct FileTilePanelResult {
    bool picked = false;
    bool cancelled = false;
    std::string path;

    bool preset_picked = false;
    const PanelState* preset = nullptr;

    bool delete_requested = false;
    std::string delete_path;
};

FileTilePanelResult file_tile_panel_frame(const GuiInput& in, const std::string& title,
                                           float viewport_w, float viewport_h, float& scroll,
                                           TilePanelMode mode, const std::string& current_suite_code);

}
}

