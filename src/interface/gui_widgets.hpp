// gui_widgets.hpp — input snapshot + small immediate-mode widget set.
//
// Widgets both draw themselves (via gui_render) and report interaction for
// the current frame — there is no retained widget tree. Text/numeric
// fields filter typed codepoints at the point of entry against an
// explicit allowed-character set, rather than accepting arbitrary
// input and validating afterward: every field in this panel only ever
// needs plain ASCII (both INOP alphabets are ASCII), so this sidesteps
// IME/dead-key handling entirely.
#pragma once

#include <string>
#include <vector>

#include "gui_render.hpp"

namespace inop {
namespace gui {

struct Rect {
    float x, y, w, h;
};

bool rect_contains(const Rect& r, double mx, double my);

// Populated by gui.cpp's GLFW callbacks once per frame, before any widget
// call. Coordinates are in the same top-left-origin space as draw calls.
struct GuiInput {
    double mouse_x = 0, mouse_y = 0;
    bool mouse_pressed = false;   // left button went down this frame
    bool mouse_released = false;  // left button went up this frame
    std::vector<unsigned int> typed;  // codepoints typed this frame
    bool key_backspace = false;
    bool key_enter = false;
    bool key_escape = false;
    double scroll_y = 0;
};

// Call once at the very start of a frame, before any widget calls.
void begin_widget_frame();
// Call once at the very end of a frame (after every widget, including any
// open dropdown popup, has been drawn) with the REAL input for the frame —
// clears text-field focus if a click landed on nothing focusable.
void end_widget_frame(const GuiInput& in);

// Palette shared across the panel so every file draws in the same voice.
namespace palette {
Color background();
Color panel();
Color border();
Color border_invalid();
Color text();
Color text_dim();
Color accent();       // brass/amber — enabled "Next", focus rings
Color disabled_bg();
Color disabled_text();
Color error_bg();
Color error_text();
}  // namespace palette

void label(const Rect& r, const std::string& text, bool dim = false, Font font = Font::Body);

// Returns true if clicked this frame.
bool button(const Rect& r, const std::string& text, const GuiInput& in, bool enabled,
            bool accent = false);

// Returns true if value changed this frame.
bool toggle(const Rect& r, bool& value, const std::string& text, const GuiInput& in, bool enabled);

// Which way (if any) a typed letter gets folded before the `allowed` check
// — Legacy's alphabet is uppercase-only, INOP-38's is lowercase-only, so
// without folding toward whichever one a field is bound to, an operator
// whose Shift/Caps Lock state doesn't match gets their keystrokes silently
// dropped. Callers editing case-sensitive things (a filename) use None.
enum class CaseFold { None, ToLower, ToUpper };

// Text field editing `value` in place. `allowed` is the full set of
// characters accepted; `case_fold` decides how a typed letter is folded
// before the `allowed` check (see CaseFold above). If `value` is empty and
// `placeholder` isn't, `placeholder` is drawn dimmed in its place — like a
// web form's placeholder attribute, it's display only and is never written
// into `value` itself. `center_text` centers the shown text horizontally
// instead of left-aligning it with a fixed pad — meant for small
// fixed-width single-character boxes (plugboard pairs); leave false for
// anything whose length changes a lot as the operator types, or the text
// will visibly jump as it grows. Returns true if `value` changed.
bool text_field(const Rect& r, std::string& value, const GuiInput& in, const std::string& allowed,
                 size_t max_len, bool enabled, bool invalid, CaseFold case_fold = CaseFold::None,
                 const std::string& placeholder = "", bool center_text = false);

// Digits-only convenience wrapper over text_field.
bool numeric_field(const Rect& r, std::string& value, const GuiInput& in, size_t max_len,
                    bool enabled, bool invalid, bool center_text = false);

// Dropdown. `id` must be a small stable integer unique within one panel
// frame (e.g. a row index) — `open_dropdown_id` is shared panel-wide
// state, so only one dropdown is ever open at a time, and it is always the
// one drawn last (topmost), regardless of layout order. NOTE: a picked
// item does not surface through the return value of this function — it
// writes into `selected` later, from draw_open_dropdown_popup(), since the
// popup draws after every dropdown() call in the frame. The return value
// here is always false; callers should not rely on it.
bool dropdown(const Rect& r, const std::vector<std::string>& options, int& selected, int id,
              int& open_dropdown_id, const GuiInput& in, bool enabled, bool invalid = false);

// Draws the popup list for whichever dropdown is currently open (if any),
// on top of everything else, using the frame's REAL (non-neutered) input.
// Call this once, last, every frame.
void draw_open_dropdown_popup(const GuiInput& in, int& open_dropdown_id);

}  // namespace gui
}  // namespace inop
