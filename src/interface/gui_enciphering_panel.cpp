#include "gui_enciphering_panel.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <vector>

#include "languages.hpp"
#include "registry.hpp"

namespace inop {
namespace gui {

namespace {

constexpr float kMargin = 16.0f;
constexpr float kBtnW = 150.0f;
constexpr float kBtnH = 30.0f;
constexpr float kGap = 8.0f;
// No label column: every box names itself from the inside, along its top,
// which is room the box already had. The whole width of a column is the
// box.
// The message and the ciphertext are the two boxes a whole dispatch is
// typed or pasted into, so they wrap onto a second line instead of
// scrolling sideways under the caret.
constexpr int kInputLines = 2;
// The gap down the middle when the two halves sit beside each other.
constexpr float kColGap = 24.0f;
// The operator's floor for the cipher box: this many groups on one line
// before it wraps. Below it the two halves cannot sit side by side and
// they stack instead.
constexpr int kMinGroupsPerLine = 4;
constexpr float kTitleH = 34.0f;
constexpr float kSectionGap = 16.0f;
// An output box never shows fewer than five lines and never grows past
// ten, after which it scrolls. Both numbers are the operator's.
constexpr int kMinLines = 5;
constexpr int kMaxLines = 10;
// A guard, not a message-length rule: the terminal has none and batch
// input is capped by file size instead.
constexpr size_t kFieldCap = 4096;

float box_height(float field_w, const std::string& text) {
    int lines = text_block_lines(field_w, text);
    if (lines < kMinLines) lines = kMinLines;
    if (lines > kMaxLines) lines = kMaxLines;
    return text_block_height(lines, true);
}

std::vector<std::string> split_spaces(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace

void EncipheringPanel::open(const PanelState& state) {
    // Pipeline holds a reference to the Machine, so it goes first.
    pipeline_.reset();
    machine_.reset();
    open_error_.clear();
    message_.clear();
    cipher_out_.clear();
    marker_out_.clear();
    check_out_.clear();
    encipher_error_.clear();
    cipher_in_.clear();
    marker_in_.clear();
    plain_out_.clear();
    decipher_error_.clear();
    paste_target_ = PasteTarget::None;
    copy_pending_ = false;
    cipher_scroll_ = marker_scroll_ = check_scroll_ = plain_scroll_ = 0;

    suite_code_ = state.suite_code;
    const Suite& su = suite(suite_code_);
    block_ = su.block;

    PipelineConfig cfg;
    cfg.double_pass = state.double_pass;
    cfg.padding = state.padding;
    cfg.moving_reflector = state.moving_reflector;
    apply_suite_lock(cfg, su.historic_lock, su.block);
    padding_ = cfg.padding;

    Alphabet alpha(su.alphabet);
    fold_ = alpha.uses_uppercase() ? CaseFold::ToUpper : CaseFold::ToLower;
    // A typed space becomes SPACE_SUB in preprocess(), and a typed
    // SPACE_SUB is dropped there, so the message field takes the one and
    // not the other. Ciphertext carries SPACE_SUB as an ordinary symbol
    // and spaces as group separators; a marker is bare symbols.
    allowed_message_.clear();
    for (char c : su.alphabet)
        if (c != SPACE_SUB) allowed_message_.push_back(c);
    allowed_message_.push_back(' ');
    allowed_cipher_ = su.alphabet + " ";
    allowed_marker_ = su.alphabet;

    std::string rotors;
    for (int i = 0; i < state.rotor_count; ++i)
        rotors += (i ? " " : "") + state.rotor_rows[i].rotor_name;
    summary_ = su.name + "   rotors " + rotors + "   reflector " + state.reflector_name +
               "   double pass " + (cfg.double_pass ? "on" : "off") + "   padding " +
               (cfg.padding ? "on" : "off") + "   moving reflector " +
               (cfg.moving_reflector ? "on" : "off");

    try {
        Settings s = settings_from_panel(state);
        std::string err;
        if (!validate_settings(s, &err)) throw std::runtime_error(err);
        machine_ = std::make_unique<Machine>(build_machine(s));
        pipeline_ = std::make_unique<Pipeline>(*machine_, cfg);
    } catch (const std::exception& e) {
        pipeline_.reset();
        machine_.reset();
        open_error_ = e.what();
    }
}

bool EncipheringPanel::take_copy_request(std::string* text) {
    if (!copy_pending_) return false;
    copy_pending_ = false;
    *text = copy_text_;
    copy_text_.clear();
    return true;
}

std::string EncipheringPanel::filtered(const std::string& text, const std::string& allowed) const {
    if (!machine_) return "";
    const Alphabet& alpha = machine_->alphabet();
    std::string out;
    for (char raw : text) {
        char c = raw;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c != ' ') c = alpha.fold_case(c);
        if (allowed.find(c) == std::string::npos) continue;
        out.push_back(c);
        if (out.size() >= kFieldCap) break;
    }
    return out;
}

void EncipheringPanel::deliver_paste(const std::string& text) {
    PasteTarget target = paste_target_;
    paste_target_ = PasteTarget::None;
    switch (target) {
        case PasteTarget::Message:    message_ = filtered(text, allowed_message_); break;
        case PasteTarget::Ciphertext: cipher_in_ = filtered(text, allowed_cipher_); break;
        case PasteTarget::Marker:     marker_in_ = filtered(text, allowed_marker_); break;
        case PasteTarget::None:       break;
    }
}

void EncipheringPanel::on_encipher() {
    encipher_error_.clear();
    cipher_out_.clear();
    marker_out_.clear();
    check_out_.clear();
    if (!pipeline_ || message_.empty()) return;
    try {
        Encrypted e = pipeline_->encrypt(message_);
        cipher_out_ = group(e.ciphertext, block_);
        marker_out_ = e.marker;
        check_out_ = pipeline_->decrypt(e.ciphertext, e.marker);
    } catch (const std::exception& e) {
        encipher_error_ = e.what();
    }
}

void EncipheringPanel::on_decipher() {
    decipher_error_.clear();
    plain_out_.clear();
    if (!pipeline_) return;

    // Mirrors the terminal: a trailing three-letter language tag, as the
    // terminal appends to INOP-38 ciphertext, is stripped rather than fed
    // to the machine.
    const Suite& su = suite(suite_code_);
    std::vector<std::string> toks = split_spaces(cipher_in_);
    if (!su.historic_lock && !toks.empty() && toks.back().size() == 3 &&
        is_supported_language(toks.back()))
        toks.pop_back();
    std::string clean;
    for (const std::string& t : toks) clean += t;
    if (clean.empty()) {
        decipher_error_ = "nothing to decipher";
        return;
    }

    std::string marker;
    if (padding_) {
        if (marker_in_.empty()) {
            decipher_error_ = "padding is on, so the marker is needed to find the message";
            return;
        }
        marker = marker_in_;
    }

    try {
        plain_out_ = pipeline_->decrypt(clean, marker);
    } catch (const std::exception& e) {
        decipher_error_ = e.what();
    }
}

const char* EncipheringPanel::kBothSeparator = "     ";

void EncipheringPanel::draw_clear_button(const GuiInput& in, float bx, float by) {
    if (button(Rect{bx, by, kBtnW, kBtnH}, "Clear", in, a_field_has_focus()))
        clear_focused_field();
}

void EncipheringPanel::copy_both() {
    copy_text_ = cipher_out_ + kBothSeparator + marker_out_;
    copy_pending_ = true;
}

void EncipheringPanel::frame(const GuiInput& in, int width, int height) {
    begin_widget_frame();
    back_clicked_ = false;
    wordmark_clicked_ = false;

    // Control and Shift and C, the combination the roadmap keybind list
    // had reserved and unused. Control alone is already the "skip the
    // warning" prefix everywhere in here, so it could not be Control and C
    // on its own without meaning two things.
    if (in.ctrl_held && in.shift_held && in.key_letter == 'C' && !cipher_out_.empty() &&
        !marker_out_.empty())
        copy_both();

    float w = static_cast<float>(width), h = static_cast<float>(height);

    float top = draw_header(in, w);

    // A one line box is a caption and a line under it now, so even the
    // shortest of them is taller than the bare field height.
    const float row = text_block_height(1, true) + kGap;
    // The two boxes that wrap are taller again.
    const float input_row = text_field_height(kInputLines, true) + kGap;
    // Two rows of buttons, which is what the encipher half needs. The
    // decipher half needs one and reserves two anyway, so the first box in
    // each half starts at the same height and the two read as one screen.
    const float ctrl_h = 2 * (kBtnH + kGap);

    // Four sixteen character groups on one line before the cipher box
    // wraps is the operator's floor, and it is what decides whether the
    // two halves fit beside each other. Measured rather than assumed: a
    // proportional face is not the monospace one this was drawn against,
    // and the suite decides both the group size and the letters in it.
    std::string sample;
    {
        const std::string& alpha = suite(suite_code_).alphabet;
        for (int g = 0; g < kMinGroupsPerLine; ++g) {
            if (g) sample += "  ";
            for (int i = 0; i < block_; ++i)
                sample += alpha[static_cast<size_t>(g * block_ + i) % alpha.size()];
        }
    }
    // Slack for the box's own padding, which the widget set keeps to
    // itself, plus a character of room so the fit is not decided by a
    // rounding error.
    const float need = text_width(Font::Body, sample) + 20.0f;

    const float two_col_field = (w - 2 * kMargin - kColGap) * 0.5f;
    const bool two_col = two_col_field >= need;
    const float field_w = two_col ? two_col_field : w - 2 * kMargin;

    // Everything in a half that is not a growable output box. What is left
    // of the window goes to the boxes that are, so a full screen still
    // fits rather than running off the bottom.
    const float e_fixed = kTitleH + ctrl_h + input_row + kGap + row;
    float d_fixed = kTitleH + ctrl_h + input_row;
    // The decipher marker is typed into rather than read, so it is a field
    // and not a block, and the two are not the same height.
    if (padding_) d_fixed += text_field_height(1, true) + kGap;

    float want_cipher = box_height(field_w, cipher_out_);
    float want_check =
        box_height(field_w, encipher_error_.empty() ? check_out_ : encipher_error_);
    float want_plain =
        box_height(field_w, decipher_error_.empty() ? plain_out_ : decipher_error_);

    const float floor_h = text_block_height(1);
    if (two_col) {
        // Side by side the two halves are given the same height and spend
        // it separately, so a long ciphertext does not squeeze the plain
        // text box in the other column.
        float e_room = h - top - kMargin - e_fixed;
        if (e_room < 2 * floor_h) e_room = 2 * floor_h;
        const float e_want = want_cipher + want_check;
        if (e_want > e_room) {
            const float scale = e_room / e_want;
            want_cipher = std::max(floor_h, want_cipher * scale);
            want_check = std::max(floor_h, want_check * scale);
        }
        float d_room = h - top - kMargin - d_fixed;
        if (d_room < floor_h) d_room = floor_h;
        if (want_plain > d_room) want_plain = d_room;
    } else {
        float room = h - top - kMargin - kSectionGap - e_fixed - d_fixed;
        if (room < 3 * floor_h) room = 3 * floor_h;
        const float want = want_cipher + want_check + want_plain;
        if (want > room) {
            const float scale = room / want;
            want_cipher = std::max(floor_h, want_cipher * scale);
            want_check = std::max(floor_h, want_check * scale);
            want_plain = std::max(floor_h, want_plain * scale);
        }
    }
    h_cipher_ = want_cipher;
    h_check_ = want_check;
    h_plain_ = want_plain;

    if (two_col) {
        draw_encipher(in, kMargin, top, field_w, ctrl_h);
        draw_decipher(in, kMargin + field_w + kColGap, top, field_w, ctrl_h);
    } else {
        float y = draw_encipher(in, kMargin, top, field_w, ctrl_h);
        draw_decipher(in, kMargin, y + kSectionGap, field_w, ctrl_h);
    }

    end_widget_frame(in);
}

float EncipheringPanel::draw_header(const GuiInput& in, float width) {
    const float pad = 6.0f;
    // The setup screen mirrored: Back sits where Next was, the wordmark
    // on the far side, the alphabet strip between them.
    Rect back_r{16, pad, kBtnW, kBtnH};
    if (button(back_r, "Back", in, true)) back_clicked_ = true;

    float word_tw = text_width(Font::Wordmark, "INOP");
    float word_th = text_line_height(Font::Wordmark);
    Rect wordmark_r{width - 16 - (word_tw + 24.0f), pad, word_tw + 24.0f, word_th + 12.0f};
    if (wordmark_button(wordmark_r, in)) wordmark_clicked_ = true;

    const Suite& su = suite(suite_code_);
    float gap_x0 = back_r.x + back_r.w + 20.0f;
    float gap_x1 = wordmark_r.x - 20.0f;
    float alpha_tw = text_width(Font::BodyLarge, su.alphabet);
    float strip_x = gap_x0 + std::max(0.0f, (gap_x1 - gap_x0 - alpha_tw) * 0.5f);
    label(Rect{strip_x, pad, alpha_tw, word_th + 12.0f}, su.alphabet, false, Font::BodyLarge);

    float y = pad + word_th + 12.0f + 6.0f;
    label(Rect{16, y, width - 32, 24}, summary_, true);
    y += 28.0f;
    if (!open_error_.empty()) {
        Rect box{16, y, width - 32, 28};
        draw_rect(box.x, box.y, box.w, box.h, palette::error_bg());
        draw_rect_outline(box.x, box.y, box.w, box.h, palette::error_text());
        label(Rect{box.x + 8, box.y, box.w - 16, box.h}, "the machine could not be built: " + open_error_);
        y += 32.0f;
    }
    return y + 8.0f;
}

float EncipheringPanel::draw_encipher(const GuiInput& in, float x, float y, float field_w,
                                      float ctrl_h) {
    const bool ready = pipeline_ != nullptr;
    label(Rect{x, y, 200, 26}, "Encipher", false, Font::BodyLarge);
    y += kTitleH;

    // Heading, then controls, then the boxes. Two rows: what puts a
    // message in and works the machine, then what takes the result out.
    // The buttons naming a box carry its name, because a row of them
    // sitting above four boxes cannot say which one it means by position.
    float cy = y;
    if (button(Rect{x, cy, kBtnW, kBtnH}, "Paste", in, ready))
        paste_target_ = PasteTarget::Message;
    if (button(Rect{x + (kBtnW + kGap), cy, kBtnW, kBtnH}, "Encipher", in,
               ready && !message_.empty(), true))
        on_encipher();
    draw_clear_button(in, x + 2 * (kBtnW + kGap), cy);
    cy += kBtnH + kGap;
    if (button(Rect{x, cy, kBtnW, kBtnH}, "Copy cipher", in, !cipher_out_.empty())) {
        copy_text_ = cipher_out_;
        copy_pending_ = true;
    }
    if (button(Rect{x + (kBtnW + kGap), cy, kBtnW, kBtnH}, "Copy marker", in,
               !marker_out_.empty())) {
        copy_text_ = marker_out_;
        copy_pending_ = true;
    }
    // The composite copy needs both halves: with padding off there is no
    // marker, and half a dispatch pasted with five spaces hanging off the
    // end would be worse than no button.
    if (button(Rect{x + 2 * (kBtnW + kGap), cy, kBtnW, kBtnH}, "Copy both", in,
               !cipher_out_.empty() && !marker_out_.empty()))
        copy_both();
    y += ctrl_h;

    const float input_h = text_field_height(kInputLines, true);
    text_field(Rect{x, y, field_w, input_h}, message_, in, allowed_message_, kFieldCap, ready,
               false, fold_, "", false, kInputLines, "message");
    y += input_h + kGap;

    text_block(Rect{x, y, field_w, h_cipher_}, cipher_out_, in, cipher_scroll_, false, "cipher");
    y += h_cipher_ + kGap;

    const float one_h = text_block_height(1, true);
    if (marker_out_.empty())
        text_block(Rect{x, y, field_w, one_h}, padding_ ? "" : "no marker: padding is off", in,
                   marker_scroll_, true, "marker");
    else
        text_block(Rect{x, y, field_w, one_h}, marker_out_, in, marker_scroll_, false, "marker");
    y += one_h + kGap;

    if (!encipher_error_.empty())
        text_block(Rect{x, y, field_w, h_check_}, "error: " + encipher_error_, in, check_scroll_,
                   true, "check");
    else
        text_block(Rect{x, y, field_w, h_check_}, check_out_, in, check_scroll_, false, "check");
    return y + h_check_;
}

float EncipheringPanel::draw_decipher(const GuiInput& in, float x, float y, float field_w,
                                      float ctrl_h) {
    const bool ready = pipeline_ != nullptr;
    label(Rect{x, y, 200, 26}, "Decipher", false, Font::BodyLarge);
    y += kTitleH;

    float cy = y;
    if (button(Rect{x, cy, kBtnW, kBtnH}, "Paste cipher", in, ready))
        paste_target_ = PasteTarget::Ciphertext;
    if (button(Rect{x + (kBtnW + kGap), cy, kBtnW, kBtnH}, "Paste marker", in, ready && padding_))
        paste_target_ = PasteTarget::Marker;
    const bool can_decipher = ready && !cipher_in_.empty() && (!padding_ || !marker_in_.empty());
    if (button(Rect{x + 2 * (kBtnW + kGap), cy, kBtnW, kBtnH}, "Decipher", in, can_decipher, true))
        on_decipher();
    cy += kBtnH + kGap;
    draw_clear_button(in, x, cy);
    y += ctrl_h;

    const float input_h = text_field_height(kInputLines, true);
    text_field(Rect{x, y, field_w, input_h}, cipher_in_, in, allowed_cipher_, kFieldCap, ready,
               false, fold_, "", false, kInputLines, "ciphertext");
    y += input_h + kGap;

    if (padding_) {
        const float marker_h = text_field_height(1, true);
        text_field(Rect{x, y, field_w, marker_h}, marker_in_, in, allowed_marker_, kFieldCap,
                   ready, false, fold_, "", false, 1, "marker");
        y += marker_h + kGap;
    }

    if (!decipher_error_.empty())
        text_block(Rect{x, y, field_w, h_plain_}, "error: " + decipher_error_, in, plain_scroll_,
                   true, "plain");
    else
        text_block(Rect{x, y, field_w, h_plain_}, plain_out_, in, plain_scroll_, false, "plain");
    return y + h_plain_;
}

}  // namespace gui
}  // namespace inop
