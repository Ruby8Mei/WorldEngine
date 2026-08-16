// gui_file_tile_panel.hpp — reusable embedded overlay: a scrollable grid
// of plain square tiles, grouped into named sections (Developer settings /
// Enigma settings / INOP settings).
//
// Shared by "Load Settings" and "Save Settings -> Overwrite existing" so
// the grid/scroll/section logic exists exactly once. No art/styling on the
// tiles yet (per spec) beyond a functional filename/preset-name label.
#pragma once

#include <string>

#include "gui_settings_panel.hpp"
#include "gui_widgets.hpp"

namespace inop {
namespace gui {

// Load shows all three sections (Developer settings is load-only, never an
// overwrite/delete target). Overwrite shows exactly one section — whichever
// suite is currently active — so an Enigma session can never overwrite an
// INOP-38 save or vice versa.
enum class TilePanelMode { Load, Overwrite };

struct FileTilePanelResult {
    bool picked = false;
    bool cancelled = false;
    std::string path;

    // Set instead of `picked` when the operator clicks a Developer preset
    // tile — it isn't backed by a file, so the caller applies `*preset`
    // directly to its PanelState rather than calling load_config(). Valid
    // for the caller's immediate use within the same frame only.
    bool preset_picked = false;
    const PanelState* preset = nullptr;

    // Set instead of `picked` when the operator clicks a tile's delete
    // affordance rather than the tile itself — the caller decides whether
    // to confirm before actually removing the file. Never set for a
    // Developer preset tile (those have no delete affordance).
    bool delete_requested = false;
    std::string delete_path;
};

// Draws a titled overlay (scrim + centered box) covering the given
// viewport. `scroll` is owned by the caller (PanelUiState::file_panel_scroll)
// so it persists across frames while the overlay stays open; reset it to
// 0 when the overlay is opened. `current_suite_code` decides which single
// section Overwrite mode shows; ignored in Load mode.
FileTilePanelResult file_tile_panel_frame(const GuiInput& in, const std::string& title,
                                           float viewport_w, float viewport_h, float& scroll,
                                           TilePanelMode mode, const std::string& current_suite_code);

}  // namespace gui
}  // namespace inop
