#include "gui_form.hpp"

#include <algorithm>

#include "gui_render.hpp"

namespace inop {
namespace gui {

float form_col_w(float window_w) {
    return std::clamp(window_w - 2.0f * kFormMargin, kFormColWMin, kFormColWMax);
}

float form_col_x(float window_w) {
    return std::max(kFormMargin, (window_w - form_col_w(window_w)) * 0.5f);
}

float form_heading(const FormMetrics& m, float x, float y, const std::string& text) {
    label(Rect{x, y, m.col_w, kFormHeadingH}, text, false, Font::BodyLarge);
    draw_rect(x, y + kFormHeadingH - 2.0f, m.col_w, 1.0f, palette::border());
    return y + kFormHeadingH + kFormRowGap;
}

void form_row_label(const FormMetrics& m, float x, float y, const std::string& text, bool locked) {
    label(Rect{x, y, m.label_w, kFormRowH}, text, locked);
}

void form_row_note(const FormMetrics& m, float x, float y, const std::string& text) {
    const Rect r{x + m.label_w + m.gap + m.ctrl_w + m.gap, y,
                 m.col_w - m.label_w - m.ctrl_w - 2 * m.gap, kFormRowH};
    // SGA draws the longest of these notes at over 1600 pixels even after
    // its own size reduction, so a wide face has to be cut somewhere.
    // Clipped rather than shortened, because every other face fits inside
    // the widened column and clipping costs those nothing.
    begin_scissor(r.x, r.y, r.w, r.h);
    label(r, text, true);
    end_scissor();
}

Rect form_control_rect(const FormMetrics& m, float x, float y) {
    return Rect{x + m.label_w + m.gap, y, m.ctrl_w, kFormRowH};
}

float form_screen_header(const GuiInput& in, float width, const std::string& title,
                         bool* wordmark_clicked) {
    const float pad = 6.0f;

    // The wordmark on the left, where the eye starts, and where the legal
    // screen already puts it.
    float word_tw = text_width(Font::Wordmark, "INOP");
    float word_th = text_line_height(Font::Wordmark);
    Rect wordmark_r{kFormMargin, pad, word_tw + 24.0f, word_th + 12.0f};
    if (wordmark_button(wordmark_r, in) && wordmark_clicked) *wordmark_clicked = true;

    // The title centred in what is left of the bar, rather than in the
    // whole of it, so it does not drift under the wordmark on a narrow
    // window.
    float gap_x0 = wordmark_r.x + wordmark_r.w + 20.0f;
    float gap_x1 = width - kFormMargin;
    float title_tw = text_width(Font::BodyLarge, title);
    float title_x = gap_x0 + std::max(0.0f, (gap_x1 - gap_x0 - title_tw) * 0.5f);
    label(Rect{title_x, pad, title_tw, word_th + 12.0f}, title, false, Font::BodyLarge);

    return pad + word_th + 12.0f + 10.0f;
}

}  // namespace gui
}  // namespace inop
