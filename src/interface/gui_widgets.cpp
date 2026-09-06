#include "gui_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "gui_anim.hpp"

namespace inop {
namespace gui {

bool rect_contains(const Rect& r, double mx, double my) {
    return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

namespace {

const void* g_focus = nullptr;       // identity of the std::string* being edited
// The same field again, writable, so that a Clear button somewhere else on
// the screen can empty whichever box the operator is in without the panel
// having to keep its own idea of which one that is.
std::string* g_focus_field = nullptr;
bool g_click_consumed_this_frame = false;

// Per-field editing state. Keyed on the address of the std::string a field
// edits, which is what focus is already keyed on, and which is stable: the
// strings are members of panels that live as long as the application. An
// entry is only made once a field is focused, so the disabled read-only
// fields that hand text_field() a local never leave one behind.
struct FieldEdit {
    size_t caret = 0;
    // Where a drag started. Equal to caret when nothing is selected, which
    // is why there is no separate "has a selection" flag.
    size_t anchor = 0;
    bool dragging = false;
    // First character shown in the box, so that a caret pushed past either
    // edge brings the text with it instead of going out of sight. Single
    // line boxes only: a box that wraps has no sideways travel to make.
    size_t view_start = 0;
    // First row shown in a box that wraps, which is the same idea turned
    // ninety degrees. Unused at one line.
    size_t first_row = 0;
    // Five steps back, oldest first, per the roadmap. Each box keeps its
    // own; there is deliberately no shared history for the screen.
    std::vector<std::string> undo;
    std::vector<std::string> redo;
};
const size_t kUndoSteps = 5;
std::map<const void*, FieldEdit> g_edits;

FieldEdit& edit_state(const std::string& value) { return g_edits[&value]; }

void push_undo(FieldEdit& e, const std::string& before) {
    e.undo.push_back(before);
    if (e.undo.size() > kUndoSteps) e.undo.erase(e.undo.begin());
    // A fresh edit is a new branch, so whatever was undone is unreachable.
    e.redo.clear();
}

// Everything needed to draw the closed face of a dropdown: the box, the
// selected text and the little arrow. Kept rather than drawn once, because
// an open list rolls out from behind its own button, which means the
// button has to be drawn again on top of the list after the list is drawn.
struct DropdownFace {
    Rect box{0, 0, 0, 0};
    Color bg{0, 0, 0, 0};
    Color border{0, 0, 0, 0};
    std::string shown;
    bool dim = false;
    bool open = false;
    bool ring = false;
    // The face of the currently picked row, so the closed box shows what
    // was chosen in the typeface it names. Empty means the interface face.
    std::string font_file;
};

struct PendingDropdown {
    bool active = false;
    int id = -1;
    Rect box;
    const std::vector<std::string>* options = nullptr;
    const std::vector<std::string>* item_fonts = nullptr;
    int* selected = nullptr;
    DropdownFace face;
};
PendingDropdown g_pending;

// Scroll offset (pixels) for whichever dropdown popup is currently open.
// Reset to 0 whenever a *different* dropdown becomes the open one, so
// switching from a long rotor-picker list to another dropdown never
// starts mid-scrolled. Only one popup is ever open at a time, so a single
// shared offset (rather than one per dropdown id) is enough.
float g_popup_scroll = 0.0f;
int g_popup_scroll_owner = -1;

// How long the open list has been rolling out, in seconds. A list grows
// downward from behind its own button instead of appearing at full length,
// so this drives its height and the button is drawn over the top of it.
// Reset whenever a different dropdown becomes the open one.
float g_popup_open_t = 0.0f;

// Quicker than a screen change, because the list is a small thing moving a
// short way and the pointer is usually already on its way to a row.
constexpr float kPopupRollSeconds = 0.12f;

// Where the open dropdown popup was drawn on the previous frame, and
// whether there was one. A popup is drawn last so it sits on top, but
// every widget it covers has already run its own hit test by then, so
// without this the control underneath sees the click first and takes it:
// picking the fourth entry of a five-entry list would instead open
// whatever dropdown that row happened to be sitting over. The rect is a
// frame behind, which is exactly right — a popup has to have been drawn
// before it can be clicked.
//
// begin_widget_frame() ages one into the other every frame rather than
// leaving a rect set until something clears it. A screen with no
// dropdowns at all never calls draw_open_dropdown_popup, so a rect that
// stayed set would go on swallowing clicks there for the rest of the
// session — leaving the setup screen with a list open would have left a
// dead patch in the middle of the main menu.
Rect g_popup_rect_last{0, 0, 0, 0};
bool g_popup_shown_last = false;
bool g_popup_drawn_this_frame = false;

// Which modal layer is being drawn into right now, and how many have been
// opened so far this frame. Layer numbers are handed out in the order the
// layers open, never reused and never counted back down, so a bigger
// number always means drawn later and therefore on top. That is what lets
// a delete confirmation sitting over the load panel take the keyboard from
// the load panel, and the quit modal take it from both.
//
// The popup guard below exists to stop a click landing on a control that
// happens to sit under an open dropdown popup; a modal is drawn over
// everything including that popup, so for its own buttons the guard is
// exactly wrong and would swallow the click that dismisses it.
int g_modal_seq = 0;
std::vector<int> g_modal_stack;

int current_modal_layer() { return g_modal_stack.empty() ? 0 : g_modal_stack.back(); }

// The tooltip asked for this frame, if any. Deferred for the same reason
// the dropdown popup is, so that it lands above whatever it overlaps.
// Nothing here is remembered across frames: unlike the popup it takes no
// clicks, so there is no last frame rect to test against.
struct PendingTooltip {
    bool active = false;
    Rect anchor{0, 0, 0, 0};
    std::string text;
    float shown_for = 0.0f;
};
PendingTooltip g_tooltip;

bool click_over_open_popup(const GuiInput& in) {
    if (current_modal_layer() > 0) return false;
    return g_popup_shown_last && rect_contains(g_popup_rect_last, in.mouse_x, in.mouse_y);
}

const float PAD = 6.0f;

// -- keyboard focus ------------------------------------------------------
//
// Immediate mode keeps no widget tree, so a focus is keyed on the rect a
// control draws itself into, exactly as the animation timers are. That is
// why every widget got a hover highlight rather than only the buttons: a
// rect handed to gui_anim can be lit by a keyboard focus without a second
// highlight path beside the hover one.
//
// Navigation is by position and not by draw order. The setup screen is a
// real grid -- rotor rows against a ring column and a notch column, with
// the plugboard pairs below -- and a flat order would make Down walk
// sideways through it.
//
// The rects are collected as the frame draws and the arrows are answered
// at the start of the next one. A frame cannot know what is on it until it
// has drawn, so there is no earlier moment to answer them.

struct FocusRect {
    Rect r;
    // Which modal layer it was drawn into, 0 for the screen itself. The
    // topmost layer on the frame owns the keyboard, and this is how that
    // falls out without anything else having to know a modal is open.
    int layer = 0;
};

std::vector<FocusRect> g_focus_filling;  // the frame being drawn now
std::vector<FocusRect> g_focus_ready;    // the frame that finished drawing
Rect g_focused{0, 0, 0, 0};
bool g_has_focus = false;

bool same_rect(const Rect& a, const Rect& b) {
    const float e = 0.5f;
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e &&
           std::fabs(a.w - b.w) < e && std::fabs(a.h - b.h) < e;
}

// Registers a control as somewhere the focus can land, and says whether it
// is where the focus is right now. Disabled controls never call this: one
// that cannot be worked cannot be reached either.
//
// A click also moves the focus here, so the pointer and the keyboard share
// one idea of where you are rather than each keeping their own.
bool focus_register(const Rect& r, const GuiInput& in) {
    g_focus_filling.push_back(FocusRect{r, current_modal_layer()});
    if (in.mouse_pressed && rect_contains(r, in.mouse_x, in.mouse_y) &&
        !click_over_open_popup(in)) {
        g_focused = r;
        g_has_focus = true;
    }
    return g_has_focus && same_rect(g_focused, r);
}

// A thin brass ring just outside the control. Outside rather than on it,
// because the control already uses its own border for hover and for
// invalid, and a focus that borrowed either of those would be telling you
// two things with one line.
void draw_focus_ring(const Rect& r) {
    draw_rect_outline(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f, palette::accent());
}

}  // namespace

void begin_widget_frame() {
    // Defensive: an unbalanced begin_modal_layer() in a screen would
    // otherwise leak into the next screen drawn.
    g_modal_stack.clear();
    anim_begin_frame();
    g_tooltip.active = false;
    g_click_consumed_this_frame = false;
    g_pending.active = false;
    g_popup_shown_last = g_popup_drawn_this_frame;
    g_popup_drawn_this_frame = false;
}

void end_widget_frame(const GuiInput& in) {
    if (in.mouse_pressed && !g_click_consumed_this_frame) {
        g_focus = nullptr;
        g_focus_field = nullptr;
    }
}

bool has_keyboard_focus(const Rect& r) { return g_has_focus && same_rect(g_focused, r); }

void set_keyboard_focus(const Rect& r) {
    g_focused = r;
    g_has_focus = true;
}

void begin_modal_layer() { g_modal_stack.push_back(++g_modal_seq); }

void end_modal_layer() {
    if (!g_modal_stack.empty()) g_modal_stack.pop_back();
}

bool modal_layer_open() { return g_modal_seq > 0; }

void resolve_focus(const GuiInput& in) {
    g_focus_ready.swap(g_focus_filling);
    g_focus_filling.clear();
    // Called once per frame, before anything draws, so this is where the
    // frames layer numbering starts over.
    g_modal_seq = 0;
    g_modal_stack.clear();

    // Whichever layer drew last frame sits on top, so it and nothing else
    // is reachable. With no modal open that is layer 0, the screen itself.
    int top_layer = 0;
    for (const FocusRect& c : g_focus_ready)
        if (c.layer > top_layer) top_layer = c.layer;

    std::vector<Rect> pool;
    for (const FocusRect& c : g_focus_ready)
        if (c.layer == top_layer) pool.push_back(c.r);

    // The focus dies with the control it was on. A screen change, or a
    // modal opening or closing, leaves it pointing at a rect nothing draws
    // any more, and an arrow pressed later would navigate from nowhere.
    if (g_has_focus) {
        bool still_there = false;
        for (const Rect& r : pool)
            if (same_rect(r, g_focused)) {
                still_there = true;
                break;
            }
        if (!still_there) g_has_focus = false;
    }

    const bool up = in.key_up, down = in.key_down;
    const bool left = in.key_left, right = in.key_right;
    if (pool.empty() || (!up && !down && !left && !right)) return;

    // An open list has first claim on the arrows, and answers them itself
    // in draw_open_dropdown_popup(). g_popup_drawn_this_frame still holds
    // last frames answer here, because begin_widget_frame() ages it and
    // has not run yet.
    if (g_popup_drawn_this_frame) return;

    if (!g_has_focus) {
        // The first press lands on the first control on the screen, in
        // reading order, whatever the pointer happens to be doing. The
        // same key always starts you in the same place.
        const Rect* first = &pool[0];
        for (const Rect& r : pool)
            if (r.y < first->y - 0.5f || (std::fabs(r.y - first->y) < 0.5f && r.x < first->x))
                first = &r;
        g_focused = *first;
        g_has_focus = true;
        return;
    }

    // Two classes of candidate, and the first always beats the second.
    //
    // A control lines up when its span overlaps the focused one across the
    // direction of travel: for Down and Up that is the horizontal span,
    // for Left and Right the vertical one. Those are the ones an arrow
    // obviously means, and taking the nearest of them is what makes Down
    // walk down one column of the rotor grid instead of stepping into the
    // ring column because something there happened to be nearer.
    //
    // Nothing lines up with a control sitting alone in a corner, such as
    // Back on the settings screen. Only then does the second class matter,
    // and there the nearest thing in that direction is the whole of the
    // answer -- weighing sideways distance more heavily there sends the
    // focus to the far bottom of the screen, which is what it used to do.
    const Rect& f = g_focused;
    const float cx = f.x + f.w * 0.5f;
    const float cy = f.y + f.h * 0.5f;
    const bool vertical = up || down;
    const Rect* best = nullptr;
    float best_score = 0.0f;
    bool best_lines_up = false;
    for (const Rect& r : pool) {
        if (same_rect(r, f)) continue;
        const float rx = r.x + r.w * 0.5f;
        const float ry = r.y + r.h * 0.5f;
        float along = 0.0f, across = 0.0f;
        if (down) {
            along = ry - cy;
            across = std::fabs(rx - cx);
        } else if (up) {
            along = cy - ry;
            across = std::fabs(rx - cx);
        } else if (right) {
            along = rx - cx;
            across = std::fabs(ry - cy);
        } else {
            along = cx - rx;
            across = std::fabs(ry - cy);
        }
        // Nothing behind the arrow, and nothing level with it either: a
        // control whose centre has not moved in the direction pressed is
        // not what that arrow means.
        if (along <= 1.0f) continue;

        const bool lines_up = vertical ? (r.x < f.x + f.w && f.x < r.x + r.w)
                                       : (r.y < f.y + f.h && f.y < r.y + r.h);
        const float score = along + across;
        const bool better = !best || (lines_up && !best_lines_up) ||
                            (lines_up == best_lines_up && score < best_score);
        if (better) {
            best = &r;
            best_score = score;
            best_lines_up = lines_up;
        }
    }
    if (best) g_focused = *best;
}

namespace palette {
namespace {

struct Colors {
    Color background, panel, border, border_invalid;
    Color text, text_dim, accent;
    Color disabled_bg, disabled_text;
    Color error_bg, error_text;
};

// The dark palette is the original one and stays the default. The light
// one is the same design at inverted lightness: the greys keep their
// spacing, so a panel still reads as raised off the background and a
// disabled control still reads as sunk into it.
Colors dark_base() {
    Colors c;
    c.background = rgba(0.09f, 0.09f, 0.10f);
    c.panel = rgba(0.14f, 0.14f, 0.16f);
    c.border = rgba(0.32f, 0.32f, 0.36f);
    c.border_invalid = rgba(0.75f, 0.30f, 0.28f);
    c.text = rgba(0.92f, 0.92f, 0.90f);
    c.text_dim = rgba(0.58f, 0.58f, 0.60f);
    c.accent = rgba(0.78f, 0.60f, 0.28f);  // brass/amber
    c.disabled_bg = rgba(0.16f, 0.16f, 0.17f);
    c.disabled_text = rgba(0.40f, 0.40f, 0.42f);
    c.error_bg = rgba(0.30f, 0.12f, 0.12f);
    c.error_text = rgba(0.92f, 0.70f, 0.70f);
    return c;
}

Colors light_base() {
    Colors c;
    c.background = rgba(0.93f, 0.92f, 0.90f);
    c.panel = rgba(0.99f, 0.98f, 0.96f);
    c.border = rgba(0.62f, 0.61f, 0.58f);
    c.border_invalid = rgba(0.70f, 0.18f, 0.16f);
    c.text = rgba(0.12f, 0.12f, 0.11f);
    c.text_dim = rgba(0.44f, 0.44f, 0.43f);
    c.accent = rgba(0.74f, 0.54f, 0.18f);
    c.disabled_bg = rgba(0.87f, 0.86f, 0.84f);
    c.disabled_text = rgba(0.58f, 0.58f, 0.57f);
    c.error_bg = rgba(0.97f, 0.86f, 0.85f);
    c.error_text = rgba(0.55f, 0.10f, 0.10f);
    return c;
}

// Only four colours in the palette carry a signal by hue: the accent (a
// control is focused, enabled, chosen), and the three that say something
// is wrong. Everything else is a neutral grey and reads the same under
// every condition, so a colourblind mode only ever touches these four.
//
// Each mode is named for the pair the operator cannot separate, so the
// rule is simply that the two signals must not be that pair. Under
// red-green the classic surviving pair is blue against orange; under
// red-blue, green against amber; under blue-green, brass against magenta.
// Monochrome has no hue left to use at all, so the two signals separate
// by lightness instead, which is the one channel every condition keeps.
void apply_colourblind(Colors& c, ColourblindMode mode, Theme theme) {
    const bool dark = theme == Theme::Dark;
    switch (mode) {
        // Protanopia and deuteranopia are both red-green deficiencies and
        // want the same thing: signalling that never asks red and green to
        // be told apart. Blue for "this is the way on", amber for "this is
        // wrong" — both survive either type, and they differ from each
        // other in hue and in luminance, so the pair is still separable if
        // the display is poor. Sharing one arm rather than inventing a
        // cosmetic difference between two conditions that need the same
        // remedy.
        case ColourblindMode::Protanopia:
        case ColourblindMode::Deuteranopia:
            c.accent = dark ? rgba(0.35f, 0.62f, 0.95f) : rgba(0.16f, 0.40f, 0.82f);
            c.border_invalid = dark ? rgba(0.92f, 0.58f, 0.12f) : rgba(0.78f, 0.45f, 0.04f);
            c.error_bg = dark ? rgba(0.32f, 0.20f, 0.03f) : rgba(0.99f, 0.90f, 0.75f);
            c.error_text = dark ? rgba(0.98f, 0.78f, 0.35f) : rgba(0.52f, 0.30f, 0.02f);
            break;
        case ColourblindMode::Tritanopia:
            // Blue against yellow is the pair that fails here, which rules
            // out both the default brass accent and the blue used above.
            // Red and green are seen normally, so the signalling moves
            // there: green for the way on, magenta for wrongness.
            c.accent = dark ? rgba(0.32f, 0.74f, 0.40f) : rgba(0.11f, 0.46f, 0.20f);
            c.border_invalid = dark ? rgba(0.88f, 0.28f, 0.68f) : rgba(0.72f, 0.10f, 0.50f);
            c.error_bg = dark ? rgba(0.30f, 0.08f, 0.22f) : rgba(0.99f, 0.85f, 0.94f);
            c.error_text = dark ? rgba(0.97f, 0.68f, 0.88f) : rgba(0.55f, 0.06f, 0.36f);
            break;
        case ColourblindMode::Achromatopsia:
            c.accent = dark ? rgba(0.70f, 0.70f, 0.70f) : rgba(0.42f, 0.42f, 0.42f);
            c.border_invalid = dark ? rgba(1.00f, 1.00f, 1.00f) : rgba(0.00f, 0.00f, 0.00f);
            c.error_bg = dark ? rgba(0.28f, 0.28f, 0.28f) : rgba(0.80f, 0.80f, 0.80f);
            c.error_text = dark ? rgba(1.00f, 1.00f, 1.00f) : rgba(0.04f, 0.04f, 0.04f);
            break;
        case ColourblindMode::Full:
        default:
            break;
    }
}

Colors g_current = dark_base();

}  // namespace

void set_palette(Theme theme, ColourblindMode mode) {
    // Resolved once, here, so that neither the base choice below nor
    // apply_colourblind() has to know that a third value exists. Callers
    // may pass System freely; nothing downstream of this line ever sees it.
    const Theme t = effective_theme(theme);
    Colors c = t == Theme::Light ? light_base() : dark_base();
    apply_colourblind(c, mode, t);
    g_current = c;
}

Color background() { return g_current.background; }
Color panel() { return g_current.panel; }
Color border() { return g_current.border; }
Color border_invalid() { return g_current.border_invalid; }
Color text() { return g_current.text; }
Color text_dim() { return g_current.text_dim; }
Color accent() { return g_current.accent; }
Color disabled_bg() { return g_current.disabled_bg; }
Color disabled_text() { return g_current.disabled_text; }
Color error_bg() { return g_current.error_bg; }
Color error_text() { return g_current.error_text; }

// Worked out from the accent rather than tabled alongside it: a blue or
// dark-green accent needs pale ink where brass needs near-black, and one
// forgotten table entry is an unreadable button. Coefficients are the
// usual perceived-brightness weights.
Color on_accent() {
    const Color a = g_current.accent;
    float luma = 0.299f * a.r + 0.587f * a.g + 0.114f * a.b;
    return luma > 0.55f ? rgba(0.10f, 0.09f, 0.07f) : rgba(0.98f, 0.98f, 0.97f);
}
}  // namespace palette

namespace {

// How far a control sinks while it is held, in logical units, so the dip
// is the same fraction of a button at every zoom.
constexpr float kDipTravel = 2.0f;

Color mix(Color a, Color b, float t) {
    return rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t);
}

// Every control lifts toward the same mid grey the dropdown popup already
// uses for its hovered row, so one hover reads the same wherever it
// happens. That grey sits between the two themes rather than above them,
// which is what lets a single blend brighten a dark panel and darken a
// light one.
Color hover_lift(Color base, float t) {
    if (t <= 0.0f) return base;
    return mix(base, palette::border(), 0.50f * t);
}

// Brass mixed with grey reads as dirty rather than as lit, so an accent
// control lifts toward white instead, and less far, since it is already
// the brightest thing on the screen.
Color hover_lift_accent(Color base, float t) {
    if (t <= 0.0f) return base;
    return mix(base, rgba(1.0f, 1.0f, 1.0f, base.a), 0.22f * t);
}

// A text field already has a loud state of its own in the focus ring, so
// its hover is a hint and not an announcement.
Color hover_lift_soft(Color base, float t) {
    if (t <= 0.0f) return base;
    return mix(base, palette::border(), 0.22f * t);
}

// A held control sinks toward its own shadow. Unlike the lift this goes
// the same way in both themes, because a pressed control reads as further
// off whichever way round the palette is.
Color press_sink(Color base, float t) {
    if (t <= 0.0f) return base;
    return mix(base, rgba(0.0f, 0.0f, 0.0f, base.a), 0.20f * t);
}

// The hover ring. Drawn by recolouring the border rather than as a second
// outline, so nothing changes size and no neighbour has to move.
Color hover_border(float t) {
    return mix(palette::border(), palette::accent(), 0.55f * t);
}

}  // namespace

void label(const Rect& r, const std::string& text, bool dim, Font font) {
    float ty = r.y + (r.h + text_line_height(font) * 0.7f) * 0.5f;
    draw_text(font, r.x, ty, text, dim ? palette::text_dim() : palette::text());
}

bool button(const Rect& r, const std::string& text, const GuiInput& in, bool enabled,
            bool accent) {
    // A disabled control asks for nothing and so claims no timer: it
    // cannot be lit and it cannot be pressed.
    WidgetMotion m = enabled ? widget_motion(r, in) : WidgetMotion{};
    const bool kb = enabled && focus_register(r, in);
    bool hovered = rect_contains(r, in.mouse_x, in.mouse_y);
    bool clicked = false;

    // The dip moves what is drawn and never what is hit. A control that
    // slid out from under the pointer as it went down would take the click
    // with it.
    float dip = motion_enabled() ? m.press * kDipTravel : 0.0f;
    Rect d{r.x, r.y + dip, r.w, r.h};

    Color bg = !enabled ? palette::disabled_bg()
                         : (accent ? palette::accent() : palette::panel());
    if (enabled) {
        bg = accent ? hover_lift_accent(bg, m.hover) : hover_lift(bg, m.hover);
        bg = press_sink(bg, m.press);
    }
    draw_rect(d.x, d.y, d.w, d.h, bg);
    draw_rect_outline(d.x, d.y, d.w, d.h,
                      enabled ? hover_border(m.hover) : palette::border());
    if (kb) draw_focus_ring(d);
    Color fg = !enabled ? palette::disabled_text()
                         : (accent ? palette::on_accent() : palette::text());
    float tw = text_width(Font::Body, text);
    float tx = d.x + (d.w - tw) * 0.5f;
    float ty = d.y + (d.h + text_line_height(Font::Body) * 0.7f) * 0.5f;
    draw_text(Font::Body, tx, ty, text, fg);
    if (enabled && hovered && in.mouse_pressed && !click_over_open_popup(in)) {
        clicked = true;
        g_click_consumed_this_frame = true;
    }
    // Enter works the focused control whatever the pointer is doing, which
    // is the entire point of being able to reach one without a pointer.
    if (kb && in.key_enter) clicked = true;
    return clicked;
}

bool wordmark_button(const Rect& r, const GuiInput& in) {
    WidgetMotion m = widget_motion(r, in);
    const bool kb = focus_register(r, in);
    bool hovered = rect_contains(r, in.mouse_x, in.mouse_y);
    float dip = motion_enabled() ? m.press * kDipTravel : 0.0f;
    // The fill fades in now rather than snapping on. Before these timers
    // existed this was the one hard edged hover in the interface, and
    // leaving it that way would have made the wordmark the odd control out
    // on every screen that carries it.
    if (m.hover > 0.0f) {
        Color fill = press_sink(palette::panel(), m.press);
        fill.a = m.hover;
        draw_rect(r.x, r.y + dip, r.w, r.h, fill);
    }
    draw_text(Font::Wordmark, r.x + 8, r.y + dip + text_line_height(Font::Wordmark) * 0.75f,
              "INOP", palette::text());
    if (kb) draw_focus_ring(r);
    return (hovered && in.mouse_pressed) || (kb && in.key_enter);
}

namespace {

// Widths add up exactly here: the baked atlas has no kerning, so one
// measurement per character is enough to know where a line ends.
std::vector<std::string> wrap_lines_uncached(float box_w, const std::string& text) {
    const float avail = std::max(0.0f, box_w - 2 * PAD);
    std::vector<std::string> lines;
    std::string cur;
    float cur_w = 0;
    size_t last_space = std::string::npos;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
            cur_w = 0;
            last_space = std::string::npos;
            continue;
        }
        float cw = text_width(Font::Body, std::string(1, c));
        if (cur_w + cw > avail && !cur.empty()) {
            if (c == ' ') {
                lines.push_back(cur);
                cur.clear();
                cur_w = 0;
                last_space = std::string::npos;
                continue;
            }
            if (last_space != std::string::npos) {
                lines.push_back(cur.substr(0, last_space));
                cur = cur.substr(last_space + 1);
                cur_w = text_width(Font::Body, cur);
                last_space = std::string::npos;
            } else {
                lines.push_back(cur);
                cur.clear();
                cur_w = 0;
            }
        }
        if (c == ' ') last_space = cur.size();
        cur.push_back(c);
        cur_w += cw;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

// Sizing a growable box wraps its text and keeps only the line count, then
// drawing the same box a moment later wraps the identical text again. At
// the 4096 character cap one wrap is roughly 4096 single character
// text_width calls plus seventy line allocations, so the repeat was the
// largest piece of wasted work in a frame. The enciphering screen sizes
// all three of its boxes before it draws any of them, so the ring has to
// hold more than the three in flight.
//
// The atlas decides how wide a character is, so a re-bake makes every
// entry here wrong. font_generation() is what catches that: changing the
// typeface empties the ring instead of serving widths from the old face.
struct WrapEntry {
    bool used = false;
    float box_w = 0.0f;
    std::string text;
    std::vector<std::string> lines;
};

const std::vector<std::string>& wrap_lines(float box_w, const std::string& text) {
    constexpr size_t kRing = 8;
    static WrapEntry ring[kRing];
    static size_t next = 0;
    static unsigned baked = 0;

    if (baked != font_generation()) {
        baked = font_generation();
        for (WrapEntry& e : ring) e = WrapEntry{};
        next = 0;
    }
    for (const WrapEntry& e : ring)
        if (e.used && e.box_w == box_w && e.text == text) return e.lines;

    WrapEntry& slot = ring[next];
    next = (next + 1) % kRing;
    slot.used = true;
    slot.box_w = box_w;
    slot.text = text;
    slot.lines = wrap_lines_uncached(box_w, text);
    return slot.lines;
}

float block_line_height() { return text_line_height(Font::Body) * 1.3f; }

// Every single line field on every screen is this tall, and was before
// there was a second line to have.
const float kOneLineFieldH = 30.0f;

// A wrap that partitions the string exactly, which wrap_lines_uncached
// above deliberately does not: it drops the space it breaks on. A box that
// is only read can afford that, and an editable one cannot. Every index
// from 0 to size() has to sit on exactly one row, or a caret placed by a
// click lands where the text is not. So the breaking space stays at the
// end of the row it broke, drawn as trailing blank that nothing sees.
struct FieldRow {
    size_t begin, end;
};

std::vector<FieldRow> field_rows(float avail, const std::string& text) {
    std::vector<FieldRow> rows;
    size_t begin = 0, last_space = std::string::npos;
    float w = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const float cw = text_width(Font::Body, std::string(1, c));
        // i > begin keeps a row from coming out empty when the box is
        // narrower than one character, which would never end.
        if (w + cw > avail && i > begin) {
            const size_t brk = last_space == std::string::npos ? i : last_space + 1;
            rows.push_back(FieldRow{begin, brk});
            begin = brk;
            last_space = std::string::npos;
            w = text_width(Font::Body, text.substr(begin, i - begin));
        }
        if (c == ' ') last_space = i;
        w += cw;
    }
    rows.push_back(FieldRow{begin, text.size()});
    return rows;
}

}  // namespace

float text_field_height(int lines, bool with_caption) {
    if (lines < 1) lines = 1;
    return kOneLineFieldH + static_cast<float>(lines - 1) * block_line_height() +
           (with_caption ? block_line_height() : 0.0f);
}

namespace {

// The caption strip along the top of a box, drawn once and left there
// while the content scrolls underneath. Returns the height it took, so a
// box with no caption pays nothing for the feature.
float draw_caption(const Rect& r, const std::string& caption) {
    if (caption.empty()) return 0.0f;
    const float h = block_line_height();
    draw_text(Font::Body, r.x + PAD, r.y + PAD + h * 0.7f, caption, palette::text_dim());
    return h;
}

}  // namespace

int text_block_lines(float box_w, const std::string& text) {
    return static_cast<int>(wrap_lines(box_w, text).size());
}

float text_block_height(int lines, bool with_caption) {
    if (lines < 1) lines = 1;
    return static_cast<float>(lines) * block_line_height() + 2 * PAD +
           (with_caption ? block_line_height() : 0.0f);
}

int text_block(const Rect& r, const std::string& text, const GuiInput& in, float& scroll,
               bool dim, const std::string& caption) {
    draw_rect(r.x, r.y, r.w, r.h, palette::panel());
    draw_rect_outline(r.x, r.y, r.w, r.h, palette::border());

    // Everything below is measured against the room left under the
    // caption, so a captioned box centres and scrolls its content the same
    // way an uncaptioned one does, just lower down.
    const float cap_h = draw_caption(r, caption);
    const Rect inner{r.x, r.y + cap_h, r.w, r.h - cap_h};

    const std::vector<std::string>& lines = wrap_lines(r.w, text);
    const float lh = block_line_height();
    const float content_h = static_cast<float>(lines.size()) * lh;

    // Compared against the whole box, not the box minus padding: a single
    // line in a field-height box is taller than the padded interior, and
    // treating that as overflow is what used to push it down far enough
    // for its descenders to touch the bottom edge.
    const float overflow = content_h - inner.h;
    float top;
    if (overflow <= 0) {
        scroll = 0;
        top = inner.y + (inner.h - content_h) * 0.5f;
    } else {
        if (in.scroll_y != 0 && rect_contains(r, in.mouse_x, in.mouse_y))
            scroll -= static_cast<float>(in.scroll_y) * lh;
        if (scroll < 0) scroll = 0;
        if (scroll > overflow + 2 * PAD) scroll = overflow + 2 * PAD;
        top = inner.y + PAD - scroll;
    }

    begin_scissor(inner.x, inner.y, inner.w, inner.h);
    float y = top;
    for (const std::string& line : lines) {
        if (y + lh > inner.y && y < inner.y + inner.h)
            draw_text(Font::Body, r.x + PAD, y + lh * 0.75f, line,
                      dim ? palette::text_dim() : palette::text());
        y += lh;
    }
    end_scissor();
    return static_cast<int>(lines.size());
}

bool text_link(const Rect& r, const std::string& text, const GuiInput& in, bool enabled) {
    const bool kb = enabled && focus_register(r, in);
    const bool hovered = enabled && rect_contains(r, in.mouse_x, in.mouse_y);
    const bool lit = hovered || kb;
    const float lh = text_line_height(Font::Body);
    const float ty = r.y + (r.h + lh * 0.7f) * 0.5f;
    draw_text(Font::Body, r.x, ty, text,
              !enabled ? palette::disabled_text()
                       : (lit ? palette::accent() : palette::text_dim()));
    if (lit) {
        // Under the baseline rather than on it, so descenders are not cut
        // in half by their own underline.
        draw_rect(r.x, ty + 3.0f, text_width(Font::Body, text), 1.0f, palette::accent());
    }
    if (kb) draw_focus_ring(r);
    const bool clicked = hovered && in.mouse_pressed && !click_over_open_popup(in);
    if (clicked) g_click_consumed_this_frame = true;
    return clicked || (kb && in.key_enter);
}

bool toggle(const Rect& r, bool& value, const std::string& text, const GuiInput& in,
            bool enabled) {
    bool changed = false;
    float box_size = r.h;
    Rect box{r.x, r.y, box_size, box_size};
    // Keyed on the box and not on the whole row, so the toggle lights only
    // where the pointer can actually click it.
    WidgetMotion m = enabled ? widget_motion(box, in) : WidgetMotion{};
    const bool kb = enabled && focus_register(box, in);
    float dip = motion_enabled() ? m.press * kDipTravel : 0.0f;
    Rect d{box.x, box.y + dip, box.w, box.h};
    Color bg = !enabled ? palette::disabled_bg() : (value ? palette::accent() : palette::panel());
    if (enabled) {
        bg = value ? hover_lift_accent(bg, m.hover) : hover_lift(bg, m.hover);
        bg = press_sink(bg, m.press);
    }
    draw_rect(d.x, d.y, d.w, d.h, bg);
    draw_rect_outline(d.x, d.y, d.w, d.h, enabled ? hover_border(m.hover) : palette::border());
    if (kb) draw_focus_ring(d);
    // The caption stays put while the box dips, the way the label beside a
    // physical switch does not travel with the switch.
    label(Rect{r.x + box_size + PAD, r.y, r.w - box_size - PAD, r.h}, text, !enabled);
    if (enabled && rect_contains(box, in.mouse_x, in.mouse_y) && in.mouse_pressed &&
        !click_over_open_popup(in)) {
        value = !value;
        changed = true;
        g_click_consumed_this_frame = true;
    }
    if (kb && in.key_enter) {
        value = !value;
        changed = true;
    }
    return changed;
}

bool text_field(const Rect& r, std::string& value, const GuiInput& in, const std::string& allowed,
                 size_t max_len, bool enabled, bool invalid, CaseFold case_fold,
                 const std::string& placeholder, bool center_text, int lines,
                 const std::string& caption) {
    bool changed = false;
    // A keyboard focus on a field is the same thing as the field being the
    // one that types. Left and Right stay navigation between controls
    // rather than moving the caret: the arrows are how a mouseless
    // operator crosses the screen, and a field that swallowed two of them
    // would be a trap. The caret is placed with the pointer.
    const bool kb = enabled && focus_register(r, in);
    if (kb) {
        g_focus = &value;
        g_focus_field = &value;
    }
    bool focused = enabled && g_focus == static_cast<const void*>(&value);

    const bool hit =
        enabled && rect_contains(r, in.mouse_x, in.mouse_y) && !click_over_open_popup(in);
    if (hit && in.mouse_pressed) {
        g_focus = &value;
        g_focus_field = &value;
        focused = true;
        g_click_consumed_this_frame = true;
    }

    FieldEdit& ed = edit_state(value);
    // The value can change under the caret without a keystroke -- a Load
    // rewrites a whole panel, a Clear empties one box -- so every index is
    // brought back inside the string before it is used for anything.
    if (ed.caret > value.size()) ed.caret = value.size();
    if (ed.anchor > value.size()) ed.anchor = value.size();
    if (ed.view_start > value.size()) ed.view_start = value.size();

    const float avail = r.w - 2 * PAD;
    // Centred boxes are the single character plugboard cells, which never
    // overflow, so their text begins wherever centring puts it and
    // view_start stays at 0.
    auto shown_text = [&]() { return value.substr(ed.view_start); };
    auto text_origin = [&]() {
        if (!center_text) return r.x + PAD;
        return r.x + (r.w - text_width(Font::Body, shown_text())) * 0.5f;
    };
    // The character boundary nearest a given x, as an index into value.
    auto index_at_x = [&](double mx) {
        float x = text_origin();
        size_t i = ed.view_start;
        while (i < value.size()) {
            float cw = text_width(Font::Body, std::string(1, value[i]));
            if (mx < static_cast<double>(x) + cw * 0.5) return i;
            x += cw;
            if (x > r.x + r.w - PAD) return i + 1;
            ++i;
        }
        return value.size();
    };

    // A box of more than one line wraps rather than scrolling sideways.
    // Centring is for the single character plugboard cells and means
    // nothing here, so the two never combine.
    const bool multiline = lines > 1 && !center_text;
    const float row_h = block_line_height();
    // The caption is drawn later, with the box, but its height is needed
    // now: everything below measures against the room left under it.
    const float cap_h = caption.empty() ? 0.0f : row_h;
    const Rect inner{r.x, r.y + cap_h, r.w, r.h - cap_h};
    // The rows sit as a block in the middle of that, so two lines with one
    // line of text in them does not hang from the ceiling.
    const float rows_top = inner.y + (inner.h - static_cast<float>(lines) * row_h) * 0.5f;
    std::vector<FieldRow> rows;
    if (multiline) rows = field_rows(avail, value);

    // Which row an index falls on. An index sitting exactly on a row end
    // belongs to the row after it, which is where a caret goes when typing
    // has just pushed a word onto the next line.
    auto row_of = [&](size_t idx) {
        for (size_t i = 0; i + 1 < rows.size(); ++i)
            if (idx < rows[i].end) return i;
        return rows.empty() ? size_t(0) : rows.size() - 1;
    };
    auto index_at_xy = [&](double mx, double my) {
        int vis = static_cast<int>((my - static_cast<double>(rows_top)) / row_h);
        if (vis < 0) vis = 0;
        if (vis > lines - 1) vis = lines - 1;
        size_t ri = ed.first_row + static_cast<size_t>(vis);
        if (ri >= rows.size()) ri = rows.size() - 1;
        float x = r.x + PAD;
        for (size_t i = rows[ri].begin; i < rows[ri].end; ++i) {
            const float cw = text_width(Font::Body, std::string(1, value[i]));
            if (mx < static_cast<double>(x) + cw * 0.5) return i;
            x += cw;
        }
        return rows[ri].end;
    };
    // The rows here are the ones drawn last frame, which is exactly what
    // the operator was aiming at when they clicked.
    auto index_at_pointer = [&](double mx, double my) {
        return multiline ? index_at_xy(mx, my) : index_at_x(mx);
    };

    if (focused) {
        // Click places the caret, and holding and moving selects from
        // there. The press already took the focus above.
        if (hit && in.mouse_pressed) {
            ed.caret = ed.anchor = index_at_pointer(in.mouse_x, in.mouse_y);
            ed.dragging = true;
        }
        if (ed.dragging && in.mouse_held) ed.caret = index_at_pointer(in.mouse_x, in.mouse_y);
        if (!in.mouse_held) ed.dragging = false;

        auto sel_lo = [&]() { return ed.caret < ed.anchor ? ed.caret : ed.anchor; };
        auto sel_hi = [&]() { return ed.caret < ed.anchor ? ed.anchor : ed.caret; };

        if (in.ctrl_held && in.key_letter == 'Z') {
            if (!ed.undo.empty()) {
                ed.redo.push_back(value);
                if (ed.redo.size() > kUndoSteps) ed.redo.erase(ed.redo.begin());
                value = ed.undo.back();
                ed.undo.pop_back();
                ed.caret = ed.anchor = value.size();
                changed = true;
            }
        } else if (in.ctrl_held && in.key_letter == 'Y') {
            if (!ed.redo.empty()) {
                ed.undo.push_back(value);
                if (ed.undo.size() > kUndoSteps) ed.undo.erase(ed.undo.begin());
                value = ed.redo.back();
                ed.redo.pop_back();
                ed.caret = ed.anchor = value.size();
                changed = true;
            }
        } else {
            if (in.key_backspace) {
                if (sel_lo() != sel_hi()) {
                    push_undo(ed, value);
                    const size_t lo = sel_lo();
                    value.erase(lo, sel_hi() - lo);
                    ed.caret = ed.anchor = lo;
                    changed = true;
                } else if (ed.caret > 0) {
                    push_undo(ed, value);
                    value.erase(ed.caret - 1, 1);
                    --ed.caret;
                    ed.anchor = ed.caret;
                    changed = true;
                }
            }
            if (in.key_delete) {
                if (sel_lo() != sel_hi()) {
                    push_undo(ed, value);
                    const size_t lo = sel_lo();
                    value.erase(lo, sel_hi() - lo);
                    ed.caret = ed.anchor = lo;
                    changed = true;
                } else if (ed.caret < value.size()) {
                    push_undo(ed, value);
                    value.erase(ed.caret, 1);
                    changed = true;
                }
            }
            // Control is a shortcut prefix everywhere else in here, so a
            // character that arrived with it held is not text.
            if (!in.ctrl_held) {
                bool first = true;
                for (unsigned int cp : in.typed) {
                    if (cp > 127 || cp < 32) continue;
                    char c = static_cast<char>(cp);
                    if (case_fold == CaseFold::ToLower && c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c - 'A' + 'a');
                    else if (case_fold == CaseFold::ToUpper && c >= 'a' && c <= 'z')
                        c = static_cast<char>(c - 'a' + 'A');
                    if (allowed.find(c) == std::string::npos) continue;
                    // One undo step for a burst of typing in the same
                    // frame, not one per character.
                    if (first) {
                        push_undo(ed, value);
                        if (sel_lo() != sel_hi()) {
                            const size_t lo = sel_lo();
                            value.erase(lo, sel_hi() - lo);
                            ed.caret = ed.anchor = lo;
                            changed = true;
                        }
                        first = false;
                    }
                    if (value.size() >= max_len) continue;
                    value.insert(ed.caret, 1, c);
                    ++ed.caret;
                    ed.anchor = ed.caret;
                    changed = true;
                }
            }
        }
        if (ed.caret > value.size()) ed.caret = value.size();
        if (ed.anchor > value.size()) ed.anchor = value.size();
    }

    // The edit above moved the text out from under the rows worked out
    // before it, so they are laid out again before anything draws.
    if (multiline && changed) rows = field_rows(avail, value);

    // Scroll the window so the caret is inside it. Widths add up exactly:
    // the baked atlas has no kerning.
    if (multiline) {
        ed.view_start = 0;
        const size_t cr = row_of(ed.caret);
        if (cr < ed.first_row) ed.first_row = cr;
        if (cr >= ed.first_row + static_cast<size_t>(lines))
            ed.first_row = cr - static_cast<size_t>(lines) + 1;
        // And no blank rows under the text when the text would fit.
        const size_t max_first = rows.size() > static_cast<size_t>(lines)
                                     ? rows.size() - static_cast<size_t>(lines)
                                     : 0;
        if (ed.first_row > max_first) ed.first_row = max_first;
    } else if (!center_text) {
        if (ed.view_start > ed.caret) ed.view_start = ed.caret;
        while (ed.view_start < ed.caret &&
               text_width(Font::Body, value.substr(ed.view_start, ed.caret - ed.view_start)) >
                   avail)
            ++ed.view_start;
        // And no empty space on the right when the text would fit.
        while (ed.view_start > 0 && text_width(Font::Body, value.substr(ed.view_start - 1)) <= avail)
            --ed.view_start;
    } else {
        ed.view_start = 0;
    }

    // No dip here: a click places a caret rather than working a control,
    // and a field that sank under the pointer would say the wrong thing
    // about what just happened.
    WidgetMotion m = enabled ? widget_motion(r, in) : WidgetMotion{};
    Color bg = !enabled ? palette::disabled_bg() : palette::panel();
    if (enabled && !focused) bg = hover_lift_soft(bg, m.hover);
    draw_rect(r.x, r.y, r.w, r.h, bg);
    Color border = invalid ? palette::border_invalid()
                           : (focused ? palette::accent() : hover_border(m.hover));
    draw_rect_outline(r.x, r.y, r.w, r.h, border, focused ? 2.0f : 1.0f);
    if (kb) draw_focus_ring(r);
    draw_caption(r, caption);

    if (value.empty() && !focused && !placeholder.empty()) {
        if (center_text) {
            float tw = text_width(Font::Body, placeholder);
            label(Rect{r.x + (r.w - tw) * 0.5f, inner.y, r.w, inner.h}, placeholder, true);
        } else {
            label(Rect{r.x + PAD, inner.y, r.w - 2 * PAD, inner.h}, placeholder, true);
        }
    } else if (multiline) {
        // Clipped to the box: a row half scrolled off the top edge is cut
        // rather than drawn over the label above it.
        begin_scissor(inner.x, inner.y, inner.w, inner.h);
        const size_t lo = ed.caret < ed.anchor ? ed.caret : ed.anchor;
        const size_t hi = ed.caret < ed.anchor ? ed.anchor : ed.caret;
        for (int v = 0; v < lines; ++v) {
            const size_t ri = ed.first_row + static_cast<size_t>(v);
            if (ri >= rows.size()) break;
            const float ry = rows_top + static_cast<float>(v) * row_h;
            const std::string line = value.substr(rows[ri].begin, rows[ri].end - rows[ri].begin);
            // A selection is drawn once per row it covers, clipped to the
            // part of the row inside it.
            if (focused && lo != hi && hi > rows[ri].begin && lo < rows[ri].end) {
                const size_t a = (lo > rows[ri].begin ? lo : rows[ri].begin) - rows[ri].begin;
                const size_t b = (hi < rows[ri].end ? hi : rows[ri].end) - rows[ri].begin;
                Color hl = palette::accent();
                hl.a = 0.35f;
                draw_rect(r.x + PAD + text_width(Font::Body, line.substr(0, a)), ry,
                          text_width(Font::Body, line.substr(a, b - a)), row_h, hl);
            }
            draw_text(Font::Body, r.x + PAD, ry + row_h * 0.75f, line,
                      enabled ? palette::text() : palette::text_dim());
        }
        if (focused) {
            const size_t cr = row_of(ed.caret);
            if (cr >= ed.first_row && cr < ed.first_row + static_cast<size_t>(lines)) {
                const size_t off = ed.caret > rows[cr].begin ? ed.caret - rows[cr].begin : 0;
                const float cy = rows_top + static_cast<float>(cr - ed.first_row) * row_h;
                draw_rect(r.x + PAD +
                              text_width(Font::Body, value.substr(rows[cr].begin, off)),
                          cy + 2.0f, 1.5f, row_h - 4.0f, palette::text());
            }
        }
        end_scissor();
    } else {
        std::string shown = shown_text();
        // Trim what runs off the right, so a long value does not draw over
        // whatever sits beside the box.
        if (!center_text) {
            float used = 0;
            size_t fit = 0;
            while (fit < shown.size()) {
                float cw = text_width(Font::Body, std::string(1, shown[fit]));
                if (used + cw > avail) break;
                used += cw;
                ++fit;
            }
            if (fit < shown.size()) shown.resize(fit);
        }
        const float tx =
            center_text ? r.x + (r.w - text_width(Font::Body, shown)) * 0.5f : r.x + PAD;
        if (focused) {
            const size_t lo = ed.caret < ed.anchor ? ed.caret : ed.anchor;
            const size_t hi = ed.caret < ed.anchor ? ed.anchor : ed.caret;
            if (lo != hi) {
                const size_t a = lo > ed.view_start ? lo - ed.view_start : 0;
                const size_t b = hi > ed.view_start ? hi - ed.view_start : 0;
                const size_t ac = a < shown.size() ? a : shown.size();
                const size_t bc = b < shown.size() ? b : shown.size();
                float sx = tx + text_width(Font::Body, shown.substr(0, ac));
                float sw = text_width(Font::Body, shown.substr(ac, bc - ac));
                Color hl = palette::accent();
                hl.a = 0.35f;
                draw_rect(sx, inner.y + 3.0f, sw, inner.h - 6.0f, hl);
            }
        }
        label(Rect{tx, inner.y, r.w, inner.h}, shown, !enabled);
        if (focused) {
            const size_t c = ed.caret > ed.view_start ? ed.caret - ed.view_start : 0;
            const size_t cc = c < shown.size() ? c : shown.size();
            float cx = tx + text_width(Font::Body, shown.substr(0, cc));
            draw_rect(cx, inner.y + 4.0f, 1.5f, inner.h - 8.0f, palette::text());
        }
    }
    return changed;
}

bool clear_focused_field() {
    if (!g_focus_field || g_focus_field->empty()) return false;
    FieldEdit& e = g_edits[g_focus_field];
    push_undo(e, *g_focus_field);
    g_focus_field->clear();
    e.caret = e.anchor = e.view_start = e.first_row = 0;
    return true;
}

bool a_field_has_focus() { return g_focus_field != nullptr; }

bool numeric_field(const Rect& r, std::string& value, const GuiInput& in, size_t max_len,
                    bool enabled, bool invalid, bool center_text) {
    return text_field(r, value, in, "0123456789", max_len, enabled, invalid,
                       CaseFold::None, /*placeholder=*/"", center_text);
}

namespace {

// The name of a row, in its own typeface where it has one and will bake,
// and in the interface face otherwise. Vertically centred off whichever
// face actually draws, since two faces at the same pixel height do not
// share a line height.
void label_in_face(const Rect& r, const std::string& text, const std::string& font_file,
                   bool dim) {
    if (font_file.empty() || !preview_font_ready(font_file)) {
        label(r, text, dim);
        return;
    }
    const Color c = dim ? palette::text_dim() : palette::text();
    const float ty = r.y + (r.h + preview_line_height(font_file) * 0.7f) * 0.5f;
    draw_preview_text(font_file, r.x, ty, text, c);
    if (typeface_draws_latin(font_file)) return;

    // A face that cannot spell its own name gets the name again after it,
    // in the interface face and in brackets, so the row both shows what
    // the face looks like and says what it is. Two typefaces on one line
    // is nothing special here: each draw binds its own atlas.
    const std::string plain = " (" + text + ")";
    const float x = r.x + preview_text_width(font_file, text);
    // Dropped rather than drawn past the end of the row. A name clipped
    // mid-bracket reads as a fault instead of as a name.
    if (x + text_width(Font::Body, plain) > r.x + r.w) return;
    label(Rect{x, r.y, r.x + r.w - x, r.h}, plain, dim);
}

void draw_dropdown_face(const DropdownFace& f) {
    draw_rect(f.box.x, f.box.y, f.box.w, f.box.h, f.bg);
    draw_rect_outline(f.box.x, f.box.y, f.box.w, f.box.h, f.border);
    if (f.ring) draw_focus_ring(f.box);
    label_in_face(Rect{f.box.x + PAD, f.box.y, f.box.w - 2 * PAD - 14, f.box.h}, f.shown,
                  f.font_file, f.dim);
    label(Rect{f.box.x + f.box.w - 16, f.box.y, 14, f.box.h}, f.open ? "^" : "v", f.dim);
}

}  // namespace

void dropdown(const Rect& r, const std::vector<std::string>& options, int& selected, int id,
              int& open_dropdown_id, const GuiInput& in, bool enabled, bool invalid,
              const std::vector<std::string>* item_fonts) {
    bool is_open = enabled && open_dropdown_id == id;

    WidgetMotion m = enabled ? widget_motion(r, in) : WidgetMotion{};
    const bool kb = enabled && focus_register(r, in);
    float dip = motion_enabled() ? m.press * kDipTravel : 0.0f;
    Rect d{r.x, r.y + dip, r.w, r.h};

    Color bg = !enabled ? palette::disabled_bg() : palette::panel();
    if (enabled) {
        bg = hover_lift(bg, m.hover);
        bg = press_sink(bg, m.press);
    }
    // An open list already owns the accent border, so the hover ring has
    // nothing to add there and would only make the two states look alike.
    Color border_color = invalid ? palette::border_invalid()
                                 : (is_open ? palette::accent() : hover_border(m.hover));
    DropdownFace face;
    face.box = d;
    face.bg = bg;
    face.border = border_color;
    face.shown = (selected >= 0 && selected < static_cast<int>(options.size()))
                     ? options[static_cast<size_t>(selected)]
                     : "";
    face.dim = !enabled;
    face.open = is_open;
    face.ring = kb;
    // A list shorter than its options is a caller mistake rather than a
    // case to handle, so the index is checked against both.
    if (item_fonts && selected >= 0 && selected < static_cast<int>(options.size()) &&
        selected < static_cast<int>(item_fonts->size()))
        face.font_file = (*item_fonts)[static_cast<size_t>(selected)];
    // Drawn here as well as again over the open list. Drawing it twice
    // costs one box and two labels and means the face can never be missing
    // for a frame, however a screen manages to change while a list is up.
    draw_dropdown_face(face);

    if (enabled && rect_contains(r, in.mouse_x, in.mouse_y) && in.mouse_pressed &&
        !click_over_open_popup(in)) {
        open_dropdown_id = is_open ? -1 : id;
        g_click_consumed_this_frame = true;
        is_open = !is_open;
    }
    // Enter opens the list, and Enter inside it takes what is highlighted
    // and closes again -- see draw_open_dropdown_popup().
    if (kb && in.key_enter) {
        open_dropdown_id = is_open ? -1 : id;
        is_open = !is_open;
    }

    if (is_open) {
        g_pending.active = true;
        g_pending.id = id;
        g_pending.box = r;
        g_pending.options = &options;
        g_pending.item_fonts = item_fonts;
        g_pending.selected = &selected;
        g_pending.face = face;
    }
}

bool dropdown_popup_open() { return g_popup_drawn_this_frame; }

void draw_open_dropdown_popup(const GuiInput& in, int& open_dropdown_id) {
    if (!g_pending.active || !g_pending.options) return;
    if (g_pending.id != g_popup_scroll_owner) {
        g_popup_scroll = 0.0f;
        g_popup_scroll_owner = g_pending.id;
        g_popup_open_t = 0.0f;
    }

    const auto& options = *g_pending.options;
    const float row_h = g_pending.box.h;
    const float max_visible = 8.0f;
    const float full_h = row_h * std::min<float>(static_cast<float>(options.size()), max_visible);

    // The list rolls out from behind its own button rather than appearing
    // at full length. Easing out rather than in: it leaves at once and
    // settles, which is what a thing being pulled out from somewhere does.
    g_popup_open_t += frame_dt();
    float roll = 1.0f;
    if (motion_enabled()) {
        roll = std::clamp(g_popup_open_t / kPopupRollSeconds, 0.0f, 1.0f);
        roll = 1.0f - (1.0f - roll) * (1.0f - roll);
    }
    const float list_h = full_h * roll;

    // The visible height is also the clickable one, so a row that has not
    // been rolled out to yet cannot be picked.
    Rect popup{g_pending.box.x, g_pending.box.y + g_pending.box.h, g_pending.box.w, list_h};
    g_popup_rect_last = popup;
    g_popup_drawn_this_frame = true;

    float max_scroll = std::max(0.0f, static_cast<float>(options.size()) * row_h - full_h);
    g_popup_scroll -= static_cast<float>(in.scroll_y) * row_h;
    if (g_popup_scroll < 0.0f) g_popup_scroll = 0.0f;
    if (g_popup_scroll > max_scroll) g_popup_scroll = max_scroll;

    // While a list is open the arrows belong to it rather than to the
    // focus, and resolve_focus() stands aside for exactly this. Enter
    // takes whatever is highlighted and closes, Escape closes without
    // taking anything.
    //
    // Only once the list has actually been on screen for a frame. Enter is
    // also what opens a list from the keyboard, and without this the list
    // would answer the very keypress that opened it and shut again in the
    // same frame, which is precisely what it used to do.
    const bool list_was_up = g_popup_shown_last;
    const int sel = *g_pending.selected;
    if (list_was_up && in.key_up && sel > 0) *g_pending.selected = sel - 1;
    if (list_was_up && in.key_down && sel + 1 < static_cast<int>(options.size()))
        *g_pending.selected = sel + 1;
    if (list_was_up && (in.key_up || in.key_down)) {
        // Keep the highlighted row in view, or arrowing past the eighth
        // entry walks the selection somewhere nobody can see.
        const float sel_y = row_h * static_cast<float>(*g_pending.selected);
        if (sel_y < g_popup_scroll) g_popup_scroll = sel_y;
        if (sel_y + row_h > g_popup_scroll + full_h) g_popup_scroll = sel_y + row_h - full_h;
        if (g_popup_scroll < 0.0f) g_popup_scroll = 0.0f;
        if (g_popup_scroll > max_scroll) g_popup_scroll = max_scroll;
    }
    if (list_was_up && (in.key_enter || in.key_escape)) open_dropdown_id = -1;

    draw_rect(popup.x, popup.y, popup.w, popup.h, palette::panel());
    draw_rect_outline(popup.x, popup.y, popup.w, popup.h, palette::accent());

    bool clicked_inside = false;
    // Rows are only skipped when *entirely* outside the popup — a row
    // scrolled by a fraction of its height is still partially inside and
    // must draw, but without a scissor clip that partial draw renders at
    // full size and bleeds past the popup's own edges into whatever sits
    // just above/below it (the closed box itself, or the next widget
    // down). begin_scissor crops that overflow to the popup rect.
    begin_scissor(popup.x, popup.y, popup.w, popup.h);
    for (size_t i = 0; i < options.size(); ++i) {
        float row_y = popup.y + row_h * static_cast<float>(i) - g_popup_scroll;
        if (row_y + row_h < popup.y || row_y > popup.y + popup.h) continue;  // fully scrolled off-screen

        Rect row{popup.x, row_y, popup.w, row_h};
        bool hovered = rect_contains(row, in.mouse_x, in.mouse_y) && rect_contains(popup, in.mouse_x, in.mouse_y);
        // The chosen row is marked as well as the hovered one now. Without
        // it a list arrowed through from the keyboard gives no sign of
        // where in itself you are.
        if (hovered) draw_rect(row.x, row.y, row.w, row.h, palette::border());
        else if (static_cast<int>(i) == *g_pending.selected)
            draw_rect(row.x, row.y, row.w, row.h, mix(palette::panel(), palette::border(), 0.5f));
        std::string row_font;
        if (g_pending.item_fonts && i < g_pending.item_fonts->size())
            row_font = (*g_pending.item_fonts)[i];
        label_in_face(Rect{row.x + PAD, row.y, row.w - 2 * PAD, row.h}, options[i], row_font,
                      false);
        if (hovered && in.mouse_pressed) {
            *g_pending.selected = static_cast<int>(i);
            open_dropdown_id = -1;
            clicked_inside = true;
        }
    }
    end_scissor();

    if (max_scroll > 0.0f && popup.h > 0.0f) {
        // A minimal scrollbar thumb on the right edge — enough to signal
        // "there's more below" without a full scrollbar widget.
        float thumb_h = std::max(12.0f, popup.h * (full_h / (static_cast<float>(options.size()) * row_h)));
        float thumb_y = popup.y + (popup.h - thumb_h) * (g_popup_scroll / max_scroll);
        draw_rect(popup.x + popup.w - 4, thumb_y, 3, thumb_h, palette::accent());
    }

    // The button goes back on top of its own list, which is the whole of
    // what makes the list read as coming out from behind it rather than
    // over it.
    draw_dropdown_face(g_pending.face);

    if (in.mouse_pressed && !clicked_inside && !rect_contains(g_pending.box, in.mouse_x, in.mouse_y)) {
        // Click landed outside the box and outside the popup — close it.
        if (!rect_contains(popup, in.mouse_x, in.mouse_y)) open_dropdown_id = -1;
    }
}

// ── scrolling ───────────────────────────────────────────────────────────

namespace {

constexpr float kScrollStep = 48.0f;
constexpr float kScrollBarW = 4.0f;

float scroll_span(float top, float height, float content_height) {
    float view_h = height - top;
    return content_height > view_h ? content_height - view_h : 0.0f;
}

}  // namespace

float begin_scroll_region(float top, float width, float height, float& scroll,
                          float content_height, const GuiInput& in) {
    const float max_scroll = scroll_span(top, height, content_height);

    // Only while the pointer is actually over the region, so a wheel event
    // meant for something else does not move the page underneath it.
    if (in.scroll_y != 0.0 && in.mouse_y >= top)
        scroll -= static_cast<float>(in.scroll_y) * kScrollStep;

    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0.0f) scroll = 0.0f;

    begin_scissor(0.0f, top, width, height - top);
    return top - scroll;
}

void end_scroll_region(float top, float width, float height, float scroll,
                       float content_height) {
    end_scissor();

    const float max_scroll = scroll_span(top, height, content_height);
    if (max_scroll <= 0.0f) return;  // everything fits; no bar to draw

    const float view_h = height - top;
    const float track_x = width - kScrollBarW - 2.0f;
    draw_rect(track_x, top, kScrollBarW, view_h, palette::disabled_bg());

    float thumb_h = std::max(24.0f, view_h * (view_h / content_height));
    float thumb_y = top + (view_h - thumb_h) * (scroll / max_scroll);
    draw_rect(track_x, thumb_y, kScrollBarW, thumb_h, palette::accent());
}

// ── modals ──────────────────────────────────────────────────────────────

namespace {

constexpr float kModalW = 460.0f;
constexpr float kModalPad = 22.0f;
constexpr float kModalBtnW = 130.0f;
constexpr float kModalBtnH = 32.0f;

constexpr float kModalBodyLine = 24.0f;

// A modal body used to be drawn as one unwrapped line, so anything much
// past fifty characters ran off the box and onto the dimmed backdrop
// behind it. It wraps now and the box grows to what it has to say, which
// is what lets a body carry two facts instead of one.
//
// wrap_lines keeps an inner pad of its own, so the text sits a little
// further in than the box padding alone would put it. That is a margin
// rather than a mistake, and it is what keeps a long word clear of the
// border.
std::vector<std::string> modal_body_lines(const std::string& body) {
    return wrap_lines(kModalW - 2 * kModalPad, body);
}

// One body line gives exactly the height every modal in here had before
// wrapping existed, so nothing that already fitted has moved.
float modal_box_height(int body_lines) {
    return kModalPad * 2 + 34.0f + static_cast<float>(body_lines) * kModalBodyLine + 18.0f +
           kModalBtnH;
}

// The dimmed sheet plus the box, shared by both modal shapes. Returns the
// box so the caller can lay its own contents out inside it.
Rect draw_modal_frame(float screen_w, float screen_h, const std::string& title,
                      const std::vector<std::string>& body_lines, float box_h) {
    begin_modal_layer();
    // The screen behind has already been drawn by the caller; this greys
    // it out so it reads as out of reach rather than merely unresponsive.
    draw_rect(0, 0, screen_w, screen_h, rgba(0.0f, 0.0f, 0.0f, 0.55f));

    Rect box{(screen_w - kModalW) * 0.5f, (screen_h - box_h) * 0.5f, kModalW, box_h};
    draw_rect(box.x, box.y, box.w, box.h, palette::panel());
    draw_rect_outline(box.x, box.y, box.w, box.h, palette::border());

    float y = box.y + kModalPad;
    label(Rect{box.x + kModalPad, y, box.w - 2 * kModalPad, 26.0f}, title, false, Font::BodyLarge);
    y += 34.0f;
    for (const std::string& line : body_lines) {
        label(Rect{box.x + kModalPad, y, box.w - 2 * kModalPad, kModalBodyLine}, line, true);
        y += kModalBodyLine;
    }
    return box;
}

}  // namespace

ModalChoice modal_question(float screen_w, float screen_h, const std::string& title,
                           const std::string& body, const std::string& confirm_text,
                           const std::string& cancel_text, const GuiInput& in) {
    const std::vector<std::string> body_lines = modal_body_lines(body);
    const float box_h = modal_box_height(static_cast<int>(body_lines.size()));
    Rect box = draw_modal_frame(screen_w, screen_h, title, body_lines, box_h);

    float by = box.y + box_h - kModalPad - kModalBtnH;
    // Confirm on the right, the way a dialog that can lose you something
    // should read: the default reading order puts the safe answer first.
    Rect cancel_r{box.x + kModalPad, by, kModalBtnW, kModalBtnH};
    Rect confirm_r{box.x + box.w - kModalPad - kModalBtnW, by, kModalBtnW, kModalBtnH};

    bool cancel = button(cancel_r, cancel_text, in, true);
    bool confirm = button(confirm_r, confirm_text, in, true, true);
    end_modal_layer();

    if (in.key_enter) confirm = true;
    if (in.key_escape) cancel = true;

    if (confirm) return ModalChoice::Confirm;
    if (cancel) return ModalChoice::Cancel;
    return ModalChoice::None;
}

bool modal_notice(float screen_w, float screen_h, const std::string& title,
                  const std::string& body, const GuiInput& in) {
    const std::vector<std::string> body_lines = modal_body_lines(body);
    const float box_h = modal_box_height(static_cast<int>(body_lines.size()));
    Rect box = draw_modal_frame(screen_w, screen_h, title, body_lines, box_h);

    float by = box.y + box_h - kModalPad - kModalBtnH;
    Rect ok_r{box.x + box.w - kModalPad - kModalBtnW, by, kModalBtnW, kModalBtnH};
    bool ok = button(ok_r, "OK", in, true, true);
    end_modal_layer();
    return ok || in.key_enter || in.key_escape;
}

// -- tooltips ------------------------------------------------------------

const float kTooltipDelay = 1.0f;

namespace {

// How long the tooltip takes to fade up once the wait is over. Short
// enough that it reads as already there by the time the eye arrives.
constexpr float kTooltipFade = 0.12f;

constexpr float kTooltipPad = 8.0f;
// The gap between a control and its tooltip, so the two read as separate
// things rather than as one taller control.
constexpr float kTooltipGap = 6.0f;

}  // namespace

void tooltip(const Rect& r, const std::string& text, const GuiInput& in) {
    if (text.empty()) return;
    // Nothing special is needed to keep a tooltip from surfacing under a
    // modal: gui.cpp draws the screen behind one with the pointer moved
    // off the window, so no control back there is hovered at all.
    WidgetMotion m = widget_motion(r, in);
    if (m.hovered_for < kTooltipDelay) return;
    g_tooltip.active = true;
    g_tooltip.anchor = r;
    g_tooltip.text = text;
    g_tooltip.shown_for = m.hovered_for - kTooltipDelay;
}

void draw_pending_tooltip(float screen_w, float screen_h) {
    if (!g_tooltip.active) return;

    float tw = text_width(Font::Body, g_tooltip.text);
    float th = text_line_height(Font::Body);
    float w = tw + kTooltipPad * 2.0f;
    float h = th + kTooltipPad * 2.0f;

    // Under the control and aligned with its left edge, so it holds still
    // while the pointer wanders about inside the control. Flipped above
    // when there is no room below, and pulled back inside the window when
    // a control near the right edge would otherwise push it out.
    float x = g_tooltip.anchor.x;
    float y = g_tooltip.anchor.y + g_tooltip.anchor.h + kTooltipGap;
    if (y + h > screen_h) y = g_tooltip.anchor.y - kTooltipGap - h;
    if (x + w > screen_w) x = screen_w - w;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;

    float a = motion_enabled() ? std::clamp(g_tooltip.shown_for / kTooltipFade, 0.0f, 1.0f) : 1.0f;

    // Lifted off the panel colour rather than drawn in it, because the
    // thing a tooltip most often sits on is a panel, and a box the same
    // colour as its background is only an outline.
    Color bg = mix(palette::panel(), palette::border(), 0.30f);
    bg.a = a;
    Color edge = palette::border();
    edge.a = a;
    Color fg = palette::text();
    fg.a = a;

    draw_rect(x, y, w, h, bg);
    draw_rect_outline(x, y, w, h, edge);
    draw_text(Font::Body, x + kTooltipPad, y + (h + th * 0.7f) * 0.5f, g_tooltip.text, fg);
}

}  // namespace gui
}  // namespace inop
