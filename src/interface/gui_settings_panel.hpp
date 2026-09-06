// gui_settings_panel.hpp — the application settings screen, reached from
// the main menu.
//
// Edits GuiPrefs, nothing else. It never repaints the application itself:
// a change stays pending inside this panel until Apply is clicked, at
// which point the new preferences are handed to gui.cpp, which owns the
// window, the font atlases and the palette and is the only place allowed
// to touch them. Leaving the screen with something unapplied discards it.
//
// Several rows are drawn locked on purpose. They are the settings the
// operator has asked for whose subject does not exist yet — there is no
// sound to switch off and no translation to switch to — and showing
// them disabled says that more honestly than leaving them out and more
// honestly than a live control that quietly does nothing.
#pragma once

#include <string>
#include <vector>

#include "gui_prefs.hpp"
#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class SettingsPanel {
public:
    // Loads the screen with the preferences currently in force. Pending
    // edits from a previous visit are dropped, which is what makes
    // leaving the screen a cancel.
    void open(const GuiPrefs& current);

    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

    // True the frame the INOP wordmark was clicked — caller returns to the
    // main menu.
    bool wordmark_clicked() const { return wordmark_clicked_; }

    // True the frame the Replay button in the Help section was clicked.
    // The caller starts the tutorial and leaves this screen; the panel
    // itself knows nothing about what a tutorial is.
    bool replay_tutorial_clicked() const { return replay_tutorial_clicked_; }

    // True the frame the footer word "License" was clicked — caller opens
    // the legal screen. The other three footer words are still plain text
    // with nothing behind them.
    bool license_clicked() const { return license_clicked_; }

    // True the frame Apply was clicked, handing over the preferences to
    // put in force and store. Consumed by the call, like the clipboard
    // requests on the enciphering screen.
    bool take_apply_request(GuiPrefs* out);

    // What actually happened, reported back by whoever applied it, and
    // drawn under the buttons until the next edit. `error` picks the
    // failure colour.
    void set_status(const std::string& text, bool error);

    // Whether an edit is sitting here waiting on Apply. Asked by gui.cpp
    // on the way out: leaving the GUI saves the preferences in force, and
    // it has no way to save one that was never applied, so the quit modal
    // names the loss rather than claiming everything is safe.
    bool has_unapplied_changes() const { return pending_ != applied_; }

private:
    float draw_header(const GuiInput& in, float width);
    // Each draws one section downward from `y` and returns the y just past
    // it, so the sections stack down one column.
    float draw_accessibility(const GuiInput& in, float x, float y);
    float draw_graphics(const GuiInput& in, float x, float y);
    float draw_appearance(const GuiInput& in, float x, float y);
    float draw_audio(float x, float y);
    float draw_interface(const GuiInput& in, float x, float y);
    // One row, above the keyboard list: the tutorial, and a button that
    // plays it again.
    float draw_help(const GuiInput& in, float x, float y);
    // Takes no input: every row is text and nothing on it can be
    // clicked. It is the one section that reports the interface rather
    // than changing it.
    float draw_keyboard(float x, float y);

    // Whether a row carrying this label survives the current search.
    bool shown(const std::string& label) const;

    // Pending is what the controls edit; applied is what was in force when
    // the screen opened or when Apply last succeeded. Apply is only
    // offered when they differ.
    GuiPrefs pending_;
    GuiPrefs applied_;

    // Dropdown selections are indices into the option lists, so they are
    // kept beside the enums rather than derived every frame.
    int colourblind_idx_ = 0;
    int window_mode_idx_ = 0;
    int theme_idx_ = 0;
    int font_idx_ = 0;
    int zoom_idx_ = 0;

    // The font row's own entries, held rather than built where they are
    // needed, because the dropdown keeps a pointer to the list it was
    // given and reads it again when the open popup draws later in the
    // frame. Rebuilt only when the folder is rescanned.
    std::vector<std::string> font_options_;
    // The file behind each of those entries, held for the same reason and
    // rebuilt at the same moments: the dropdown keeps the pointer and
    // reads it again when the popup draws later in the frame.
    std::vector<std::string> font_option_files_;
    // Whether the font list was open on the previous frame, so the folder
    // is rescanned once as it opens and not once per frame while it is up.
    bool font_list_open_ = false;

    // Locked rows still need somewhere for the widget to write, since the
    // widget set takes a reference. Nothing reads these.
    bool arachnophobia_ = false;
    int font_size_idx_ = 1;  // Normal
    int language_idx_ = 0;
    int script_idx_ = 0;  // Latin

    // How far the content is scrolled, and how tall it measured last frame.
    // The second is what the clamp needs and can only be known after a
    // layout pass, so it lags by one frame by construction.
    float scroll_ = 0.0f;
    float content_h_ = 0.0f;

    // What is typed into the search row at the top. Empty means the screen
    // looks exactly as it did before there was a search row. Matched
    // against the row labels only, and never against a note, because a hit
    // has to be a word the operator can see on the left of the row.
    std::string search_;

    int open_dropdown_id_ = -1;
    std::string status_;
    bool status_error_ = false;
    bool apply_pending_ = false;
    bool wordmark_clicked_ = false;
    bool license_clicked_ = false;
    bool replay_tutorial_clicked_ = false;
};

}  // namespace gui
}  // namespace inop
