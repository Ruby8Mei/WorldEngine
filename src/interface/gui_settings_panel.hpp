// gui_settings_panel.hpp — the settings-panel screen: state, live
// validation, layout, and the Save/Load Settings flows.
//
// This is the only GUI file that talks to registry.hpp/pipeline.hpp/
// settings.hpp/languages.hpp/rng.hpp/generator.hpp — gui_render.hpp/
// gui_widgets.hpp know nothing about the cipher domain, and this file knows
// nothing about GLFW callbacks (gui.cpp owns those).
#pragma once

#include <string>
#include <vector>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

constexpr int kMaxRotors = 10;
constexpr int kMaxPlugSlots = 15;
constexpr int kNotchBoxes = 3;  // matches INOP-38's max_notches, the largest of any suite

struct RotorRow {
    std::string rotor_name;
    std::string ring_text;
    // Three independent single-character boxes rather than one field — at
    // least one must be filled (any single one counts, not specifically
    // box[0]); the rest are optional extra notch symbols. See notch_text()
    // below for the combined string validation/storage actually deals in.
    std::string notch_box[kNotchBoxes];
};

// Pure data snapshot of everything the panel can produce — deliberately
// shaped like inop::Settings + the three PipelineConfig toggles, since
// converting one to the other on "Next" (or on save/load) is a handful of
// field copies, not a redesign.
struct PanelState {
    std::string suite_code = "38";
    std::string language_code = "eng";

    int rotor_count = 5;
    RotorRow rotor_rows[kMaxRotors];
    std::string reflector_name;

    // Each pair is edited as two independent single-character boxes joined
    // by a dash, not one two-character field — see plug_pair() below for
    // the combined string validation/storage actually deals in.
    std::string plug_left[kMaxPlugSlots];   // "" or exactly 1 symbol
    std::string plug_right[kMaxPlugSlots];  // "" or exactly 1 symbol

    bool double_pass = true;
    bool padding = true;
    bool moving_reflector = true;

    std::string master_key_text;
    bool master_key_prefilled = false;  // guards secure_string() re-draw to once per unlock
};

struct FieldValidity {
    bool rotor_count_ok = false;
    bool rotor_pick_ok[kMaxRotors] = {};
    bool ring_ok[kMaxRotors] = {};
    bool notch_ok[kMaxRotors] = {};
    bool reflector_ok = false;
    bool plug_slot_ok[kMaxPlugSlots] = {};  // per-slot, live highlighting only
    bool plugboard_ok = false;              // authoritative gate (real Plugboard ctor)
    bool all_mandatory_ok = false;
    int master_key_needed_len = 0;
};

// Re-derives every validity flag from scratch against the suite named by
// state.suite_code. Cheap enough to call once per frame — mirrors the
// CLI's own validation rules (main.cpp's collect_settings()) rather than
// reinventing them; see DESIGN.md and the plan for the specific rules.
FieldValidity derive_validity(const PanelState& state);

// The combined (0, 1, or 2 character) pair for plugboard slot `slot` —
// plug_left[slot] + plug_right[slot]. Used by derive_validity, the
// gui_config_store JSON schema (which still stores/loads one 2-char
// string per filled pair, unaware the panel edits each half separately),
// and the panel's own drawing code, so the combine logic lives in exactly
// one place.
std::string plug_pair(const PanelState& state, int slot);

// The combined notch string (0-3 alphabet symbols) for one rotor row —
// concatenation of row.notch_box[0..2] in order, so a gap just gets
// skipped (typing box 0 and box 2 but leaving box 1 empty yields the same
// 2-symbol string as filling box 0 and box 1). Used by derive_validity and
// the gui_config_store JSON schema (still one "notches" string per rotor).
std::string notch_text(const RotorRow& row);

// Full reset of PanelState (keeping only suite_code/language_code), not a
// selective clear — avoids leftover fields that were only valid under the
// old suite. Call whenever suite_code changes.
void on_suite_changed(PanelState& state);

// Runs the existing apply_suite_lock() against the panel's three toggles
// for historic-lock suites (Legacy/Enigma), writing the forced values back.
void apply_toggle_lock(PanelState& state);

// True once the master key field itself is exactly validity's required
// length and every character is a member of the suite's alphabet. Combined
// with validity.all_mandatory_ok this is "Next enabled" / "this is a
// fully usable configuration" (used both by the panel and by
// gui_config_store's load-time corruption check).
bool master_key_valid(const PanelState& state, const FieldValidity& validity);

// Transient, non-persisted UI state: which overlay/modal is open, and any
// in-progress modal input. Kept apart from PanelState (the serializable
// data), the same separation the codebase already draws between Settings
// and the interactive CLI flow around it.
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

class SettingsPanel {
public:
    SettingsPanel();

    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

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

    // Dropdowns hand a clicked popup item's write back through a pointer
    // captured at draw time (see gui_widgets::draw_open_dropdown_popup,
    // called once at the very end of frame()) — so the `selected` index
    // gui_widgets writes into must outlive the whole frame, not just the
    // row that opened it. These members are synced from PanelState at the
    // start of frame() and back into it at the end.
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

    // Shown as greyed-out placeholder text in the master key box, never
    // written into state_.master_key_text itself — like a web form's
    // placeholder attribute, it must vanish the instant the operator types
    // their own key, not sit there as real content they have to backspace
    // through first. Regenerated once per unlock, guarded the same way
    // the field itself used to be (state_.master_key_prefilled).
    std::string master_key_placeholder_;

    PanelState state_;
    PanelUiState ui_;
    FieldValidity validity_;
};

}  // namespace gui
}  // namespace inop
