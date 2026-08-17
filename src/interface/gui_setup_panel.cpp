#include "gui_setup_panel.hpp"

#include <algorithm>
#include <iostream>

#include "generator.hpp"
#include "gui_config_store.hpp"
#include "gui_file_tile_panel.hpp"
#include "inop.hpp"
#include "languages.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"
#include "settings.hpp"

namespace inop {
namespace gui {

namespace {

int index_of(const std::vector<std::string>& v, const std::string& s) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) return static_cast<int>(i);
    return -1;
}

bool wordmark_button(const Rect& r, const GuiInput& in) {
    bool hovered = rect_contains(r, in.mouse_x, in.mouse_y);
    if (hovered) draw_rect(r.x, r.y, r.w, r.h, palette::panel());
    draw_text(Font::Wordmark, r.x + 8, r.y + text_line_height(Font::Wordmark) * 0.75f, "INOP",
              palette::text());
    return hovered && in.mouse_pressed;
}

float dropdown_content_width(const std::vector<std::string>& options, float min_w, float max_w) {
    float w = min_w;
    for (const auto& s : options) {
        float tw = text_width(Font::Body, s) + 34.0f;
        if (tw > w) w = tw;
    }
    return w > max_w ? max_w : w;
}

void log_next_clicked(const PanelState& state) {
    std::cout << "[gui] Next clicked (no-op) -- suite=" << state.suite_code << " rotors=[";
    for (int i = 0; i < state.rotor_count; ++i)
        std::cout << (i ? "," : "") << state.rotor_rows[i].rotor_name;
    std::cout << "] reflector=" << state.reflector_name
              << " plugs=" << [&] {
                     int n = 0;
                     for (int i = 0; i < kMaxPlugSlots; ++i)
                         if (plug_pair(state, i).size() == 2) ++n;
                     return n;
                 }()
              << " master_key_len=" << state.master_key_text.size() << "\n";
}

}

std::string plug_pair(const PanelState& state, int slot) {
    return state.plug_left[slot] + state.plug_right[slot];
}

std::string notch_text(const RotorRow& row) {
    std::string s;
    for (const auto& box : row.notch_box) s += box;
    return s;
}


FieldValidity derive_validity(const PanelState& state) {
    FieldValidity v;
    const Suite& su = suite(state.suite_code);
    Alphabet alpha(su.alphabet);

    v.rotor_count_ok = state.rotor_count >= su.min_rotors && state.rotor_count <= su.max_rotors;

    std::vector<std::string> avail_rotors = available_rotors(su);
    std::vector<std::string> avail_reflectors = available_reflectors(su);

    for (int i = 0; i < kMaxRotors; ++i) {
        bool active = i < state.rotor_count;
        if (!active) {
            v.rotor_pick_ok[i] = true;
            v.ring_ok[i] = true;
            v.notch_ok[i] = true;
            continue;
        }
        const RotorRow& row = state.rotor_rows[i];

        bool pick_ok = !row.rotor_name.empty() &&
                       std::find(avail_rotors.begin(), avail_rotors.end(), row.rotor_name) !=
                           avail_rotors.end();
        if (pick_ok) {
            for (int k = 0; k < state.rotor_count; ++k)
                if (k != i && state.rotor_rows[k].rotor_name == row.rotor_name) { pick_ok = false; break; }
        }
        v.rotor_pick_ok[i] = pick_ok;

        bool ring_ok = false;
        if (!row.ring_text.empty()) {
            try {
                size_t consumed = 0;
                int val = std::stoi(row.ring_text, &consumed);
                ring_ok = consumed == row.ring_text.size() && val >= 1 && val <= alpha.size();
            } catch (...) {
                ring_ok = false;
            }
        }
        v.ring_ok[i] = ring_ok;

        bool notch_ok;
        if (su.notches_are_fixed) {
            notch_ok = true;
        } else {
            notch_ok = !row.notch_box[0].empty() || !row.notch_box[1].empty() ||
                       !row.notch_box[2].empty();
            if (notch_ok) {
                for (int b = 0; b < kNotchBoxes && b < su.max_notches; ++b) {
                    if (row.notch_box[b].empty()) continue;
                    if (!alpha.contains(row.notch_box[b][0])) { notch_ok = false; break; }
                }
            }
        }
        v.notch_ok[i] = notch_ok;
    }

    if (!su.notches_are_fixed) {
        int notch_count[256] = {};
        for (int i = 0; i < state.rotor_count; ++i)
            for (const auto& box : state.rotor_rows[i].notch_box)
                if (!box.empty()) notch_count[static_cast<unsigned char>(box[0])]++;
        for (int i = 0; i < state.rotor_count; ++i) {
            for (const auto& box : state.rotor_rows[i].notch_box) {
                if (!box.empty() && notch_count[static_cast<unsigned char>(box[0])] > 1) {
                    v.notch_ok[i] = false;
                    break;
                }
            }
        }
    }

    v.reflector_ok = !state.reflector_name.empty() &&
                     std::find(avail_reflectors.begin(), avail_reflectors.end(), state.reflector_name) !=
                         avail_reflectors.end();

    std::vector<std::string> active_pairs;
    bool used[256] = {};
    for (int i = 0; i < kMaxPlugSlots; ++i) {
        bool active = i < su.max_plug_pairs;
        std::string p = plug_pair(state, i);
        if (!active || p.empty()) {
            v.plug_slot_ok[i] = true;
            continue;
        }
        bool ok = p.size() == 2 && p[0] != p[1] && alpha.contains(p[0]) && alpha.contains(p[1]) &&
                  !used[static_cast<unsigned char>(p[0])] && !used[static_cast<unsigned char>(p[1])];
        if (ok) {
            used[static_cast<unsigned char>(p[0])] = true;
            used[static_cast<unsigned char>(p[1])] = true;
        }
        v.plug_slot_ok[i] = ok;
        active_pairs.push_back(p);
    }
    try {
        Plugboard probe(active_pairs, alpha);
        v.plugboard_ok = true;
    } catch (const std::exception&) {
        v.plugboard_ok = false;
    }

    bool rotor_rows_ok = true;
    for (int i = 0; i < state.rotor_count; ++i)
        if (!v.rotor_pick_ok[i] || !v.ring_ok[i] || !v.notch_ok[i]) rotor_rows_ok = false;

    v.all_mandatory_ok = v.rotor_count_ok && rotor_rows_ok && v.reflector_ok && v.plugboard_ok;
    v.master_key_needed_len = state.rotor_count + (su.historic_lock ? 0 : 1);
    return v;
}

void apply_toggle_lock(PanelState& state) {
    const Suite& su = suite(state.suite_code);
    PipelineConfig cfg;
    cfg.double_pass = state.double_pass;
    cfg.padding = state.padding;
    cfg.moving_reflector = state.moving_reflector;
    apply_suite_lock(cfg, su.historic_lock, su.block);
    state.double_pass = cfg.double_pass;
    state.padding = cfg.padding;
    state.moving_reflector = cfg.moving_reflector;
}

void on_suite_changed(PanelState& state) {
    std::string suite_code = state.suite_code;
    std::string language_code = state.language_code;
    state = PanelState{};
    state.suite_code = suite_code;
    state.language_code = language_code;

    const Suite& su = suite(state.suite_code);
    if (state.rotor_count < su.min_rotors) state.rotor_count = su.min_rotors;
    if (state.rotor_count > su.max_rotors) state.rotor_count = su.max_rotors;

    if (su.historic_lock) apply_toggle_lock(state);
}

bool master_key_valid(const PanelState& state, const FieldValidity& validity) {
    const Suite& su = suite(state.suite_code);
    Alphabet alpha(su.alphabet);
    if (static_cast<int>(state.master_key_text.size()) != validity.master_key_needed_len) return false;
    for (char c : state.master_key_text)
        if (!alpha.contains(c)) return false;
    return true;
}


SetupPanel::SetupPanel() {
    for (int i = 0; i < kMaxRotors; ++i) rotor_pick_idx_[i] = -1;
}

void SetupPanel::sync_indices_from_state() {
    const Suite& su = suite(state_.suite_code);

    suite_codes_ = {"38", "26"};
    suite_labels_ = {"INOP-38", "Enigma"};
    suite_idx_ = index_of(suite_codes_, state_.suite_code);
    if (suite_idx_ < 0) suite_idx_ = 0;

    language_codes_.clear();
    language_labels_.clear();
    for (const auto& li : supported_languages()) {
        language_codes_.push_back(li.code);
        language_labels_.push_back(li.name);
    }
    language_idx_ = index_of(language_codes_, state_.language_code);
    if (language_idx_ < 0) language_idx_ = 0;

    rotor_options_ = available_rotors(su);
    for (int i = 0; i < kMaxRotors; ++i)
        rotor_pick_idx_[i] = index_of(rotor_options_, state_.rotor_rows[i].rotor_name);

    reflector_options_ = available_reflectors(su);
    reflector_idx_ = index_of(reflector_options_, state_.reflector_name);
}

void SetupPanel::sync_state_from_indices() {
    if (suite_idx_ >= 0 && suite_idx_ < static_cast<int>(suite_codes_.size())) {
        const std::string& new_suite = suite_codes_[static_cast<size_t>(suite_idx_)];
        if (new_suite != state_.suite_code) {
            state_.suite_code = new_suite;
            on_suite_changed(state_);
            return;
        }
    }

    if (language_idx_ >= 0 && language_idx_ < static_cast<int>(language_codes_.size()))
        state_.language_code = language_codes_[static_cast<size_t>(language_idx_)];

    for (int i = 0; i < kMaxRotors; ++i) {
        if (rotor_pick_idx_[i] >= 0 && rotor_pick_idx_[i] < static_cast<int>(rotor_options_.size()))
            state_.rotor_rows[i].rotor_name = rotor_options_[static_cast<size_t>(rotor_pick_idx_[i])];
        else
            state_.rotor_rows[i].rotor_name.clear();
    }

    if (reflector_idx_ >= 0 && reflector_idx_ < static_cast<int>(reflector_options_.size()))
        state_.reflector_name = reflector_options_[static_cast<size_t>(reflector_idx_)];
    else
        state_.reflector_name.clear();
}

void SetupPanel::frame(const GuiInput& real_in, int width, int height) {
    begin_widget_frame();
    wordmark_clicked_ = false;

    sync_indices_from_state();
    validity_ = derive_validity(state_);

    bool unlocked = validity_.all_mandatory_ok;
    if (unlocked && !state_.master_key_prefilled) {
        const Suite& su = suite(state_.suite_code);
        master_key_placeholder_ =
            secure_string(su.alphabet, static_cast<size_t>(validity_.master_key_needed_len));
        state_.master_key_prefilled = true;
    } else if (!unlocked) {
        state_.master_key_prefilled = false;
        state_.master_key_text.clear();
        master_key_placeholder_.clear();
    }

    bool any_file_modal = ui_.show_load_panel || ui_.show_save_chooser || ui_.show_overwrite_panel ||
                           ui_.show_create_name_modal || ui_.show_corruption_popup;
    bool modal_active = any_file_modal || ui_.open_dropdown_id != -1;

    GuiInput in = real_in;
    if (modal_active) {
        in.mouse_pressed = false;
        in.mouse_released = false;
        in.typed.clear();
        in.key_backspace = in.key_enter = in.key_escape = false;
        in.scroll_y = 0;
    }

    float w = static_cast<float>(width), h = static_cast<float>(height);
    clear(palette::background());

    const float header_h = 110.0f;
    const float top_h = 190.0f;

    draw_header(in, w);

    float top_y = header_h;
    draw_top_row(in, w, top_y, top_h);

    float bottom_y = top_y + top_h;
    float bottom_h = h - bottom_y;
    if (bottom_h > 10.0f) draw_bottom_row(in, w, bottom_y, bottom_h);

    if (any_file_modal) draw_file_overlays(real_in, w, h);
    draw_open_dropdown_popup(real_in, ui_.open_dropdown_id);

    sync_state_from_indices();

    end_widget_frame(real_in);
}

void SetupPanel::draw_header(const GuiInput& in, float width) {
    const float pad = 6.0f;
    float word_tw = text_width(Font::Wordmark, "INOP");
    float word_th = text_line_height(Font::Wordmark);
    Rect wordmark_r{16, pad, word_tw + 24.0f, word_th + 12.0f};
    if (wordmark_button(wordmark_r, in)) wordmark_clicked_ = true;

    bool next_enabled = validity_.all_mandatory_ok && master_key_valid(state_, validity_);
    float btn_w = 150, btn_h = 30, gap = 6;
    Rect next_r{width - btn_w - 16, pad, btn_w, btn_h};
    if (button(next_r, "Next", in, next_enabled, next_enabled)) log_next_clicked(state_);

    Rect generate_r{width - btn_w - 16, pad + (btn_h + gap), btn_w, btn_h};
    if (button(generate_r, "Generate Setup", in, true)) on_generate_clicked();

    Rect save_r{width - btn_w - 16, pad + 2 * (btn_h + gap), btn_w, btn_h};
    if (button(save_r, "Save Setup", in, next_enabled)) on_save_clicked();

    Rect load_r{width - btn_w - 16, pad + 3 * (btn_h + gap), btn_w, btn_h};
    if (button(load_r, "Load Setup", in, true)) {
        ui_.show_load_panel = true;
        ui_.file_panel_scroll = 0;
    }

    const Suite& su = suite(state_.suite_code);
    float gap_x0 = wordmark_r.x + wordmark_r.w + 20.0f;
    float gap_x1 = next_r.x - 20.0f;
    float alpha_tw = text_width(Font::BodyLarge, su.alphabet);
    float strip_x = gap_x0 + std::max(0.0f, (gap_x1 - gap_x0 - alpha_tw) * 0.5f);
    label(Rect{strip_x, pad, alpha_tw, word_th + 12.0f}, su.alphabet, false, Font::BodyLarge);

    if (su.alphabet.find('#') != std::string::npos) {
        std::string hint = "# = space";
        float hint_tw = text_width(Font::Body, hint);
        label(Rect{strip_x + (alpha_tw - hint_tw) * 0.5f, pad + word_th - 6.0f, hint_tw, 16.0f}, hint,
              true);
    }
}

void SetupPanel::draw_top_row(const GuiInput& in, float width, float y, float h) {
    const Suite& su = suite(state_.suite_code);
    const float margin = 16;
    float col_w = (width - 4 * margin) / 3.0f;

    Rect left{margin, y, col_w, h};
    Rect center{margin * 2 + col_w, y, col_w, h};
    Rect right{margin * 3 + 2 * col_w, y, col_w, h};

    label(Rect{left.x, left.y, left.w, 18}, "machine");
    float machine_w = dropdown_content_width(suite_labels_, 90.0f, 160.0f);
    Rect machine_r{left.x, left.y + 20, machine_w, 30};
    dropdown(machine_r, suite_labels_, suite_idx_, 0, ui_.open_dropdown_id, in, true);

    if (!su.historic_lock) {
        label(Rect{left.x, left.y + 58, left.w, 18}, "language");
        float lang_w = dropdown_content_width(language_labels_, 120.0f, 220.0f);
        Rect lang_r{left.x, left.y + 78, lang_w, 30};
        dropdown(lang_r, language_labels_, language_idx_, 1, ui_.open_dropdown_id, in, true);
    }

    draw_master_key(in, center);

    if (!su.historic_lock) {
        label(Rect{right.x, right.y, right.w, 18}, "hardening (INOP-38 only)");
        float toggle_h = 26, toggle_gap = 8;
        Rect t1{right.x, right.y + 24, right.w, toggle_h};
        Rect t2{right.x, right.y + 24 + (toggle_h + toggle_gap), right.w, toggle_h};
        Rect t3{right.x, right.y + 24 + 2 * (toggle_h + toggle_gap), right.w, toggle_h};
        toggle(t1, state_.double_pass, "double pass", in, true);
        toggle(t2, state_.moving_reflector, "rotating reflector", in, true);
        toggle(t3, state_.padding, "padding / traffic concealment", in, true);
    }
}

void SetupPanel::draw_master_key(const GuiInput& in, Rect area) {
    const Suite& su = suite(state_.suite_code);
    bool unlocked = validity_.all_mandatory_ok;
    label(Rect{area.x, area.y, area.w, 18}, "master key", !unlocked);

    int needed = validity_.master_key_needed_len;
    float field_w = text_width(Font::Body, std::string(static_cast<size_t>(needed) + 1, 'w')) + 24.0f;
    if (field_w > area.w) field_w = area.w;
    if (field_w < 90.0f) field_w = 90.0f;

    Rect field_r{area.x, area.y + 20, field_w, 30};
    bool invalid = unlocked && !master_key_valid(state_, validity_);
    CaseFold fold = Alphabet(su.alphabet).uses_uppercase() ? CaseFold::ToUpper : CaseFold::ToLower;
    text_field(field_r, state_.master_key_text, in, su.alphabet,
               static_cast<size_t>(validity_.master_key_needed_len), unlocked, invalid, fold,
               master_key_placeholder_);
    std::string hint = unlocked ? ("length " + std::to_string(validity_.master_key_needed_len))
                                 : "locked - fill every other field first";
    label(Rect{area.x, area.y + 54, area.w, 16}, hint, true);
}

void SetupPanel::draw_bottom_row(const GuiInput& in, float width, float y, float h) {
    const Suite& su = suite(state_.suite_code);
    const float margin = 16;
    float left_w = width * 0.58f - margin * 1.5f;
    float right_w = width - left_w - margin * 3;

    Rect left{margin, y, left_w, h};
    Rect right{margin * 2 + left_w, y, right_w, h};

    float grid_y = left.y;
    if (su.min_rotors != su.max_rotors) {
        label(Rect{left.x, left.y, left.w, 18}, "rotor count (" + std::to_string(su.min_rotors) + "-" +
                                                     std::to_string(su.max_rotors) + ")");
        Rect rc_area{left.x, left.y + 20, left.w, 26};
        draw_rotor_count_buttons(in, rc_area);
        grid_y = left.y + 56;
    }

    int visible_rows = su.historic_lock ? su.max_rotors : kMaxRotors;
    Rect grid_area{left.x, grid_y, left.w, visible_rows * 30.0f + 20.0f};
    draw_rotor_grid(in, grid_area);

    float reflector_y = grid_area.y + grid_area.h + 12;
    label(Rect{left.x, reflector_y, left.w, 16}, "reflector");
    float reflector_w = dropdown_content_width(rotor_options_, 120.0f, 200.0f);
    Rect reflector_r{left.x, reflector_y + 20, reflector_w, 26};
    dropdown(reflector_r, reflector_options_, reflector_idx_, 30, ui_.open_dropdown_id, in, true,
             !validity_.reflector_ok);

    draw_plugboard_grid(in, right);
}

void SetupPanel::draw_rotor_count_buttons(const GuiInput& in, Rect area) {
    const Suite& su = suite(state_.suite_code);
    int lo = su.min_rotors, hi = su.max_rotors;
    int count = hi - lo + 1;
    bool enabled = lo != hi;
    float gap = 6.0f;
    float btn_w = (area.w - gap * (count - 1)) / static_cast<float>(count);

    for (int n = lo; n <= hi; ++n) {
        Rect r{area.x + (n - lo) * (btn_w + gap), area.y, btn_w, area.h};
        bool selected = state_.rotor_count == n;
        if (button(r, std::to_string(n), in, enabled, selected)) state_.rotor_count = n;
    }
}

void SetupPanel::draw_rotor_grid(const GuiInput& in, Rect area) {
    const Suite& su = suite(state_.suite_code);
    const float header_h = 20.0f;

    const float ring_w = 50.0f;
    const float notch_box_w = 26.0f, notch_gap = 4.0f;
    const float notch_total_w = notch_box_w * kNotchBoxes + notch_gap * (kNotchBoxes - 1);
    float pick_w = dropdown_content_width(rotor_options_, 120.0f, 200.0f);
    float ring_x = area.x + pick_w + 8.0f;
    float notch_x = ring_x + ring_w + 16.0f;

    label(Rect{area.x, area.y, pick_w, header_h}, "rotor selection", true);
    label(Rect{ring_x, area.y, ring_w, header_h}, "ring", true);
    label(Rect{notch_x, area.y, notch_total_w, header_h}, "notch", true);

    int visible_rows = su.historic_lock ? su.max_rotors : kMaxRotors;
    float grid_top = area.y + header_h;
    float row_h = (area.h - header_h) / static_cast<float>(visible_rows);

    for (int i = 0; i < visible_rows; ++i) {
        bool active = i < state_.rotor_count;
        float ry = grid_top + i * row_h;
        Rect pick_r{area.x, ry, pick_w, row_h - 4};
        Rect ring_r{ring_x, ry, ring_w, row_h - 4};

        dropdown(pick_r, rotor_options_, rotor_pick_idx_[i], 10 + i, ui_.open_dropdown_id, in, active,
                 active && !validity_.rotor_pick_ok[i]);
        numeric_field(ring_r, state_.rotor_rows[i].ring_text, in, 2, active,
                      active && !validity_.ring_ok[i], true);

        if (su.notches_are_fixed) {
            Rect notch_r{notch_x, ry, notch_total_w, row_h - 4};
            draw_rect(notch_r.x, notch_r.y, notch_r.w, notch_r.h, palette::disabled_bg());
            draw_rect_outline(notch_r.x, notch_r.y, notch_r.w, notch_r.h, palette::border());
            std::string display = "-";
            if (active && !state_.rotor_rows[i].rotor_name.empty()) {
                Alphabet alpha(su.alphabet);
                std::string ns = make_rotor(state_.rotor_rows[i].rotor_name, alpha).notch_str(alpha);
                if (!ns.empty()) display = ns;
            }
            label(Rect{notch_r.x + 6, notch_r.y, notch_r.w - 12, notch_r.h}, display, true);
        } else {
            bool row_invalid = active && !validity_.notch_ok[i];
            CaseFold fold = Alphabet(su.alphabet).uses_uppercase() ? CaseFold::ToUpper : CaseFold::ToLower;
            for (int b = 0; b < kNotchBoxes; ++b) {
                Rect box_r{notch_x + b * (notch_box_w + notch_gap), ry, notch_box_w, row_h - 4};
                text_field(box_r, state_.rotor_rows[i].notch_box[b], in, su.alphabet, 1, active,
                           row_invalid, fold, "", true);
            }
        }
    }
}

void SetupPanel::draw_plugboard_grid(const GuiInput& in, Rect area) {
    const Suite& su = suite(state_.suite_code);
    float grid_y = area.y + 24;
    const int cols = 3, rows = 5;
    float cell_w = area.w / cols;
    float cell_h = (area.h - 24) / rows;

    const float box_size = 28.0f;
    const float dash_w = 14.0f;
    const float pair_w = box_size * 2 + dash_w;

    float first_container_x = area.x + (cell_w - pair_w) * 0.5f;
    label(Rect{first_container_x, area.y, area.w, 18},
          "plugboard (up to " + std::to_string(su.max_plug_pairs) + " pairs)");

    CaseFold fold = Alphabet(su.alphabet).uses_uppercase() ? CaseFold::ToUpper : CaseFold::ToLower;
    for (int i = 0; i < su.max_plug_pairs && i < kMaxPlugSlots; ++i) {
        int col = i / rows;
        int row = i % rows;
        float cx = area.x + col * cell_w + (cell_w - pair_w) * 0.5f;
        float cy = grid_y + row * cell_h + (cell_h - box_size) * 0.5f;
        bool invalid = !validity_.plug_slot_ok[i] && !plug_pair(state_, i).empty();

        Rect left_r{cx, cy, box_size, box_size};
        Rect dash_r{cx + box_size, cy, dash_w, box_size};
        Rect right_r{cx + box_size + dash_w, cy, box_size, box_size};

        text_field(left_r, state_.plug_left[i], in, su.alphabet, 1, true, invalid,
                   fold, "", true);
        float dash_tw = text_width(Font::Body, "-");
        label(Rect{dash_r.x + (dash_r.w - dash_tw) * 0.5f, dash_r.y, dash_r.w, dash_r.h}, "-");
        text_field(right_r, state_.plug_right[i], in, su.alphabet, 1, true, invalid,
                   fold, "", true);
    }
}

void SetupPanel::draw_file_overlays(const GuiInput& in, float w, float h) {
    GuiInput under_in = in;
    if (ui_.show_corruption_popup || ui_.show_delete_confirm) {
        under_in.mouse_pressed = false;
        under_in.mouse_released = false;
        under_in.typed.clear();
        under_in.scroll_y = 0;
    }

    if (ui_.show_load_panel) {
        FileTilePanelResult r = file_tile_panel_frame(under_in, "Load Setup", w, h,
                                                       ui_.file_panel_scroll, TilePanelMode::Load,
                                                       state_.suite_code);
        if (r.preset_picked) {
            state_ = *r.preset;
            sync_indices_from_state();
            ui_.show_load_panel = false;
        } else if (r.delete_requested)
            on_delete_tile_requested(r.delete_path);
        else if (r.cancelled)
            ui_.show_load_panel = false;
        else if (r.picked)
            on_load_tile_picked(r.path);
    } else if (ui_.show_overwrite_panel) {
        FileTilePanelResult r =
            file_tile_panel_frame(under_in, "Overwrite Setup", w, h, ui_.file_panel_scroll,
                                   TilePanelMode::Overwrite, state_.suite_code);
        if (r.delete_requested)
            on_delete_tile_requested(r.delete_path);
        else if (r.cancelled)
            ui_.show_overwrite_panel = false;
        else if (r.picked)
            on_overwrite_tile_picked(r.path);
    } else if (ui_.show_save_chooser) {
        draw_rect(0, 0, w, h, rgba(0, 0, 0, 0.55f));
        Rect box{w / 2 - 160, h / 2 - 70, 320, 140};
        draw_rect(box.x, box.y, box.w, box.h, palette::panel());
        draw_rect_outline(box.x, box.y, box.w, box.h, palette::accent(), 2.0f);
        label(Rect{box.x + 16, box.y + 10, box.w - 32, 24}, "Save Setup");
        Rect overwrite_btn{box.x + 20, box.y + 46, box.w - 40, 32};
        Rect create_btn{box.x + 20, box.y + 86, box.w - 40, 32};
        if (button(overwrite_btn, "Overwrite existing", under_in, true)) {
            ui_.show_save_chooser = false;
            ui_.show_overwrite_panel = true;
            ui_.file_panel_scroll = 0;
        }
        if (button(create_btn, "Create new", under_in, true)) {
            ui_.show_save_chooser = false;
            ui_.show_create_name_modal = true;
            std::string suggested = suggest_filename(state_);
            const std::string ext = ".json";
            if (suggested.size() > ext.size() &&
                suggested.compare(suggested.size() - ext.size(), ext.size(), ext) == 0)
                suggested.resize(suggested.size() - ext.size());
            ui_.create_name_text = suggested;
            ui_.create_name_error = ui_.create_name_text.empty()
                                         ? "no auto-numbered slot left (max " +
                                               std::to_string(kMaxSavedPerSuite) +
                                               " per machine) — type a custom name"
                                         : "";
        }
        if (under_in.mouse_pressed && !rect_contains(box, under_in.mouse_x, under_in.mouse_y))
            ui_.show_save_chooser = false;
    } else if (ui_.show_create_name_modal) {
        draw_rect(0, 0, w, h, rgba(0, 0, 0, 0.55f));
        Rect box{w / 2 - 200, h / 2 - 90, 400, 180};
        draw_rect(box.x, box.y, box.w, box.h, palette::panel());
        draw_rect_outline(box.x, box.y, box.w, box.h, palette::accent(), 2.0f);
        label(Rect{box.x + 16, box.y + 10, box.w - 32, 24}, "new setup file name");
        float ext_w = text_width(Font::Body, ".json") + 16.0f;
        Rect field_r{box.x + 16, box.y + 44, box.w - 32 - ext_w - 4, 32};
        text_field(field_r, ui_.create_name_text, under_in,
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ", 64, true,
                   !ui_.create_name_error.empty());
        Rect ext_r{field_r.x + field_r.w + 4, field_r.y, ext_w, 32};
        draw_rect(ext_r.x, ext_r.y, ext_r.w, ext_r.h, palette::disabled_bg());
        draw_rect_outline(ext_r.x, ext_r.y, ext_r.w, ext_r.h, palette::border());
        label(Rect{ext_r.x + 6, ext_r.y, ext_r.w - 12, ext_r.h}, ".json", true);
        if (!ui_.create_name_error.empty())
            label(Rect{box.x + 16, box.y + 80, box.w - 32, 20}, ui_.create_name_error);
        Rect confirm_r{box.x + 16, box.y + box.h - 44, 120, 32};
        Rect cancel_r{box.x + box.w - 136, box.y + box.h - 44, 120, 32};
        if (button(confirm_r, "Confirm", under_in, true, true)) on_create_confirmed();
        if (button(cancel_r, "Cancel", under_in, true)) ui_.show_create_name_modal = false;
    }

    if (ui_.show_corruption_popup) {
        Rect box{w / 2 - 220, h / 2 - 60, 440, 120};
        draw_rect(box.x, box.y, box.w, box.h, palette::error_bg());
        draw_rect_outline(box.x, box.y, box.w, box.h, palette::error_text(), 2.0f);
        label(Rect{box.x + 16, box.y + 16, box.w - 32, 40},
              "Corruption error, please pick another setting file");
        Rect ok_r{box.x + box.w / 2 - 50, box.y + box.h - 44, 100, 30};
        if (button(ok_r, "OK", in, true)) ui_.show_corruption_popup = false;
    }

    if (ui_.show_delete_confirm) {
        std::string filename = ui_.delete_confirm_path;
        size_t slash = filename.find_last_of("\\/");
        if (slash != std::string::npos) filename = filename.substr(slash + 1);

        Rect box{w / 2 - 220, h / 2 - 60, 440, 120};
        draw_rect(box.x, box.y, box.w, box.h, palette::error_bg());
        draw_rect_outline(box.x, box.y, box.w, box.h, palette::error_text(), 2.0f);
        label(Rect{box.x + 16, box.y + 16, box.w - 32, 40}, "Delete " + filename + "? This cannot be undone.");
        Rect confirm_r{box.x + box.w / 2 - 108, box.y + box.h - 44, 100, 30};
        Rect cancel_r{box.x + box.w / 2 + 8, box.y + box.h - 44, 100, 30};
        if (button(confirm_r, "Delete", in, true)) on_delete_confirmed();
        if (button(cancel_r, "Cancel", in, true)) ui_.show_delete_confirm = false;
    }
}

void SetupPanel::on_generate_clicked() {
    const Suite& su = suite(state_.suite_code);
    try {
        int rotor_count = su.min_rotors + static_cast<int>(secure_below(
                               static_cast<uint32_t>(su.max_rotors - su.min_rotors + 1)));
        int plug_pairs = static_cast<int>(secure_below(static_cast<uint32_t>(su.max_plug_pairs + 1)));

        GeneratedSettings g = random_settings(su, rotor_count, plug_pairs, 1);
        if (!su.notches_are_fixed)
            g.notches = random_variable_notches(Alphabet(su.alphabet), rotor_count, su.max_notches);
        state_.rotor_count = rotor_count;

        for (int i = 0; i < kMaxRotors; ++i) {
            if (i >= state_.rotor_count) {
                state_.rotor_rows[i] = RotorRow{};
                continue;
            }
            state_.rotor_rows[i].rotor_name = g.rotors[static_cast<size_t>(i)];
            state_.rotor_rows[i].ring_text = std::to_string(g.rings[static_cast<size_t>(i)]);
            const std::string& n = g.notches[static_cast<size_t>(i)];
            for (int b = 0; b < kNotchBoxes; ++b)
                state_.rotor_rows[i].notch_box[b] =
                    b < static_cast<int>(n.size()) ? std::string(1, n[static_cast<size_t>(b)]) : "";
        }
        state_.reflector_name = g.reflector;

        for (int i = 0; i < kMaxPlugSlots; ++i) {
            state_.plug_left[i].clear();
            state_.plug_right[i].clear();
        }
        for (size_t i = 0; i < g.plugs.size() && i < static_cast<size_t>(kMaxPlugSlots); ++i) {
            state_.plug_left[i] = std::string(1, g.plugs[i][0]);
            state_.plug_right[i] = std::string(1, g.plugs[i][1]);
        }

        FieldValidity v = derive_validity(state_);
        state_.master_key_text = secure_string(su.alphabet, static_cast<size_t>(v.master_key_needed_len));
        state_.master_key_prefilled = true;

        sync_indices_from_state();
    } catch (const std::exception& e) {
        std::cout << "[gui] generate failed: " << e.what() << "\n";
    }
}

void SetupPanel::on_save_clicked() { ui_.show_save_chooser = true; }

void SetupPanel::on_load_tile_picked(const std::string& path) {
    PanelState loaded;
    std::string err;
    if (load_config(path, loaded, &err)) {
        state_ = loaded;
        sync_indices_from_state();
        ui_.show_load_panel = false;
    } else {
        ui_.show_corruption_popup = true;
    }
}

void SetupPanel::on_overwrite_tile_picked(const std::string& path) {
    std::string filename = path;
    size_t slash = filename.find_last_of("\\/");
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    std::string err;
    if (!save_config(state_, filename, &err))
        std::cout << "[gui] overwrite failed: " << err << "\n";
    ui_.show_overwrite_panel = false;
}

void SetupPanel::on_create_confirmed() {
    std::string name = ui_.create_name_text;
    size_t a = name.find_first_not_of(' ');
    size_t b = name.find_last_not_of(' ');
    name = (a == std::string::npos) ? "" : name.substr(a, b - a + 1);

    if (name.empty()) {
        ui_.create_name_error = "name cannot be empty";
        return;
    }
    name += ".json";
    if (config_exists(name)) {
        ui_.create_name_error = "a file with that name already exists";
        return;
    }

    std::string err;
    if (!save_config(state_, name, &err)) {
        ui_.create_name_error = err.empty() ? "save failed" : err;
        return;
    }
    ui_.show_create_name_modal = false;
    ui_.create_name_error.clear();
}

void SetupPanel::on_delete_tile_requested(const std::string& path) {
    ui_.delete_confirm_path = path;
    ui_.show_delete_confirm = true;
}

void SetupPanel::on_delete_confirmed() {
    std::string err;
    if (!delete_config(ui_.delete_confirm_path, &err)) std::cout << "[gui] delete failed: " << err << "\n";
    ui_.show_delete_confirm = false;
}

}
}

