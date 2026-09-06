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

#include "gui_prefs.hpp"
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
    // Down right now, as opposed to the two edges above. A dip has to hold
    // for as long as the control is held, which neither edge can say.
    bool mouse_held = false;
    std::vector<unsigned int> typed;  // codepoints typed this frame
    bool key_backspace = false;
    // Backspace eats the character before the caret, Delete the one after
    // it. Both eat the selection instead when there is one.
    bool key_delete = false;
    bool key_enter = false;
    bool key_escape = false;
    // The four arrows move the keyboard focus from control to control, by
    // position rather than by draw order -- see resolve_focus() below. An
    // open dropdown takes them for its own list instead.
    bool key_left = false;
    bool key_right = false;
    bool key_up = false;
    bool key_down = false;
    double scroll_y = 0;
    // Held, not pressed: the convention across this interface is that
    // holding Control while doing something that would normally warn you
    // skips the warning. Polled every frame like the mouse button rather
    // than arriving as an event, since what matters is whether it is down
    // at the moment of the click or keypress.
    bool ctrl_held = false;
    // Held, like ctrl_held and for the same reason: what matters is
    // whether it is down at the moment of the keypress, not that it
    // arrived as an event of its own.
    bool shift_held = false;
    // Held, like the two above. Alt is the screen prefix: Alt and a letter
    // goes straight to a screen from wherever the operator is, which is
    // why it is the one modifier no control on any screen reads.
    bool alt_held = false;
    // The letter key pressed this frame, as an uppercase ASCII letter, or
    // 0 for none. Separate from typed, because holding Control suppresses
    // the character event on Windows: Control and F together produce no
    // typed F at all, so a shortcut built on typed could never see one.
    // One field rather than a bool per letter, since only one shortcut can
    // fire in a frame and the roadmap asks for a whole row of them.
    char key_letter = 0;
};

// Called once per frame by gui.cpp, before the screen draws, with the
// REAL input for the frame -- a modal has to be reachable from the
// keyboard too, and the screen behind one is handed neutered input.
//
// Moves the keyboard focus if an arrow was pressed. It works off the rects
// that registered themselves during the previous frame, because a frame
// only knows what is on it once it has drawn: one frame of lag, which is
// the same trade the scroll regions already make and is invisible at any
// refresh rate.
void resolve_focus(const GuiInput& in);

// Whether the keyboard focus is on this rect right now. Drawing a control
// already answers this for the control itself, but a panel that wants to
// answer a key with "put the focus over there" has to ask before it draws.
bool has_keyboard_focus(const Rect& r);

// Puts the keyboard focus on this rect, the way clicking it would. For a
// shortcut that jumps to one named control -- the settings screen answers
// k this way. Takes a rect because that is what focus is keyed on
// everywhere else in here.
void set_keyboard_focus(const Rect& r);

// Whether the keyboard focus is inside `r` right now, rather than exactly
// on it. A tutorial step aimed at a group of boxes -- one plugboard pair
// is two of them -- has to be able to ask about the group.
bool keyboard_focus_inside(const Rect& r);

// -- the focus gate ------------------------------------------------------
//
// Focus Mode, which the first-launch tutorial holds on for the length of
// one step. While a gate is up, only the controls inside one of the gate
// rects answer anything: a click or an Enter aimed anywhere else does
// nothing at all, and the keyboard focus cannot walk out of the gate
// either, because a control outside it stops registering as somewhere the
// focus can land.
//
// Nothing is said about a blocked click. No nudge, no shake, no message.
// The rest of the screen keeps drawing exactly as it did, it simply stops
// answering, which is the whole of what the mode promises.
//
// A gate rect is an area and not one control, so a step can open a whole
// row or a pair of boxes: anything drawn inside one of them is allowed.
// Set it before the screen draws, from rects measured on the previous
// frame -- see set_landmark() below.
void clear_focus_gate();
void add_focus_gate(const Rect& r);
bool focus_gate_on();

// Whether a control drawn at `r` would be shut out right now. Every
// widget asks this for itself, so nothing in the interface needs to call
// it; it is here so the rule can be checked without a window, where no
// widget can be drawn to ask on its behalf.
bool focus_gate_blocks(const Rect& r);

// The tutorial's own bubble is drawn over a screen that is gated, and its
// Skip has to work when nothing else does. Everything drawn between these
// two ignores the gate. Nestable, like a scissor.
void begin_gate_bypass();
void end_gate_bypass();

// -- landmarks -----------------------------------------------------------
//
// Where a named control was drawn. A panel calls set_landmark() as it
// draws the control, and whoever wants to point at it reads it back on
// the next frame with landmark(). One frame of lag, the same trade the
// keyboard focus and the scroll regions already make, and for the same
// reason: a frame cannot know what is on it until it has drawn.
//
// Names are short stable strings, "setup.next" and so on, listed in
// gui_tutorial.cpp, which is the only thing that reads them.
void set_landmark(const char* name, const Rect& r);
bool landmark(const char* name, Rect* out);

// -- modal layers --------------------------------------------------------
//
// A modal owns the keyboard while it is up. modal_question/modal_notice
// below do this for themselves, but a screen that draws its own overlay by
// hand has to say so, or the focus keeps walking around the screen behind
// it and a mouseless operator can never reach the box.
//
// Wrap every draw call the overlay makes. Layers are numbered in the order
// they open and the highest one on the frame takes the focus, so overlays
// that stack -- a delete confirmation over the file list -- work out
// without either of them knowing about the other. Numbering restarts every
// frame, in resolve_focus().
void begin_modal_layer();
void end_modal_layer();

// Whether any modal layer has been opened so far this frame. gui.cpp asks
// after the screen has drawn, so that Escape closes an overlay the screen
// put up rather than leaving the screen out from under it.
bool modal_layer_open();

// Call once at the very start of a frame, before any widget calls.
void begin_widget_frame();
// Call once at the very end of a frame (after every widget, including any
// open dropdown popup, has been drawn) with the REAL input for the frame —
// clears text-field focus if a click landed on nothing focusable.
void end_widget_frame(const GuiInput& in);

// Palette shared across the panel so every file draws in the same voice.
// Every colour drawn anywhere in the GUI comes from here, which is what
// lets the settings screen restyle the whole application by calling
// set_palette() and nothing else.
namespace palette {

// Recomputes every colour below. Called at startup with the stored
// preferences and again whenever the settings screen applies a change.
// Defaults, before any call, are the dark palette with no colourblind
// adjustment.
void set_palette(Theme theme, ColourblindMode mode);

Color background();
Color panel();
Color border();
Color border_invalid();
Color text();
Color text_dim();
Color accent();       // brass/amber — enabled "Next", focus rings
Color on_accent();    // ink for text drawn on top of accent()
Color disabled_bg();
Color disabled_text();
Color error_bg();
Color error_text();
}  // namespace palette

void label(const Rect& r, const std::string& text, bool dim = false, Font font = Font::Body);

// Returns true if clicked this frame.
bool button(const Rect& r, const std::string& text, const GuiInput& in, bool enabled,
            bool accent = false);

// The clickable INOP wordmark, drawn in the Wordmark font with a hover
// fill. Every screen that carries the wordmark draws it through this one
// call. Returns true if clicked this frame.
bool wordmark_button(const Rect& r, const GuiInput& in);

// A word that behaves like a button and looks like the plain text beside
// it: dim at rest, in the accent colour and underlined under the pointer.
// For a footer of words where a box around each one would be four boxes
// too many. Returns true on a click or on Enter while it holds the
// keyboard focus.
bool text_link(const Rect& r, const std::string& text, const GuiInput& in, bool enabled);

// Read-only text wrapped to fit the width of `r`, drawn in a box like a
// field. Breaks at spaces where it can and mid-word where it must, which
// is what a block-grouped ciphertext needs. Content shorter than the box
// is centred vertically; content taller than it scrolls, and `scroll` is
// the caller-owned offset in pixels, moved only while the pointer is over
// the box and clamped here. Returns the number of lines laid out.
// `caption` names the box from inside it, drawn dim along the top and
// never scrolling away, so a screen full of output still says which box is
// which. The content starts below it. Give the Rect the matching height
// from text_block_height().
int text_block(const Rect& r, const std::string& text, const GuiInput& in, float& scroll,
               bool dim = false, const std::string& caption = "");

// How many lines `text` wraps to inside a box `box_w` wide, by the same
// rule text_block lays out with, and the height a box needs to show that
// many. Together they let a caller size a box to its content before
// drawing it.
int text_block_lines(float box_w, const std::string& text);
float text_block_height(int lines, bool with_caption = false);

// The lines themselves, by that same rule. text_block_lines() answers how
// many there are; this answers what they say, for a caller that draws its
// own paragraph rather than putting one in a box -- the tutorial bubble
// and the modals both do.
const std::vector<std::string>& wrap_text(float box_w, const std::string& text);

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
// `lines` is how many rows of text the box shows. At 1 the box is a single
// line that scrolls sideways under the caret. Above 1 the text wraps at the
// box width instead, the rows scroll vertically to follow the caret, and a
// click lands on whichever row it was over. Give the Rect the matching
// height from text_field_height(), or the rows will not fit inside it.
// `fold_marks` sends every keystroke through transform() before it is
// written, so a key the machine alphabet has no room for arrives as the
// code that stands in for it: a-acute becomes a2, a capital A becomes a0.
// It is what lets a box take diacritics and capitals at all, since the
// font atlas can draw neither and the rotors have no key for either.
// Punctuation folds to nothing and is dropped, which is what transform()
// does with it everywhere else. A field whose alphabet cannot hold a
// whole code leaves this false and keeps the plain per-character
// behaviour, `case_fold` included; with it true `case_fold` is ignored,
// because case is carried in the code instead of being folded away.
bool text_field(const Rect& r, std::string& value, const GuiInput& in, const std::string& allowed,
                 size_t max_len, bool enabled, bool invalid, CaseFold case_fold = CaseFold::None,
                 const std::string& placeholder = "", bool center_text = false, int lines = 1,
                 const std::string& caption = "", bool fold_marks = false);

// The height a text_field needs to show `lines` rows. One line answers the
// same 30 pixels every single-line field on every screen already uses, so
// the number is unchanged for all of them and each extra row adds exactly
// one row height on top.
float text_field_height(int lines, bool with_caption = false);

// Empties whichever writable field the operator is in, as one undo step,
// and says whether it emptied anything. For a Clear button that sits away
// from the box it clears and so cannot name it. Nothing happens when no
// field has the focus, or when the one that has it is already empty.
bool clear_focused_field();

// Whether any writable field has the focus. A Clear button asks so that it
// can grey itself out rather than sitting there doing nothing.
bool a_field_has_focus();

// Superfocus: the state a writable field enters the moment a character is
// typed into it. While it holds, the arrow keys move the caret inside that
// field instead of walking the focus between controls, and Escape is what
// gives them back. Nothing else turns it on, so an operator crossing the
// screen with the arrows never falls into it by accident.
bool superfocus_active();

// Whether a field answered Escape by leaving superfocus this frame. gui.cpp
// asks after the screen has drawn, so that the first Escape leaves the
// caret and only the second leaves the screen.
bool superfocus_ate_escape();

// Digits-only convenience wrapper over text_field.
bool numeric_field(const Rect& r, std::string& value, const GuiInput& in, size_t max_len,
                    bool enabled, bool invalid, bool center_text = false);

// Dropdown. `id` must be a small stable integer unique within one panel
// frame (e.g. a row index) — `open_dropdown_id` is shared panel-wide
// state, so only one dropdown is ever open at a time, and it is always the
// one drawn last (topmost), regardless of layout order. A picked item does
// not surface from here at all — it writes into `selected` later, from
// draw_open_dropdown_popup(), since the popup draws after every dropdown()
// call in the frame. That is why there is nothing to return.
//
// `item_fonts`, when given, is a font filename per option, and each row
// draws its own name in that face rather than in the interface face. It
// is what makes the font picker show what it is offering. Nullptr, an
// entry that is empty, and a file that will not bake all fall back to the
// interface face, so a shorter list than `options` is not allowed but a
// blank entry is. The pointer is held until the popup draws later in the
// frame, the same way `options` is, so it has to outlive the frame.
void dropdown(const Rect& r, const std::vector<std::string>& options, int& selected, int id,
              int& open_dropdown_id, const GuiInput& in, bool enabled, bool invalid = false,
              const std::vector<std::string>* item_fonts = nullptr);

// Whether a dropdown list is open on screen. gui.cpp asks so that Escape
// closes the list rather than leaving the screen: with a list open the key
// plainly means the list, and answering both would do two things at once.
bool dropdown_popup_open();

// Draws the popup list for whichever dropdown is currently open (if any),
// on top of everything else, using the frame's REAL (non-neutered) input.
// Call this once, last, every frame.
void draw_open_dropdown_popup(const GuiInput& in, int& open_dropdown_id);

// ── scrolling ───────────────────────────────────────────────────────────
//
// Vertical scrolling for a panel's whole content area, for when the layout
// is taller than the window. Zoom made that routine rather than rare: at
// 150% a panel needs half again the vertical space it was laid out for.
//
// Immediate mode has a chicken and egg here — the content height is only
// known once the content has been laid out — so the caller keeps last
// frame's measurement and hands it back. One frame of lag on the clamp is
// invisible, and the first frame simply cannot scroll.
//
// Returns the y the caller should lay its first row out at: the requested
// top, shifted up by however far the region is scrolled. Everything drawn
// between the two calls is clipped to the region, so content cannot spill
// over the header above it.
float begin_scroll_region(float top, float width, float height, float& scroll,
                          float content_height, const GuiInput& in);

// Ends the region and draws the position indicator down the right edge.
// `content_height` is what the caller measured this frame. Call any
// dropdown popup AFTER this, so a popup can overhang the region rather
// than being clipped by it.
void end_scroll_region(float top, float width, float height, float scroll,
                       float content_height);

// ── modals ──────────────────────────────────────────────────────────────
//
// A modal is drawn over a whole screen, so unlike every other widget here
// it is not the screen's business: gui.cpp owns which modal is open, draws
// the screen underneath with neutered input so nothing behind can be
// clicked, and then calls one of these with the real input. That is why
// these take a screen size rather than a Rect.

enum class ModalChoice { None, Confirm, Cancel };

// A question with two answers. Enter confirms and Escape cancels, so a
// modal raised by a keypress can be answered without reaching for the
// mouse. Returns what was chosen this frame, or None while it waits.
ModalChoice modal_question(float screen_w, float screen_h, const std::string& title,
                           const std::string& body, const std::string& confirm_text,
                           const std::string& cancel_text, const GuiInput& in);

// A statement with nothing to decide. Returns true the frame it is
// dismissed, by the button, by Enter or by Escape.
bool modal_notice(float screen_w, float screen_h, const std::string& title,
                  const std::string& body, const GuiInput& in);

// -- tooltips ------------------------------------------------------------
//
// Call right after drawing the control it belongs to, with that controls
// own rect. Nothing happens until the pointer has rested there for
// kTooltipDelay; after that the text is put aside and drawn later by
// draw_pending_tooltip(), which is what gets it above the controls it
// would otherwise appear behind. The same deferral the dropdown popup
// uses, and simpler than that one, because a tooltip is never clicked and
// so never needs the remembered rect that stops a click landing on
// whatever is underneath.
//
// Drawn text is ASCII 32 to 127 like every other string here: the baked
// atlas has nothing else, and anything outside that range comes out as a
// hole.
void tooltip(const Rect& r, const std::string& text, const GuiInput& in);

// Draws whichever tooltip was asked for this frame, above everything the
// screen drew including an open dropdown popup. Called once, by gui.cpp,
// after the screen frame returns and before any modal, so that a modal
// still covers it.
void draw_pending_tooltip(float screen_w, float screen_h);

// How long the pointer has to rest on a control before its tooltip
// appears. The point of the wait is that somebody who already knows where
// they are going never sees one.
//
// It started at 1.5 seconds, which is what VS Code was timed at. That
// measurement is kept here rather than dropped, because it is where the
// figure came from, but the value in force is 1 second, settled by
// watching this interface run rather than by copying another one. One
// number, here and nowhere else, so it stays easy to move again.
extern const float kTooltipDelay;

}  // namespace gui
}  // namespace inop
