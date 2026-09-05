#include "gui_maintenance_panel.hpp"

#include "gui_form.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "generator.hpp"
#include "registry.hpp"
#include "rng.hpp"

namespace inop {
namespace gui {

namespace {

// Row metrics, headings and the screen header come from gui_form.hpp,
// shared with the settings screen. This file used to hold its own
// byte-identical copies, and they drifted: the settings column learned to
// grow with the window while this one stayed at a fixed 840.
constexpr float kWideBtnW = 320.0f;
constexpr float kPathW = 396.0f;
constexpr float kMargin = kFormMargin;
constexpr float kBtnW = kFormBtnW;
constexpr float kBtnH = kFormBtnH;
constexpr float kRowH = kFormRowH;
constexpr float kRowGap = kFormRowGap;
constexpr float kSectionGap = kFormSectionGap;
constexpr float kHeadingH = kFormHeadingH;
constexpr float kLabelW = 200.0f;
constexpr float kCtrlW = 180.0f;
constexpr float kGap = 16.0f;

// Filled at the top of frame() from the real window width.
FormMetrics g_form{kLabelW, kCtrlW, kGap, kFormColWMin};
// Two label+control pairs fit across one row; this is the step from the
// first pair to the second, which lands the second control at 832 of the
// 840 the column has.
constexpr float kPairDX = kLabelW + kGap + kCtrlW + 40.0f;

// Stable within one frame and unique across the panel, which is all
// dropdown() asks of them.
constexpr int kIdRotorMode = 1;
constexpr int kIdReflectorMode = 2;
constexpr int kIdSheetSuite = 3;
constexpr int kIdSheetCountMode = 4;

// Wheel names are whitespace-delimited tokens in the wheel file, and every
// factory name is uppercase letters, so a generated prefix is held to the
// same shape rather than to whatever the field would otherwise accept.
const char* const kPrefixChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char* const kPathChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";

const std::vector<std::string>& mode_options() {
    static const std::vector<std::string> v{"Overwrite", "Append"};
    return v;
}

const std::vector<std::string>& suite_options() {
    static const std::vector<std::string> v{"Legacy", "INOP-38"};
    return v;
}

const std::vector<std::string>& count_mode_options() {
    static const std::vector<std::string> v{"Fixed", "Drawn per entry"};
    return v;
}

// Whole-string parse inside an inclusive range, which is what every
// numeric field here needs and what ask_int() gives the terminal menu.
// A field that does not satisfy it is drawn invalid and its Generate
// button is disabled, so no out-of-range value ever reaches the generator.
bool parse_int(const std::string& s, int lo, int hi, int* out) {
    if (s.empty() || s.size() > 9) return false;
    int v = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    if (v < lo || v > hi) return false;
    if (out) *out = v;
    return true;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Thin names over the shared layout, so the call sites below read the way
// they always did and only one file knows the geometry.
float heading(float x, float y, const std::string& text) {
    return form_heading(g_form, x, y, text);
}

void row_label(float x, float y, const std::string& text, bool locked = false) {
    form_row_label(g_form, x, y, text, locked);
}

void row_note(float x, float y, const std::string& text) { form_row_note(g_form, x, y, text); }

Rect ctrl_rect(float x, float y) { return form_control_rect(g_form, x, y); }

// The status a section reports under its own button, in the failure colour
// when it failed. Drawn beside the button rather than under it so a
// section keeps a fixed height whether or not it has run.
void draw_status(float x, float y, const std::string& text, bool error) {
    if (text.empty()) return;
    float w = g_form.col_w - (kWideBtnW + kGap);
    if (error) {
        draw_rect(x, y, w, kRowH, palette::error_bg());
        draw_text(Font::Body, x + 6.0f, y + (kRowH + text_line_height(Font::Body) * 0.7f) * 0.5f,
                  text, palette::error_text());
    } else {
        label(Rect{x, y, w, kRowH}, text, true);
    }
}

}  // namespace

void MaintenancePanel::open() {
    rotor_ = WheelForm{};
    rotor_.count = "50";
    rotor_.prefix = "U";
    rotor_.start = "1";
    rotor_.notches = "0";
    rotor_.path = kRotorsPath;

    reflector_ = WheelForm{};
    reflector_.count = "10";
    reflector_.prefix = "K";
    reflector_.start = "1";
    reflector_.path = kReflectorsPath;

    sheet_ = SheetForm{};
    sheet_.entries = "360";
    sheet_.plug_pairs = std::to_string(suite("38").max_plug_pairs / 2);
    sheet_.notches = std::to_string(suite("38").max_notches);
    sheet_.rotor_count = std::to_string(suite("38").min_rotors);
    sheet_.path = "inop_keysheet.json";

    open_dropdown_id_ = -1;
}

bool MaintenancePanel::wheels_need_confirm(const WheelForm& f) {
    return f.mode_idx == 0 && file_exists(f.path);
}

void MaintenancePanel::frame(const GuiInput& in, int width, int height) {
    back_clicked_ = false;
    wordmark_clicked_ = false;

    float w = static_cast<float>(width), h = static_cast<float>(height);
    begin_widget_frame();

    float top = draw_header(in, w);
    // Set before anything lays a row out, the same as the settings screen.
    g_form.col_w = form_col_w(static_cast<float>(w));
    float x = form_col_x(w);
    float start = begin_scroll_region(top, w, h, scroll_, content_h_, in);

    float y = draw_wheels(in, x, start, true);
    y = draw_wheels(in, x, y + kSectionGap, false);
    y = draw_key_sheet(in, x, y + kSectionGap);
    content_h_ = (y + kMargin) - start;

    end_scroll_region(top, w, h, scroll_, content_h_);

    // After the region ends, so an open dropdown can overhang it instead of
    // being clipped at the bottom edge.
    draw_open_dropdown_popup(in, open_dropdown_id_);
    end_widget_frame(in);
}

float MaintenancePanel::draw_header(const GuiInput& in, float width) {
    // Back stays here. Unlike the settings screen it is not a duplicate of
    // the wordmark yet: it will matter the moment this screen gains a
    // second level, and removing it now would have to be undone then.
    return form_screen_header(in, width, "Maintenance", /*with_back=*/true, &back_clicked_,
                              &wordmark_clicked_);
}

float MaintenancePanel::draw_wheels(const GuiInput& in, float x, float y, bool rotors) {
    WheelForm& f = rotors ? rotor_ : reflector_;
    const Suite& s = suite("38");
    const char* what = rotors ? "rotors" : "reflectors";

    y = heading(x, y, rotors ? "Rotor batch" : "Reflector batch");

    bool changed = false;

    row_label(x, y, std::string("How many ") + what);
    bool count_ok = parse_int(f.count, 1, 500, nullptr);
    changed |= numeric_field(ctrl_rect(x, y), f.count, in, 3, true, !count_ok);
    // Legacy wheels are fixed and historical and are never machine
    // generated, which is why the terminal menu refuses suite 26 here
    // rather than offering it. There is only one suite left to pick, so
    // this says which it is instead of drawing a dropdown with one row.
    // ASCII only in anything drawn: the font atlases are baked from the
    // printable ASCII range, so an em dash here comes out as a hole in the
    // sentence rather than a dash.
    row_note(x, y, "INOP-38 only; Legacy wheels are historic, never generated");
    y += kRowH + kRowGap;

    row_label(x, y, "Name prefix");
    bool prefix_ok = !f.prefix.empty();
    changed |= text_field(ctrl_rect(x, y), f.prefix, in, kPrefixChars, 4, true, !prefix_ok,
                          CaseFold::ToUpper);
    row_label(x + kPairDX, y, "First number");
    bool start_ok = parse_int(f.start, 0, 100000, nullptr);
    changed |= numeric_field(ctrl_rect(x + kPairDX, y), f.start, in, 6, true, !start_ok);
    y += kRowH + kRowGap;

    bool notch_ok = true;
    if (rotors) {
        row_label(x, y, "Notches per rotor");
        notch_ok = parse_int(f.notches, 0, s.max_notches, nullptr);
        changed |= numeric_field(ctrl_rect(x, y), f.notches, in, 2, true, !notch_ok);
        row_note(x, y, "0 leaves them blank, to be set per message");
        y += kRowH + kRowGap;
    }

    row_label(x, y, "Write to");
    bool path_ok = !f.path.empty();
    changed |= text_field(Rect{x + kLabelW + kGap, y, kPathW, kRowH}, f.path, in, kPathChars, 64,
                          true, !path_ok);
    y += kRowH + kRowGap;

    row_label(x, y, "Mode");
    dropdown(ctrl_rect(x, y), mode_options(), f.mode_idx, rotors ? kIdRotorMode : kIdReflectorMode,
             open_dropdown_id_, in, true);
    y += kRowH + kRowGap;

    // A confirmation only stands while it is still about to destroy
    // something: switching to append, or editing the path to name a file
    // that does not exist, retracts it without a further click.
    if (f.confirm && !wheels_need_confirm(f)) {
        f.confirm = false;
        f.status.clear();
    }
    if (changed) f.status.clear();

    bool valid = count_ok && prefix_ok && start_ok && notch_ok && path_ok;
    std::string caption = f.confirm ? "Overwrite " + f.path : "Generate";
    if (button(Rect{x, y, f.confirm ? kWideBtnW : kBtnW, kBtnH}, caption, in, valid, f.confirm)) {
        // Holding Control skips the confirmation, the same bargain the
        // quit dialog offers. It still destroys the file — it just does
        // not stop to ask first.
        if (!f.confirm && !in.ctrl_held && wheels_need_confirm(f)) {
            f.confirm = true;
            f.status = f.path + " already exists. Click again to replace it.";
            f.status_error = true;
        } else {
            f.confirm = false;
            generate_wheels(rotors);
        }
    }
    draw_status(x + kWideBtnW + kGap, y, f.status, f.status_error);

    return y + kRowH;
}

float MaintenancePanel::draw_key_sheet(const GuiInput& in, float x, float y) {
    SheetForm& f = sheet_;
    const Suite& s = suite(f.suite_idx == 0 ? "26" : "38");
    bool ranged = s.min_rotors != s.max_rotors;

    y = heading(x, y, "Key sheet");

    bool changed = false;

    row_label(x, y, "Suite");
    dropdown(ctrl_rect(x, y), suite_options(), f.suite_idx, kIdSheetSuite, open_dropdown_id_, in,
             true);
    y += kRowH + kRowGap;

    row_label(x, y, "Entries");
    bool entries_ok = parse_int(f.entries, 1, 10000, nullptr);
    changed |= numeric_field(ctrl_rect(x, y), f.entries, in, 5, true, !entries_ok);
    row_label(x + kPairDX, y, "Plugboard pairs");
    bool plugs_ok = parse_int(f.plug_pairs, 0, s.max_plug_pairs, nullptr);
    changed |= numeric_field(ctrl_rect(x + kPairDX, y), f.plug_pairs, in, 2, true, !plugs_ok);
    y += kRowH + kRowGap;

    // Legacy wheels carry their historic notches, so there is nothing to
    // choose; the row stays visible and locked rather than appearing and
    // disappearing as the suite changes.
    bool notch_ok = true;
    row_label(x, y, "Notches per rotor", !ranged && s.notches_are_fixed);
    if (s.notches_are_fixed) {
        std::string fixed = "historic";
        text_field(ctrl_rect(x, y), fixed, in, "", 0, false, false);
        row_note(x, y, "Legacy wheels carry the notches they shipped with");
    } else {
        notch_ok = parse_int(f.notches, 1, s.max_notches, nullptr);
        changed |= numeric_field(ctrl_rect(x, y), f.notches, in, 2, true, !notch_ok);
        // Every notch symbol in a machine is distinct, so a high rotor
        // count buys fewer of them per rotor and the alphabet runs out
        // first. The terminal menu says this after the fact; here there is
        // room to say it before the sheet is written.
        int affordable = static_cast<int>(s.alphabet.size()) / s.max_rotors;
        int asked = 0;
        if (notch_ok && parse_int(f.notches, 1, s.max_notches, &asked) && asked > affordable)
            row_note(x, y, "entries using more than " +
                               std::to_string(static_cast<int>(s.alphabet.size()) / asked) +
                               " rotors will carry fewer than this");
        else
            row_note(x, y, "the maximum is usually the better pick");
    }
    y += kRowH + kRowGap;

    row_label(x, y, "Rotor count", !ranged);
    bool rotor_ok = true;
    if (!ranged) {
        std::string fixed = std::to_string(s.min_rotors);
        text_field(ctrl_rect(x, y), fixed, in, "", 0, false, false);
        row_note(x, y, s.name + " always uses " + std::to_string(s.min_rotors) + " rotors");
    } else {
        dropdown(ctrl_rect(x, y), count_mode_options(), f.count_mode_idx, kIdSheetCountMode,
                 open_dropdown_id_, in, true);
        if (f.count_mode_idx == 0) {
            // The only control in the panel with no caption of its own, so
            // the caption carries the range rather than a hint being
            // squeezed in after the field: the column has no room left
            // past the second control of a paired row.
            row_label(x + kPairDX, y,
                      "Rotors, " + std::to_string(s.min_rotors) + " to " +
                          std::to_string(s.max_rotors));
            rotor_ok = parse_int(f.rotor_count, s.min_rotors, s.max_rotors, nullptr);
            changed |= numeric_field(ctrl_rect(x + kPairDX, y), f.rotor_count, in, 2, true,
                                     !rotor_ok);
        }
    }
    y += kRowH + kRowGap;

    row_label(x, y, "Write to");
    bool path_ok = !f.path.empty();
    changed |= text_field(Rect{x + kLabelW + kGap, y, kPathW, kRowH}, f.path, in, kPathChars, 64,
                          true, !path_ok);
    y += kRowH + kRowGap;

    // Same rule as the wheel sections: the sheet is always written whole,
    // so an existing target is always about to be destroyed.
    if (f.confirm && !file_exists(f.path)) {
        f.confirm = false;
        f.status.clear();
    }
    if (changed) f.status.clear();

    bool valid = entries_ok && plugs_ok && notch_ok && rotor_ok && path_ok;
    std::string caption = f.confirm ? "Overwrite " + f.path : "Generate";
    if (button(Rect{x, y, f.confirm ? kWideBtnW : kBtnW, kBtnH}, caption, in, valid, f.confirm)) {
        if (!f.confirm && !in.ctrl_held && file_exists(f.path)) {
            f.confirm = true;
            f.status = f.path + " already exists. Click again to replace it.";
            f.status_error = true;
        } else {
            f.confirm = false;
            generate_key_sheet();
        }
    }
    draw_status(x + kWideBtnW + kGap, y, f.status, f.status_error);

    return y + kRowH;
}

void MaintenancePanel::generate_wheels(bool rotors) {
    WheelForm& f = rotors ? rotor_ : reflector_;
    const Suite& s = suite("38");
    const char* what = rotors ? "rotors" : "reflectors";

    int count = 0, start = 0, notch_n = 0;
    if (!parse_int(f.count, 1, 500, &count) || !parse_int(f.start, 0, 100000, &start) ||
        (rotors && !parse_int(f.notches, 0, s.max_notches, &notch_n))) {
        f.status = "One of the values is out of range.";
        f.status_error = true;
        return;
    }

    bool append = f.mode_idx == 1;

    // Appending to a file that is already rejected as a whole would bury
    // good wheels behind bad ones: load_wheel_file() throws out an entire
    // file on a single duplicate or rotation, so one degenerate batch
    // already sitting in there invalidates everything appended after it
    // too. Checked before anything is generated, exactly as the terminal
    // menu does, so a refusal costs nothing.
    if (append) {
        std::vector<std::string> problems;
        load_wheel_file(f.path, &problems);
        if (!problems.empty()) {
            f.status = f.path + " does not pass validation as it stands, and appending cannot "
                                "fix that. Overwrite it, or write to a fresh path.";
            f.status_error = true;
            return;
        }
    }

    try {
        // build_wheel_batch() runs entropy_self_check() before it draws
        // anything, so a dead entropy source is refused here rather than
        // producing plausible-looking wheels.
        WheelBatch batch = build_wheel_batch(s, rotors, count, f.prefix, start, notch_n);
        std::string err;
        if (!write_wheel_batch(f.path, batch, s, append, &err)) {
            f.status = err + ". Nothing was written; " + f.path + " is untouched.";
            f.status_error = true;
            return;
        }
    } catch (const std::exception& e) {
        f.status = std::string("refusing to generate: ") + e.what();
        f.status_error = true;
        return;
    }

    // Pull them into the live pool now, so a key sheet generated in this
    // same session can actually draw on them.
    load_wheel_file(f.path);
    f.status = std::to_string(count) + " " + what + (append ? " appended to " : " written to ") +
               f.path + "; pool now " + std::to_string(available_rotors(s).size()) +
               " rotors, " + std::to_string(available_reflectors(s).size()) + " reflectors";
    if (f.path != kRotorsPath && f.path != kReflectorsPath)
        f.status += " (only the two default files are loaded automatically at startup)";
    f.status_error = false;
}

void MaintenancePanel::generate_key_sheet() {
    SheetForm& f = sheet_;
    const Suite& s = suite(f.suite_idx == 0 ? "26" : "38");
    bool ranged = s.min_rotors != s.max_rotors;

    int count = 0, plugs = 0, notch_n = 0, fixed_count = s.min_rotors;
    if (!parse_int(f.entries, 1, 10000, &count) ||
        !parse_int(f.plug_pairs, 0, s.max_plug_pairs, &plugs)) {
        f.status = "One of the values is out of range.";
        f.status_error = true;
        return;
    }
    if (!s.notches_are_fixed && !parse_int(f.notches, 1, s.max_notches, &notch_n)) {
        f.status = "One of the values is out of range.";
        f.status_error = true;
        return;
    }

    bool random_count = ranged && f.count_mode_idx == 1;
    if (!random_count && ranged && !parse_int(f.rotor_count, s.min_rotors, s.max_rotors,
                                              &fixed_count)) {
        f.status = "One of the values is out of range.";
        f.status_error = true;
        return;
    }

    // The wheel path gets this inside build_wheel_batch(); the sheet path
    // has no equivalent chokepoint, so the check the terminal menu runs
    // once at the top of the maintenance menu is run here instead.
    try {
        entropy_self_check();
    } catch (const std::exception& e) {
        f.status = std::string("refusing to generate: ") + e.what();
        f.status_error = true;
        return;
    }

    std::string first, err;
    if (!write_key_sheet(f.path, s, count, plugs, notch_n, random_count, fixed_count, &first,
                         &err)) {
        f.status = err;
        f.status_error = true;
        return;
    }
    f.status = std::to_string(count) + " entries written to " + f.path;
    f.status_error = false;
}

}  // namespace gui
}  // namespace inop
