// gui_form.hpp - the row layout the settings and maintenance screens share.
//
// Both screens are the same thing on paper: a centred column of headings,
// each heading followed by rows of "label, control, dim note". They grew
// their own private copies of that layout, byte for byte identical in
// places, and the copies drifted the moment one of them learned something
// the other did not. This is the one definition.
//
// What genuinely differs between the two screens is three widths, so those
// are the only thing FormMetrics carries. Everything else is a constant
// here, because two screens disagreeing about a row height was never a
// design decision, only an accident waiting to happen.

#ifndef INOP_GUI_FORM_HPP
#define INOP_GUI_FORM_HPP

#include <string>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

constexpr float kFormMargin = 16.0f;
constexpr float kFormBtnW = 150.0f;
constexpr float kFormBtnH = 30.0f;
constexpr float kFormRowH = 30.0f;
constexpr float kFormRowGap = 8.0f;
constexpr float kFormSectionGap = 18.0f;
constexpr float kFormHeadingH = 30.0f;

// The column grows with the window between these two. It has to grow at
// all because the note beside a row is the one thing on these screens
// whose length the layout does not choose: the longest settings note needs
// 524 pixels in Courier and 362 in Times, against the 250 a fixed 840
// column left for it. The upper bound stops the page stretching into a
// line too long to read on a wide monitor.
constexpr float kFormColWMin = 840.0f;
constexpr float kFormColWMax = 1150.0f;

// The column width for a window this wide, and the x the column starts at.
// Both screens ask, so neither can drift from the other.
float form_col_w(float window_w);
float form_col_x(float window_w);

// The three widths that are a real difference between the two screens.
// col_w is filled from form_col_w() once per frame rather than recomputed
// per row, so every row of one frame agrees.
struct FormMetrics {
    float label_w = 0.0f;
    float ctrl_w = 0.0f;
    float gap = 0.0f;
    float col_w = kFormColWMin;
};

// One section heading with the rule under it. Returns the y where the
// first row of the section starts.
float form_heading(const FormMetrics& m, float x, float y, const std::string& text);

// The caption on the left of a row, dimmed when the row is locked.
void form_row_label(const FormMetrics& m, float x, float y, const std::string& text,
                    bool locked = false);

// The dim explanation to the right of a control, saying what a value means
// or why a row is locked. Clipped to its own box: label() draws its text
// whatever the rect says, and a wide typeface makes these longer than any
// column could reasonably be.
void form_row_note(const FormMetrics& m, float x, float y, const std::string& text);

// Where a row's control goes.
Rect form_control_rect(const FormMetrics& m, float x, float y);

// The bar every screen wears: the wordmark on the left, what this screen
// is beside it. Returns the y where the content below it starts.
//
// There is no Back button. It went where the wordmark goes and meant what
// the wordmark means, so both screens carried two controls doing one job.
// The wordmark keeps it, because it means the same on every screen, and
// Escape does the same thing from the keyboard.
float form_screen_header(const GuiInput& in, float width, const std::string& title,
                         bool* wordmark_clicked);

}  // namespace gui
}  // namespace inop

#endif  // INOP_GUI_FORM_HPP
