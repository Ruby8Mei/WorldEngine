#include "gui_settings_panel.hpp"

#include <algorithm>
#include <vector>

namespace inop {
namespace gui {

namespace {

constexpr float kMargin = 16.0f;
constexpr float kBtnW = 150.0f;
constexpr float kWideBtnW = 200.0f;
constexpr float kBtnH = 30.0f;
constexpr float kRowH = 30.0f;
constexpr float kRowGap = 8.0f;
constexpr float kSectionGap = 18.0f;
constexpr float kHeadingH = 30.0f;
// The column the whole page is laid out in. It grows with the window
// between these two, because the note beside a locked row is the one thing
// here whose length is not the layouts to choose: the longest of them
// needs 524 pixels in Courier and 362 in Times, against the 250 the
// original fixed 840 column left for it, so every note ran past its own
// box in every typeface. The upper bound keeps the page from stretching
// into a line too long to read on a wide monitor.
constexpr float kColWMin = 840.0f;
constexpr float kColWMax = 1150.0f;
constexpr float kLabelW = 250.0f;
constexpr float kCtrlW = 300.0f;
constexpr float kGap = 20.0f;

// Set once at the top of frame() from the real window width, so the
// helpers below can lay a row out without every one of them taking a
// width it would only pass along.
float g_col_w = kColWMin;

// What the pinned footer reserves at the bottom of the screen: the gap
// above the button row, the row itself, the status line under it, the link
// row under that, and the bottom margin. Kept as a sum of the same
// constants the footer lays itself out with, so moving any one of them
// cannot leave the reserved height and the drawn height disagreeing.
constexpr float kFooterH = kSectionGap + kBtnH + 6.0f + kRowH + 4.0f + kRowH + kMargin;

// Stable within one frame and unique across the panel, which is all
// dropdown() asks of them.
constexpr int kIdColourblind = 1;
constexpr int kIdFontSize = 2;
constexpr int kIdDisplayMode = 3;
constexpr int kIdFont = 4;
constexpr int kIdTheme = 5;
constexpr int kIdLanguage = 6;
constexpr int kIdZoom = 7;
constexpr int kIdScript = 8;

// The clinical names, each with the plain meaning after it: the operator
// who knows their diagnosis finds it by name, and the operator who does
// not can still tell which one describes them.
const std::vector<std::string>& colourblind_options() {
    static const std::vector<std::string> v{"Full", "Protanopia (red weak)",
                                            "Deuteranopia (green weak)",
                                            "Tritanopia (blue weak)",
                                            "Achromatopsia (greyscale)"};
    return v;
}

// Named for what it sets. The old label said Resolution mode, which
// promised a resolution picker this control has never had and is not
// getting: a borderless window takes the resolution of the desktop.
const std::vector<std::string>& display_mode_options() {
    static const std::vector<std::string> v{"Fullscreen", "Borderless fullscreen", "Windowed"};
    return v;
}

// System setting leads and is the default: an operator who has already
// told Windows which way they want it should not have to say so twice.
const std::vector<std::string>& theme_options() {
    static const std::vector<std::string> v{"System setting", "Light", "Dark"};
    return v;
}

const std::vector<std::string>& font_size_options() {
    static const std::vector<std::string> v{"Small", "Normal", "Large"};
    return v;
}

// Zoom scales the whole interface, font size scales only the text, which
// is why they are two settings and not one. Percentages rather than
// Small/Normal/Large because a literal zoom has a literal factor. Built
// from zoom_steps() so the labels and the stored values cannot drift.
const std::vector<std::string>& zoom_options() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (int step : zoom_steps()) out.push_back(std::to_string(step) + "%");
        return out;
    }();
    return v;
}

int zoom_at(int idx) {
    const std::vector<int>& steps = zoom_steps();
    if (idx < 0 || idx >= static_cast<int>(steps.size())) return 100;
    return steps[static_cast<size_t>(idx)];
}

int index_of_zoom(int percent) {
    const std::vector<int>& steps = zoom_steps();
    int fallback = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i] == percent) return static_cast<int>(i);
        if (steps[i] == 100) fallback = static_cast<int>(i);
    }
    return fallback;
}

const std::vector<std::string>& language_options() {
    static const std::vector<std::string> v{"English"};
    return v;
}

// The five the roadmap asks for, listed whole rather than trimmed to the
// one that works, so the row says what is coming as well as what is here.
// Latin is first and is the default.
const std::vector<std::string>& script_options() {
    static const std::vector<std::string> v{"Latin", "Greek", "Cyrillic", "Hebrew", "Hangul"};
    return v;
}

// The last entry, which is not a font. Picking it opens the folder and
// puts the selection back where it was, so the list is one longer than
// available_fonts() and only this index has no face behind it.
const char* const kAddYourOwn = "Add your own...";

// Rebuilt on every call rather than held in a static, because the list of
// faces changes the moment the operator drops a file into the folder and
// the row has to be able to say so without a restart.
std::vector<std::string> font_options() {
    std::vector<std::string> out;
    for (const FontChoice& c : available_fonts()) out.push_back(c.name);
    if (out.empty()) out.push_back("no system fonts found");
    else out.push_back(kAddYourOwn);
    return out;
}

int index_of_font(const std::string& file) {
    const std::vector<FontChoice>& fonts = available_fonts();
    for (size_t i = 0; i < fonts.size(); ++i)
        if (fonts[i].file == file) return static_cast<int>(i);
    return 0;
}

ColourblindMode colourblind_at(int idx) {
    switch (idx) {
        case 1: return ColourblindMode::Protanopia;
        case 2: return ColourblindMode::Deuteranopia;
        case 3: return ColourblindMode::Tritanopia;
        case 4: return ColourblindMode::Achromatopsia;
        default: return ColourblindMode::Full;
    }
}

int index_of_colourblind(ColourblindMode m) {
    switch (m) {
        case ColourblindMode::Protanopia: return 1;
        case ColourblindMode::Deuteranopia: return 2;
        case ColourblindMode::Tritanopia: return 3;
        case ColourblindMode::Achromatopsia: return 4;
        default: return 0;
    }
}

// Most invasive first, least invasive last, which is the order the
// roadmap asks for and the order these three sit in every game options
// screen. Borderless is the default and therefore the middle entry.
WindowMode window_mode_at(int idx) {
    switch (idx) {
        case 0: return WindowMode::Fullscreen;
        case 1: return WindowMode::BorderlessFullscreen;
        default: return WindowMode::Windowed;
    }
}

int index_of_window_mode(WindowMode m) {
    switch (m) {
        case WindowMode::Fullscreen: return 0;
        case WindowMode::BorderlessFullscreen: return 1;
        default: return 2;
    }
}

Theme theme_at(int idx) {
    switch (idx) {
        case 1: return Theme::Light;
        case 2: return Theme::Dark;
        default: return Theme::System;
    }
}

int index_of_theme(Theme t) {
    switch (t) {
        case Theme::Light: return 1;
        case Theme::Dark: return 2;
        default: return 0;
    }
}

// One section heading with the rule under it. Returns the y where the
// first row of the section starts.
float heading(float x, float y, const std::string& text) {
    label(Rect{x, y, g_col_w, kHeadingH}, text, false, Font::BodyLarge);
    float rule_y = y + kHeadingH - 2.0f;
    draw_rect(x, rule_y, g_col_w, 1.0f, palette::border());
    return y + kHeadingH + kRowGap;
}

// The left-hand caption every row carries, dimmed when the row is locked.
void row_label(float x, float y, const std::string& text, bool locked) {
    label(Rect{x, y, kLabelW, kRowH}, text, locked);
}

// The dim explanation to the right of a locked control, saying why it is
// locked rather than leaving the operator to guess.
void row_note(float x, float y, const std::string& text) {
    const Rect r{x + kLabelW + kGap + kCtrlW + kGap, y, g_col_w - kLabelW - kCtrlW - 2 * kGap,
                 kRowH};
    // label() draws its text whatever the rect says, and a wide typeface
    // makes these notes longer than any column could reasonably be: SGA
    // draws the longest of them at over 1600 pixels even after its own
    // size reduction. Clipped rather than shortened, because every other
    // typeface fits inside the widened column and clipping costs those
    // nothing.
    begin_scissor(r.x, r.y, r.w, r.h);
    label(r, text, true);
    end_scissor();
}

Rect control_rect(float x, float y) { return Rect{x + kLabelW + kGap, y, kCtrlW, kRowH}; }

// What the search row accepts. Every label on this screen is letters and
// spaces; the digits and the two marks are here so that a label added
// later does not need this widened before it can be found.
const char* const kSearchChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -/";

std::string fold_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace

bool SettingsPanel::shown(const std::string& label) const {
    if (search_.empty()) return true;
    return fold_lower(label).find(fold_lower(search_)) != std::string::npos;
}

void SettingsPanel::open(const GuiPrefs& current) {
    // A search left over from last time would open the screen already
    // filtered, with nothing on it to say why half the rows are missing.
    search_.clear();
    scroll_ = 0.0f;
    pending_ = current;
    applied_ = current;
    colourblind_idx_ = index_of_colourblind(pending_.colourblind);
    window_mode_idx_ = index_of_window_mode(pending_.window_mode);
    theme_idx_ = index_of_theme(pending_.theme);
    font_idx_ = index_of_font(pending_.font_file);
    zoom_idx_ = index_of_zoom(pending_.zoom_percent);
    open_dropdown_id_ = -1;
    status_.clear();
    status_error_ = false;
    apply_pending_ = false;
}

bool SettingsPanel::take_apply_request(GuiPrefs* out) {
    if (!apply_pending_) return false;
    apply_pending_ = false;
    if (out) *out = pending_;
    applied_ = pending_;
    return true;
}

void SettingsPanel::set_status(const std::string& text, bool error) {
    status_ = text;
    status_error_ = error;
}

void SettingsPanel::frame(const GuiInput& real_in, int width, int height) {
    // A copy, because the k shortcut has to take its own keystroke out of
    // the frame before the box it jumps to could type it.
    GuiInput in = real_in;
    back_clicked_ = false;
    wordmark_clicked_ = false;

    // Set before anything lays a row out, because every helper below reads
    // it rather than being handed the width.
    g_col_w = static_cast<float>(width) - 2.0f * kMargin;
    if (g_col_w < kColWMin) g_col_w = kColWMin;
    if (g_col_w > kColWMax) g_col_w = kColWMax;

    float w = static_cast<float>(width), h = static_cast<float>(height);
    begin_widget_frame();

    // Whatever the popup wrote into the indices last frame becomes the
    // pending preferences before anything reads them, so the Apply button
    // and the enum fields agree with what is on screen.
    pending_.colourblind = colourblind_at(colourblind_idx_);
    pending_.window_mode = window_mode_at(window_mode_idx_);
    pending_.theme = theme_at(theme_idx_);
    pending_.zoom_percent = zoom_at(zoom_idx_);
    const std::vector<FontChoice>& fonts = available_fonts();
    if (!fonts.empty() && font_idx_ >= 0 && font_idx_ < static_cast<int>(fonts.size()))
        pending_.font_file = fonts[static_cast<size_t>(font_idx_)].file;
    if (pending_ != applied_) status_.clear();

    float top = draw_header(in, w);
    float x = std::max(kMargin, (w - g_col_w) * 0.5f);

    // The footer is pinned to the bottom of the screen and sits outside the
    // scroll region, so Apply, Reset and the status line stay reachable
    // however far the page is scrolled.
    //
    // This reverses an earlier call. When the panels learned to scroll, the
    // clamp that used to hold this row down was removed on the grounds that
    // scrolling made it unnecessary and that a pinned row would fight a
    // moving page. Scrolling is what makes the pin necessary instead: the
    // page is now long enough to put its own commit controls out of sight,
    // and the earlier fight only happened because the row was clamped while
    // still living inside the scrolled content. Outside the region there is
    // nothing to fight.
    //
    // Only this screen does it. The other panels commit as you go and have
    // no footer worth protecting.
    const float footer_top = h - kFooterH;

    float start = begin_scroll_region(top, w, footer_top, scroll_, content_h_, in);

    // The search row sits above everything it filters, and is never
    // filtered itself.
    const Rect search_r = control_rect(x, start);
    // Control and F together jump into the box, which is what the roadmap
    // settled on. It replaces a bare k, which could only ever work while
    // the box did not have the focus, since inside the box k is a letter
    // like any other. A shortcut carrying Control has no such problem and
    // fires wherever the focus happens to be. An open list still has first
    // claim on the keyboard, so it is left alone.
    if (open_dropdown_id_ < 0 && in.ctrl_held && in.key_letter == 'F') {
        set_keyboard_focus(search_r);
        // Taken out of the frame so nothing further down answers it too.
        in.key_letter = 0;
    }
    row_label(x, start, "Search", false);
    // Filtering is a view and not a preference, so it never touches
    // pending_ and Apply stays as dark as it was. A changed search does
    // put the page back to the top, because a shorter list can otherwise
    // be left scrolled past its own end.
    if (text_field(search_r, search_, in, kSearchChars, 40, true, false, CaseFold::None,
                   "type to filter"))
        scroll_ = 0.0f;

    float y = start + kRowH;
    const float after_search = y;

    y = draw_accessibility(in, x, y);
    y = draw_graphics(in, x, y);
    y = draw_appearance(in, x, y);
    y = draw_audio(x, y);
    y = draw_interface(in, x, y);

    // Nothing drew, so the operator is looking at an empty page and is
    // owed a reason for it.
    if (y == after_search) {
        label(Rect{x, y + kSectionGap, g_col_w, kRowH}, "No setting has that in its name.", true);
        y += kSectionGap + kRowH;
    }

    // Measured from where the content actually began, so the scroll clamp is
    // right whatever the zoom does to the row heights. The footer is no
    // longer part of it.
    content_h_ = (y + kMargin) - start;

    end_scroll_region(top, w, footer_top, scroll_, content_h_);

    // A rule rather than nothing, so the pinned row reads as a footer and
    // not as a section that happens to have stopped moving.
    draw_rect(0.0f, footer_top, w, 1.0f, palette::border());

    float by = footer_top + kSectionGap;

    bool dirty = pending_ != applied_;
    Rect apply_r{x, by, kBtnW, kBtnH};
    if (button(apply_r, "Apply", in, dirty, true)) apply_pending_ = true;
    // The only tooltip in the interface so far, and here to be looked at
    // rather than because Apply is hard to follow. Everybody knows what
    // Apply does; the point is to see the wait, the placement and the fade
    // on something harmless before deciding where these belong.
    tooltip(apply_r, "Puts these settings in force and saves them to inop.gui.json", in);

    bool at_defaults = pending_ == GuiPrefs{};
    if (button(Rect{x + kBtnW + kRowGap, by, kWideBtnW, kBtnH}, "Reset to default", in,
               !at_defaults)) {
        pending_ = GuiPrefs{};
        colourblind_idx_ = index_of_colourblind(pending_.colourblind);
        window_mode_idx_ = index_of_window_mode(pending_.window_mode);
        theme_idx_ = index_of_theme(pending_.theme);
        font_idx_ = index_of_font(pending_.font_file);
        zoom_idx_ = index_of_zoom(pending_.zoom_percent);
        status_.clear();
    }

    if (!status_.empty()) {
        Rect status_r{x, by + kBtnH + 6.0f, g_col_w, kRowH};
        if (status_error_) {
            draw_rect(status_r.x, status_r.y, g_col_w, kRowH, palette::error_bg());
            draw_text(Font::Body, status_r.x + 6.0f,
                      status_r.y + (kRowH + text_line_height(Font::Body) * 0.7f) * 0.5f, status_,
                      palette::error_text());
        } else {
            label(status_r, status_, true);
        }
    } else if (dirty) {
        label(Rect{x, by + kBtnH + 6.0f, g_col_w, kRowH}, "Not applied yet.", true);
    }

    // Footer. Plain text and nothing else for now, drawn as four separate
    // labels rather than one string so each can become a link later
    // without the row having to be laid out again.
    {
        const char* const items[] = {"License", "Legal", "Donate", "Support"};
        float fy = by + kBtnH + 6.0f + kRowH + 4.0f;
        float fx = x;
        for (const char* item : items) {
            float w_item = text_width(Font::Body, item);
            label(Rect{fx, fy, w_item, kRowH}, item, true);
            fx += w_item + 28.0f;
        }
    }

    // After the region ends, so an open dropdown can overhang it instead of
    // being clipped at the bottom edge.
    draw_open_dropdown_popup(in, open_dropdown_id_);

    // Again after the popup, so a pick made this frame is not left sitting
    // only in the index until the next one.
    pending_.colourblind = colourblind_at(colourblind_idx_);
    pending_.window_mode = window_mode_at(window_mode_idx_);
    pending_.theme = theme_at(theme_idx_);
    pending_.zoom_percent = zoom_at(zoom_idx_);
    if (!fonts.empty() && font_idx_ >= 0 && font_idx_ < static_cast<int>(fonts.size()))
        pending_.font_file = fonts[static_cast<size_t>(font_idx_)].file;

    end_widget_frame(in);
}

float SettingsPanel::draw_header(const GuiInput& in, float width) {
    const float pad = 6.0f;
    // Same shape as every other screen: the way back on the left, the
    // wordmark on the right, what this screen is in between.
    Rect back_r{kMargin, pad, kBtnW, kBtnH};
    if (button(back_r, "Back", in, true)) back_clicked_ = true;

    float word_tw = text_width(Font::Wordmark, "INOP");
    float word_th = text_line_height(Font::Wordmark);
    Rect wordmark_r{width - kMargin - (word_tw + 24.0f), pad, word_tw + 24.0f, word_th + 12.0f};
    if (wordmark_button(wordmark_r, in)) wordmark_clicked_ = true;

    float gap_x0 = back_r.x + back_r.w + 20.0f;
    float gap_x1 = wordmark_r.x - 20.0f;
    float title_tw = text_width(Font::BodyLarge, "Settings");
    float title_x = gap_x0 + std::max(0.0f, (gap_x1 - gap_x0 - title_tw) * 0.5f);
    label(Rect{title_x, pad, title_tw, word_th + 12.0f}, "Settings", false, Font::BodyLarge);

    return pad + word_th + 12.0f + 10.0f;
}

// Every section takes the same shape now: nothing at all when the search
// has taken all of its rows, because a heading over an empty space says
// less than no heading. Each row advances by the same amount and the
// section gives the last gap back, so a hidden row leaves no hole behind.
float SettingsPanel::draw_accessibility(const GuiInput& in, float x, float y) {
    if (!shown("Arachnophobia mode") && !shown("Colour vision") && !shown("Font size")) return y;
    y = heading(x, y + kSectionGap, "Accessibility");

    if (shown("Arachnophobia mode")) {
        row_label(x, y, "Arachnophobia mode", true);
        toggle(control_rect(x, y), arachnophobia_, "nothing in the interface to hide yet", in,
               false);
        y += kRowH + kRowGap;
    }

    if (shown("Colour vision")) {
        row_label(x, y, "Colour vision", false);
        dropdown(control_rect(x, y), colourblind_options(), colourblind_idx_, kIdColourblind,
                 open_dropdown_id_, in, true);
        y += kRowH + kRowGap;
    }

    if (shown("Font size")) {
        row_label(x, y, "Font size", true);
        dropdown(control_rect(x, y), font_size_options(), font_size_idx_, kIdFontSize,
                 open_dropdown_id_, in, false);
        row_note(x, y, "waiting on the panel layouts to scale");
        y += kRowH + kRowGap;
    }

    return y - kRowGap;
}

float SettingsPanel::draw_graphics(const GuiInput& in, float x, float y) {
    if (!shown("Display mode") && !shown("Zoom") && !shown("Reduced motion")) return y;
    y = heading(x, y + kSectionGap, "Graphics");

    if (shown("Display mode")) {
        row_label(x, y, "Display mode", false);
        dropdown(control_rect(x, y), display_mode_options(), window_mode_idx_, kIdDisplayMode,
                 open_dropdown_id_, in, true);
        y += kRowH + kRowGap;
    }

    if (shown("Zoom")) {
        row_label(x, y, "Zoom", false);
        dropdown(control_rect(x, y), zoom_options(), zoom_idx_, kIdZoom, open_dropdown_id_, in,
                 true);
        // Said before Apply rather than only after it, so the refusal is not
        // a surprise. gui.cpp still enforces it — this is the warning, not
        // the check.
        if (zoom_at(zoom_idx_) > kMaxSupportedZoom)
            row_note(x, y, "above " + std::to_string(kMaxSupportedZoom) +
                               "% the panels do not fit a window this size yet");
        y += kRowH + kRowGap;
    }

    if (shown("Reduced motion")) {
        row_label(x, y, "Reduced motion", false);
        toggle(control_rect(x, y), pending_.reduced_motion, "no dips, no fades, nothing travels",
               in, true);
        y += kRowH + kRowGap;
    }

    return y - kRowGap;
}

float SettingsPanel::draw_appearance(const GuiInput& in, float x, float y) {
    if (!shown("Font family") && !shown("App mode")) return y;
    y = heading(x, y + kSectionGap, "Appearance");

    if (shown("Font family")) {
        // Rescanned as the list opens, so a file copied into the folder a
        // moment ago is in the list the operator is about to read. Once
        // per opening and not per frame: it touches the disk.
        if (open_dropdown_id_ != kIdFont) font_list_open_ = false;
        else if (!font_list_open_) {
            font_list_open_ = true;
            refresh_available_fonts();
            font_options_ = font_options();
            font_idx_ = index_of_font(pending_.font_file);
        }
        if (font_options_.empty()) font_options_ = font_options();
        bool have_fonts = !available_fonts().empty();
        row_label(x, y, "Font family", !have_fonts);
        dropdown(control_rect(x, y), font_options_, font_idx_, kIdFont, open_dropdown_id_, in,
                 have_fonts);
        // The last entry is not a face. It shows the folder and hands the
        // row straight back to whatever was selected before, so the pick
        // never has to be undone by hand.
        if (font_idx_ >= static_cast<int>(available_fonts().size())) {
            open_bundled_fonts_folder();
            font_idx_ = index_of_font(pending_.font_file);
            set_status("Put a .ttf or .otf in that folder with its licence beside it, "
                       "then open this list again.",
                       false);
        }
        if (!have_fonts) row_note(x, y, "no usable font file in the system font folder");
        // A file turned away for having no licence is named here. Silence
        // would read as the scan having missed it, and the operator would
        // go looking for a fault instead of for a licence.
        else if (!unlicensed_font_files().empty()) {
            const std::string& f = unlicensed_font_files().front();
            row_note(x, y, f + " needs " + licence_filename(f) + " beside it");
        }
        y += kRowH + kRowGap;
    }

    if (shown("App mode")) {
        row_label(x, y, "App mode", false);
        dropdown(control_rect(x, y), theme_options(), theme_idx_, kIdTheme, open_dropdown_id_, in,
                 true);
        y += kRowH + kRowGap;
    }

    return y - kRowGap;
}

float SettingsPanel::draw_audio(float x, float y) {
    // No rows, so there is no label for a search to match. It goes whole
    // rather than sitting there as a heading over a sentence nobody was
    // looking for.
    if (!search_.empty()) return y;
    y = heading(x, y + kSectionGap, "Audio");
    label(Rect{x, y, g_col_w, kRowH}, "The application makes no sound yet, so there is nothing here.",
          true);
    return y + kRowH;
}

float SettingsPanel::draw_interface(const GuiInput& in, float x, float y) {
    if (!shown("Interface language") && !shown("INOP script")) return y;
    y = heading(x, y + kSectionGap, "Interface");

    if (shown("Interface language")) {
        row_label(x, y, "Interface language", true);
        dropdown(control_rect(x, y), language_options(), language_idx_, kIdLanguage,
                 open_dropdown_id_, in, false);
        row_note(x, y, "locked to English until there are translations");
        y += kRowH + kRowGap;
    }

    if (shown("INOP script")) {
        // Locked on the font and not on the cipher. The baked atlas holds
        // ASCII 32 to 127 and nothing else, so four of these five would
        // draw as a row of blank holes rather than as letters. The row is
        // here, and stays inert, until the atlas can carry them.
        row_label(x, y, "INOP script", true);
        dropdown(control_rect(x, y), script_options(), script_idx_, kIdScript, open_dropdown_id_,
                 in, false);
        row_note(x, y, "the font can only draw latin so far");
        y += kRowH + kRowGap;
    }

    return y - kRowGap;
}

}  // namespace gui
}  // namespace inop
