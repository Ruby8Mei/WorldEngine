#include "gui_prefs.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

// Windows theme lookup, executable location, and folder opening use this
// platform layer.
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

std::vector<std::filesystem::path> system_font_dirs() {
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    return {windir ? std::filesystem::path(windir) / "Fonts"
                   : std::filesystem::path("C:/Windows/Fonts")};
#elif defined(__APPLE__)
    std::vector<std::filesystem::path> dirs{"/System/Library/Fonts", "/Library/Fonts"};
    if (const char* user_home = std::getenv("HOME"))
        dirs.push_back(std::filesystem::path(user_home) / "Library/Fonts");
    return dirs;
#else
    std::vector<std::filesystem::path> dirs{"/usr/share/fonts", "/usr/local/share/fonts"};
    if (const char* data_home = std::getenv("XDG_DATA_HOME"))
        dirs.push_back(std::filesystem::path(data_home) / "fonts");
    if (const char* user_home = std::getenv("HOME")) {
        dirs.push_back(std::filesystem::path(user_home) / ".local/share/fonts");
        dirs.push_back(std::filesystem::path(user_home) / ".fonts");
    }
    return dirs;
#endif
}

// The faces that travel with the program rather than with the operating
// system. The working directory is checked first, then the executable
// directory so installed and build-tree launches find the same bundle.
const char* const kBundledFontsDir = "fonts";

std::filesystem::path executable_dir() {
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && length < path.size())
        return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        return std::filesystem::weakly_canonical(path.data()).parent_path();
#else
    std::vector<char> path(4096);
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length > 0) {
        path[static_cast<size_t>(length)] = '\0';
        return std::filesystem::path(path.data()).parent_path();
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::filesystem::path bundled_fonts_path() {
    const std::filesystem::path local(kBundledFontsDir);
    std::error_code ec;
    if (std::filesystem::is_directory(local, ec)) return local;
    const std::filesystem::path beside = executable_dir() / kBundledFontsDir;
    ec.clear();
    if (std::filesystem::is_directory(beside, ec)) return beside;
    return local;
}

bool readable(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

}  // namespace

std::string font_path(const std::string& file) {
    const std::filesystem::path bundled = bundled_fonts_path() / file;
    if (readable(bundled.string())) return bundled.string();
    for (const std::filesystem::path& dir : system_font_dirs()) {
        const std::filesystem::path direct = dir / file;
        if (readable(direct.string())) return direct.string();
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (!ec && it != end) {
            if (it->is_regular_file(ec) && it->path().filename() == file)
                return it->path().string();
            it.increment(ec);
        }
    }
    return std::string();
}

std::string bundled_fonts_dir() { return bundled_fonts_path().string() + std::string(1, std::filesystem::path::preferred_separator); }

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
    const std::filesystem::path bundled_dir = bundled_fonts_path();
    std::filesystem::directory_iterator it(bundled_dir, ec);
    if (ec) return;
    for (const std::filesystem::directory_entry& e : it) {
        if (!e.is_regular_file(ec)) continue;
        const std::string file = e.path().filename().string();
        if (!is_font_filename(file)) continue;
        bool already = false;
        for (const FontChoice& c : g_fonts)
            if (c.file == file) already = true;
        if (already) continue;
        if (!readable((bundled_dir / licence_filename(file)).string())) {
            g_unlicensed.push_back(file);
            continue;
        }
        g_fonts.push_back(FontChoice{name_from_filename(file), file});
    }
}

const std::vector<std::string>& unlicensed_font_files() { return g_unlicensed; }

std::string licence_filename(const std::string& font_file) {
    if (font_file == "CrimsonPro.ttf") return "crimsonpro-license.txt";
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
    const std::filesystem::path dir = std::filesystem::absolute(bundled_fonts_path(), ec);
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

const std::vector<int>& frame_rate_limits() {
    static const std::vector<int> limits{0, 30, 60, 120, 144, 180};
    return limits;
}

double frame_delay_seconds(int limit, double elapsed) {
    const auto& limits = frame_rate_limits();
    if (limit == 0 || std::find(limits.begin(), limits.end(), limit) == limits.end()) return 0;
    return std::max(0.0, 1.0 / static_cast<double>(limit) - std::max(0.0, elapsed));
}

bool operator==(const GuiPrefs& a, const GuiPrefs& b) {
    return a.theme == b.theme && a.colourblind == b.colourblind &&
           a.window_mode == b.window_mode && a.font_file == b.font_file &&
           a.vsync == b.vsync && a.frame_rate_limit == b.frame_rate_limit &&
           a.zoom_percent == b.zoom_percent && a.reduced_motion == b.reduced_motion &&
           a.audio_muted == b.audio_muted && a.audio_volume == b.audio_volume &&
           a.tutorial_done == b.tutorial_done && a.tutorial_section == b.tutorial_section &&
           a.tutorial_launches == b.tutorial_launches;
}

bool load_prefs(GuiPrefs& p, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    GuiPrefs read;
    if (j.contains("vsync") && j["vsync"].is_boolean())
        read.vsync = j["vsync"].get<bool>();
    if (j.contains("frame_rate_limit") && j["frame_rate_limit"].is_number_integer())
        for (int limit : frame_rate_limits())
            if (j["frame_rate_limit"] == limit) read.frame_rate_limit = limit;
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

    if (j.contains("audio") && j["audio"].is_object()) {
        const nlohmann::json& audio = j["audio"];
        if (audio.contains("muted") && audio["muted"].is_boolean())
            read.audio_muted = audio["muted"].get<bool>();
        if (audio.contains("volume") && audio["volume"].is_number_integer()) {
            const int volume = std::clamp(audio["volume"].get<int>(), 0, 100);
            read.audio_volume = ((volume + 5) / 10) * 10;
        }
    }

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
    j["vsync"] = p.vsync;
    j["frame_rate_limit"] = p.frame_rate_limit;
    j["font"] = p.font_file;
    j["zoom"] = p.zoom_percent;
    j["reduced_motion"] = p.reduced_motion;
    j["audio"] = {{"muted", p.audio_muted}, {"volume", std::clamp(p.audio_volume, 0, 100)}};
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
