#pragma once

#include <string>
#include <vector>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

constexpr int kMaxRotors = 10;
constexpr int kMaxPlugSlots = 15;
constexpr int kNotchBoxes = 3;

struct RotorRow {
    std::string rotor_name;
    std::string ring_text;
    std::string notch_box[kNotchBoxes];
};

struct PanelState {
    std::string suite_code = "38";
    std::string language_code = "eng";

    int rotor_count = 5;
    RotorRow rotor_rows[kMaxRotors];
    std::string reflector_name;

    std::string plug_left[kMaxPlugSlots];
    std::string plug_right[kMaxPlugSlots];

    bool double_pass = true;
    bool padding = true;
    bool moving_reflector = true;

    std::string master_key_text;
    bool master_key_prefilled = false;
};

struct FieldValidity {
    bool rotor_count_ok = false;
    bool rotor_pick_ok[kMaxRotors] = {};
    bool ring_ok[kMaxRotors] = {};
    bool notch_ok[kMaxRotors] = {};
    bool reflector_ok = false;
    bool plug_slot_ok[kMaxPlugSlots] = {};
    bool plugboard_ok = false;
    bool all_mandatory_ok = false;
    int master_key_needed_len = 0;
};

FieldValidity derive_validity(const PanelState& state);

std::string plug_pair(const PanelState& state, int slot);

std::string notch_text(const RotorRow& row);

void on_suite_changed(PanelState& state);

void apply_toggle_lock(PanelState& state);

bool master_key_valid(const PanelState& state, const FieldValidity& validity);

struct PanelUiState {
    int open_dropdown_id = -1;

    bool show_load_panel = false;
    bool show_save_chooser = false;
    bool show_overwrite_panel = false;
    bool show_create_name_modal = false;
    std::string create_name_text;
    std::string create_name_error;

    bool show_corruption_popup = false;

    bool show_delete_confirm = false;
    std::string delete_confirm_path;

    float file_panel_scroll = 0.0f;
};

class SetupPanel {
public:
    SetupPanel();

    void frame(const GuiInput& in, int width, int height);

    bool wordmark_clicked() const { return wordmark_clicked_; }

private:
    void draw_header(const GuiInput& in, float width);
    void draw_top_row(const GuiInput& in, float width, float y, float h);
    void draw_bottom_row(const GuiInput& in, float width, float y, float h);
    void draw_rotor_count_buttons(const GuiInput& in, Rect area);
    void draw_rotor_grid(const GuiInput& in, Rect area);
    void draw_plugboard_grid(const GuiInput& in, Rect area);
    void draw_master_key(const GuiInput& in, Rect area);
    void draw_file_overlays(const GuiInput& in, float width, float height);

    void on_generate_clicked();
    void on_save_clicked();
    void on_load_tile_picked(const std::string& path);
    void on_overwrite_tile_picked(const std::string& path);
    void on_create_confirmed();
    void on_delete_tile_requested(const std::string& path);
    void on_delete_confirmed();

    void sync_indices_from_state();
    void sync_state_from_indices();

    int suite_idx_ = 0;
    int language_idx_ = 0;
    int rotor_pick_idx_[kMaxRotors];
    int reflector_idx_ = -1;

    std::vector<std::string> suite_labels_, suite_codes_;
    std::vector<std::string> language_labels_, language_codes_;
    std::vector<std::string> rotor_options_;
    std::vector<std::string> reflector_options_;

    std::string master_key_placeholder_;

    PanelState state_;
    PanelUiState ui_;
    FieldValidity validity_;
    bool wordmark_clicked_ = false;
};

}
}

