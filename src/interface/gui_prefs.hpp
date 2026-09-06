// gui_prefs.hpp — application-wide preferences, as opposed to the machine
// configuration the setup screen edits.
//
// A machine configuration is message material: which rotors, which rings,
// which key. It is saved per configuration under setup/ and travels with
// the message. Nothing here does. These are preferences about the
// application itself — how it looks, which window it opens in, which
// typeface it draws with — so there is exactly one set of them, stored in
// one file next to the executable.
//
// Deliberately knows nothing about GLFW, OpenGL or the widget set: the
// settings screen edits this struct, and gui.cpp is what turns a changed
// field into a re-baked font atlas or a fullscreen window.
#pragma once

#include <string>
#include <vector>

namespace inop {
namespace gui {

// System is a stored preference rather than a palette: it means "whatever
// the operating system is set to", and it is the default because an
// operator who has already told Windows they want dark should not have to
// tell this application separately. It is never a colour. Everything that
// paints goes through effective_theme() below, which resolves it to one of
// the other two before any palette code sees it.
enum class Theme { System, Dark, Light };

// Resolves System by asking the operating system, and returns Dark or
// Light unchanged. On Windows that is the AppsUseLightTheme preference.
// Anywhere else, and on a Windows that will not answer, the fallback is
// Dark, which is this application's own default and the safer of the two
// to be wrong about in a dim room.
Theme effective_theme(Theme t);

// The clinical types of colour vision deficiency, named as an operator
// who knows their own diagnosis would look for them. Each mode picks
// signalling colours that survive that deficiency — see the palette
// tables in gui_widgets.cpp for what each one swaps.
//
// Protanopia and deuteranopia are both red-green deficiencies and take
// the same safe palette. They are listed separately anyway because the
// operator knows which one they have, and offering only a merged
// "red-green" would make them guess whether it applies to them.
enum class ColourblindMode { Full, Protanopia, Deuteranopia, Tritanopia, Achromatopsia };

enum class WindowMode { Windowed, BorderlessFullscreen, Fullscreen };

struct FontChoice {
    std::string name;  // shown in the dropdown
    std::string file;  // filename inside the Windows font directory
};

// Where a font file actually is. The folder bundled with the program is
// searched first and the system font directory second, so a face shipped
// with INOP wins over a same-named one installed on the machine. Comes
// back empty when neither holds it.
std::string font_path(const std::string& file);

// The folder INOP ships fonts in, with its trailing separator. Anything
// bundled with the program is in here and nothing else is, which is what
// makes it the answer to "which font licences do we have to show".
const char* bundled_fonts_dir();

// Every face this build knows how to offer, filtered down to the ones
// font_path() can actually find — a machine missing Harlow should not be
// shown Harlow. Courier New is first and is the default; if even that is
// missing the list comes back empty and the font row has nothing to
// offer.
const std::vector<FontChoice>& available_fonts();

// Rebuilds that list from disk. Called when the font list is opened, so a
// file dropped into the bundled folder while INOP is running is offered
// without a restart.
void refresh_available_fonts();

// Shows the bundled fonts folder in the file manager, creating it if it is
// not there. Does nothing off Windows.
void open_bundled_fonts_folder();

// The licence a font added to the bundled folder has to come with, named
// for the font itself: MyFont.ttf asks for MyFont-license.txt. Without it
// the face is not offered, because most font licences require the terms to
// travel with the file and INOP has no way to agree to terms it cannot
// read.
std::string licence_filename(const std::string& font_file);

// Font files in the bundled folder that were turned away for having no
// licence beside them, filled by the last scan. Named so the settings row
// can say which file needs what rather than staying silent about a file
// the operator can plainly see is there.
const std::vector<std::string>& unlicensed_font_files();

// The preferences that currently do something. Rows the settings screen
// draws locked (arachnophobia mode, font size, audio, interface language)
// are deliberately absent: nothing reads them, so nothing should store
// them either.
struct GuiPrefs {
    Theme theme = Theme::System;
    ColourblindMode colourblind = ColourblindMode::Full;
    WindowMode window_mode = WindowMode::BorderlessFullscreen;
    std::string font_file = "cour.ttf";
    // Whole-interface scale as a percentage, so the stored value reads the
    // same as the control that sets it. Kept as an int rather than a float
    // because it only ever takes the fixed steps the dropdown offers, and
    // a rounded percentage survives a round trip through JSON exactly.
    int zoom_percent = 100;
    // Less motion rather than none. The interface still answers a hover
    // and a press, it just stops travelling to get there: no dip, no
    // fade, nothing that moves position moves. Off by default, because
    // the motion is the point of having built it.
    bool reduced_motion = false;
};

// The steps the zoom control offers, 50 to 250 in 25s.
const std::vector<int>& zoom_steps();

// Above this, the layouts do not fit a normal window and the setting is
// refused with a notice rather than applied. See gui.cpp, which owns that
// policy because it owns the window.
extern const int kMaxSupportedZoom;

bool operator==(const GuiPrefs& a, const GuiPrefs& b);
inline bool operator!=(const GuiPrefs& a, const GuiPrefs& b) { return !(a == b); }

// Where the preferences live. One flat file next to the executable, not a
// folder under setup/, which holds saved machine configurations and is
// browsed as tiles.
extern const char* const kPrefsPath;

// Missing file, unreadable file and unparseable file are all the same
// answer: `p` is left at its defaults and false comes back. A first run
// has no preferences file and that is not an error, so callers ignore the
// return value unless they want to report it.
bool load_prefs(GuiPrefs& p, const std::string& path = kPrefsPath);

bool save_prefs(const GuiPrefs& p, const std::string& path = kPrefsPath);

}  // namespace gui
}  // namespace inop
