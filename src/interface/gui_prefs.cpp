#include "gui_prefs.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

// Only for effective_theme(), which has to ask Windows which way the
// system theme is set. Nothing else in this file touches the platform.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#endif

namespace inop {
namespace gui {

const char* const kPrefsPath = "inop.gui.json";

namespace {

// Written as names rather than integers so the file stays legible and a
// future reordering of the enums cannot silently reinterpret a saved
// value as a different setting.
const char* theme_name(Theme t) {
    switch (t) {
        case Theme::Light: return "light";
        case Theme::Dark: return "dark";
        default: return "system";
    }
}

// An older preferences file holds "dark" or "light" and keeps meaning
// exactly what it did. Only an unrecognised value lands on System, which
// is also what a fresh install gets.
Theme theme_from(const std::string& s) {
    if (s == "light") return Theme::Light;
    if (s == "dark") return Theme::Dark;
    return Theme::System;
}

const char* colourblind_name(ColourblindMode m) {
    switch (m) {
        case ColourblindMode::Protanopia: return "protanopia";
        case ColourblindMode::Deuteranopia: return "deuteranopia";
        case ColourblindMode::Tritanopia: return "tritanopia";
        case ColourblindMode::Achromatopsia: return "achromatopsia";
        default: return "full";
    }
}

ColourblindMode colourblind_from(const std::string& s) {
    if (s == "protanopia") return ColourblindMode::Protanopia;
    if (s == "deuteranopia") return ColourblindMode::Deuteranopia;
    if (s == "tritanopia") return ColourblindMode::Tritanopia;
    if (s == "achromatopsia") return ColourblindMode::Achromatopsia;
    // The names these modes were saved under before they were given their
    // clinical ones. Read but never written, so a preferences file written
    // by an older build keeps working instead of silently reverting to
    // full colour. "red-green" becomes deuteranopia, the commoner of the
    // two it used to cover. "red-blue" and "blue-green" describe no
    // clinical type and have no successor, so they fall through. So does
    // "off", which is what full colour was called before it was named for
    // what it is rather than for what it is not.
    if (s == "red-green") return ColourblindMode::Deuteranopia;
    if (s == "monochrome") return ColourblindMode::Achromatopsia;
    return ColourblindMode::Full;
}

const char* window_mode_name(WindowMode m) {
    switch (m) {
        case WindowMode::BorderlessFullscreen: return "borderless";
        case WindowMode::Fullscreen: return "fullscreen";
        default: return "windowed";
    }
}

WindowMode window_mode_from(const std::string& s) {
    if (s == "borderless") return WindowMode::BorderlessFullscreen;
    if (s == "fullscreen") return WindowMode::Fullscreen;
    return WindowMode::Windowed;
}

std::string system_fonts_dir() {
    const char* windir = std::getenv("WINDIR");
    return windir ? std::string(windir) + "\\Fonts\\" : std::string("C:\\Windows\\Fonts\\");
}

// The faces that travel with the program rather than with the operating
// system, because Crimson Pro and SGA are on no machine by default. The
// path is relative, so it resolves against the working directory — the
// same rule kPrefsPath already follows, and the same limitation with it:
// launched from elsewhere, neither one is found.
const char* const kBundledFontsDir = "fonts/";

bool readable(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

}  // namespace

std::string font_path(const std::string& file) {
    const std::string bundled = std::string(kBundledFontsDir) + file;
    if (readable(bundled)) return bundled;
    const std::string installed = system_fonts_dir() + file;
    if (readable(installed)) return installed;
    return std::string();
}

const char* bundled_fonts_dir() { return kBundledFontsDir; }

Theme effective_theme(Theme t) {
    if (t != Theme::System) return t;
#if defined(_WIN32)
    // The value Windows itself uses for application chrome, as opposed to
    // the separate one for the taskbar and Start. Nonzero means light.
    // Read on every call rather than cached, so changing the system theme
    // while INOP is running is picked up the next time the palette is set
    // instead of needing a restart.
    DWORD light = 0;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light,
                     &size) == ERROR_SUCCESS)
        return light ? Theme::Light : Theme::Dark;
#endif
    return Theme::Dark;
}

namespace {

// The name shown for a file nobody named for us: the filename with its
// extension taken off. Underscores read as word breaks, since that is what
// a font file uses where a name has a space.
std::string name_from_filename(const std::string& file) {
    std::string out = file.substr(0, file.find_last_of('.'));
    for (char& c : out)
        if (c == '_') c = ' ';
    return out;
}

bool is_font_filename(const std::string& file) {
    const std::size_t dot = file.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = file.substr(dot);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext == ".ttf" || ext == ".otf";
}

std::vector<FontChoice> g_fonts;
// Font files found in the folder that were turned away for having no
// licence beside them. Kept so the settings row can name them rather than
// leaving the operator to wonder why the file they just copied in is not
// on the list.
std::vector<std::string> g_unlicensed;
bool g_fonts_scanned = false;

}  // namespace

void refresh_available_fonts() {
    // The operator list, in the operator order. The first two come with
    // Windows and the next two come with INOP. Harlow is a Microsoft face,
    // neither bundled nor guaranteed, so it appears only where it is
    // installed. Grandview was asked for and then dropped: it is a
    // Microsoft font that may not be redistributed, so bundling it was
    // never open to us.
    const FontChoice candidates[] = {
        {"Courier New", "cour.ttf"},
        {"Times New Roman", "times.ttf"},
        {"Crimson Pro", "CrimsonPro.ttf"},
        {"SGA", "sga-all-characters.otf"},
        {"Harlow Solid Italic", "HARLOWSI.TTF"},
    };
    g_fonts.clear();
    for (const FontChoice& c : candidates)
        if (!font_path(c.file).empty()) g_fonts.push_back(c);

    // Anything else the operator has dropped into the bundled folder, in
    // whatever order the filesystem gives them, after the named list. The
    // scan is what makes "add your own" mean anything: without it the
    // entry would open a folder that no amount of copying into could
    // change what the list offers.
    //
    // A face found this way is only offered with its licence beside it,
    // named for the font: MyFont.ttf needs MyFont-license.txt. Every font
    // worth having states terms, most of them require the licence to be
    // distributed with the file, and a program that offered a face it had
    // no licence for would be putting the operator in the wrong. The five
    // named above are exempt because their licences ship with INOP.
    g_unlicensed.clear();
    std::error_code ec;
    std::filesystem::directory_iterator it(kBundledFontsDir, ec);
    if (ec) return;
    for (const std::filesystem::directory_entry& e : it) {
        if (!e.is_regular_file(ec)) continue;
        const std::string file = e.path().filename().string();
        if (!is_font_filename(file)) continue;
        bool already = false;
        for (const FontChoice& c : g_fonts)
            if (c.file == file) already = true;
        if (already) continue;
        if (!readable(std::string(kBundledFontsDir) + licence_filename(file))) {
            g_unlicensed.push_back(file);
            continue;
        }
        g_fonts.push_back(FontChoice{name_from_filename(file), file});
    }
}

const std::vector<std::string>& unlicensed_font_files() { return g_unlicensed; }

std::string licence_filename(const std::string& font_file) {
    return font_file.substr(0, font_file.find_last_of('.')) + "-license.txt";
}

const std::vector<FontChoice>& available_fonts() {
    if (!g_fonts_scanned) {
        g_fonts_scanned = true;
        refresh_available_fonts();
    }
    return g_fonts;
}

void open_bundled_fonts_folder() {
#if defined(_WIN32)
    // The folder is opened, not a file run, so there is nothing here that
    // could execute anything the operator put in it.
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::absolute(kBundledFontsDir, ec);
    if (ec) return;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

const std::vector<int>& zoom_steps() {
    // Dense near 100 and sparse at the extremes, which is what browsers and
    // Windows both ship: 25 points is a small relative change at 200% and a
    // large one at 75%, so uniform steps waste entries where they are least
    // useful. Every value here is reachable; the list stops where the
    // layouts stop.
    static const std::vector<int> v{50, 70, 80, 90, 100, 110, 120, 135, 150, 175, 200};
    return v;
}

// Raised from 125 to 175 once the settings and maintenance screens learned
// to scroll, and from 175 to 200 once the setup screen did too. That last
// one was the binding constraint: its header and top row are a fixed 300
// logical pixels, and the bottom row took whatever was left, so the higher
// the scale the less room the rotor rows had. The bottom row is laid out at
// its natural height now and everything under the header scrolls, so the
// fixed region can no longer squeeze anything off the screen.
//
// 200 is where the list stops rather than where the layouts do, because 200
// is the number SC 1.4.4 asks for and there is no demand past it. The
// ceiling and zoom_steps() must agree: a value offered but refused makes the
// control lie about what it can do.
const int kMaxSupportedZoom = 200;

bool operator==(const GuiPrefs& a, const GuiPrefs& b) {
    return a.theme == b.theme && a.colourblind == b.colourblind &&
           a.window_mode == b.window_mode && a.font_file == b.font_file &&
           a.zoom_percent == b.zoom_percent && a.reduced_motion == b.reduced_motion &&
           a.tutorial_done == b.tutorial_done && a.tutorial_section == b.tutorial_section &&
           a.tutorial_launches == b.tutorial_launches;
}

bool load_prefs(GuiPrefs& p, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    GuiPrefs read;
    if (j.contains("theme") && j["theme"].is_string())
        read.theme = theme_from(j["theme"].get<std::string>());
    if (j.contains("colourblind") && j["colourblind"].is_string())
        read.colourblind = colourblind_from(j["colourblind"].get<std::string>());
    if (j.contains("window_mode") && j["window_mode"].is_string())
        read.window_mode = window_mode_from(j["window_mode"].get<std::string>());
    // A font the machine does not have would leave the window with no text
    // at all, so a saved name that is no longer installed falls back to the
    // default rather than being taken at its word.
    if (j.contains("font") && j["font"].is_string()) {
        std::string file = j["font"].get<std::string>();
        for (const FontChoice& c : available_fonts())
            if (c.file == file) read.font_file = file;
    }
    // Only a value the control could actually have produced is accepted:
    // a hand-edited 900 would otherwise scale the interface past anything
    // usable with no way back to the settings screen to undo it.
    if (j.contains("zoom") && j["zoom"].is_number_integer()) {
        int z = j["zoom"].get<int>();
        for (int step : zoom_steps())
            if (step == z) read.zoom_percent = z;
    }

    // A preferences file written before this setting existed has no key
    // at all, and full motion is exactly what that build did, so the
    // default is also the honest reading of an older file.
    if (j.contains("reduced_motion") && j["reduced_motion"].is_boolean())
        read.reduced_motion = j["reduced_motion"].get<bool>();

    // Nested rather than three flat keys, because the three only mean
    // anything together. A file written before the tutorial existed has
    // no object here and reads as a tutorial never started, which is the
    // truth about that file.
    if (j.contains("tutorial") && j["tutorial"].is_object()) {
        const nlohmann::json& t = j["tutorial"];
        if (t.contains("done") && t["done"].is_boolean()) read.tutorial_done = t["done"].get<bool>();
        if (t.contains("section") && t["section"].is_number_integer()) {
            const int sec = t["section"].get<int>();
            // Clamped rather than trusted: a hand-edited 9 would index
            // past the end of the step table.
            read.tutorial_section = sec < 0 ? 0 : (sec > 2 ? 2 : sec);
        }
        if (t.contains("launches") && t["launches"].is_number_integer()) {
            const int n = t["launches"].get<int>();
            read.tutorial_launches = n < 0 ? 0 : n;
        }
    }

    p = read;
    return true;
}

bool save_prefs(const GuiPrefs& p, const std::string& path) {
    nlohmann::json j;
    j["theme"] = theme_name(p.theme);
    j["colourblind"] = colourblind_name(p.colourblind);
    j["window_mode"] = window_mode_name(p.window_mode);
    j["font"] = p.font_file;
    j["zoom"] = p.zoom_percent;
    j["reduced_motion"] = p.reduced_motion;
    j["tutorial"] = {{"done", p.tutorial_done},
                     {"section", p.tutorial_section},
                     {"launches", p.tutorial_launches}};

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2) << "\n";
    return static_cast<bool>(f);
}

}  // namespace gui
}  // namespace inop
