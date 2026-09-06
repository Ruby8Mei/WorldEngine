#include "gui_legal_panel.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "gui_prefs.hpp"

namespace inop {
namespace gui {

namespace {

constexpr float kMargin = 16.0f;
constexpr float kTabW = 200.0f;
constexpr float kTabH = 30.0f;
constexpr float kGap = 8.0f;
constexpr float kColGap = 24.0f;

// The atlas holds ASCII 32 to 127 and nothing else, so a curly quote or a
// dash out of a word processor draws as a blank hole. Licence files are
// written by other people and are full of both. The nearest plain
// character is substituted and anything with no near equivalent is
// dropped, which changes what is drawn and never the file on disk.
std::string to_drawable(const std::string& in) {
    struct Fold {
        const char* from;
        const char* to;
    };
    static const Fold folds[] = {
        {"\xEF\xBB\xBF", ""},    // byte order mark
        {"\xE2\x80\x98", "'"},   // left single quote
        {"\xE2\x80\x99", "'"},   // right single quote
        {"\xE2\x80\x9C", "\""},  // left double quote
        {"\xE2\x80\x9D", "\""},  // right double quote
        {"\xE2\x80\x93", "-"},   // en dash
        {"\xE2\x80\x94", "-"},   // em dash
        {"\xE2\x80\xA6", "..."}, // ellipsis
        {"\xC2\xA0", " "},       // no-break space
        {"\xC2\xA9", "(c)"},     // copyright sign
        {"\xC2\xAE", "(r)"},     // registered sign
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        bool folded = false;
        for (const Fold& f : folds) {
            const size_t n = std::char_traits<char>::length(f.from);
            if (in.compare(i, n, f.from) == 0) {
                out += f.to;
                i += n;
                folded = true;
                break;
            }
        }
        if (folded) continue;
        const unsigned char c = static_cast<unsigned char>(in[i]);
        // Tabs become spaces because the block layout has no tab stops.
        if (c == '\t')
            out += "    ";
        else if (c == '\r')
            ;  // dropped: the line break is the '\n' beside it
        else if (c == '\n' || (c >= 32 && c < 127))
            out += static_cast<char>(c);
        ++i;
    }
    return out;
}

bool read_file(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = to_drawable(ss.str());
    return true;
}

bool is_font_file(const std::string& file) {
    const size_t dot = file.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = file.substr(dot);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext == ".ttf" || ext == ".otf";
}

// The licence beside a font file. The rule is the font's own name, which
// is what licence_filename() answers and what a font added by the operator
// has to follow. The bundled faces predate that rule and one of them names
// its licence for the family rather than the file, so the first part of
// the name is tried as well before giving up.
std::string licence_path_for(const std::string& font_file) {
    const std::string dir = bundled_fonts_dir();
    std::ifstream exact(dir + licence_filename(font_file), std::ios::binary);
    if (exact) return dir + licence_filename(font_file);
    const size_t dash = font_file.find('-');
    if (dash != std::string::npos) {
        const std::string family = dir + font_file.substr(0, dash) + "-license.txt";
        std::ifstream f(family, std::ios::binary);
        if (f) return family;
    }
    return std::string();
}

// What to call the tab. A face INOP knows by name is called that; anything
// else is called after its file, which is the only name it has.
std::string tab_name_for(const std::string& font_file) {
    for (const FontChoice& c : available_fonts())
        if (c.file == font_file) return c.name;
    const size_t dot = font_file.find_last_of('.');
    return dot == std::string::npos ? font_file : font_file.substr(0, dot);
}

}  // namespace

void LegalPanel::open() {
    docs_.clear();
    selected_ = 0;

    std::string text;
    if (read_file("LICENSE", &text)) docs_.push_back(Document{"INOP", text});

    // Only what is actually in the bundled folder. A face installed on the
    // machine rather than shipped with INOP is the machine's business, and
    // guessing at terms for a file that is not here would be worse than
    // saying nothing.
    refresh_available_fonts();
    std::error_code ec;
    std::filesystem::directory_iterator it(bundled_fonts_dir(), ec);
    if (!ec) {
        for (const std::filesystem::directory_entry& e : it) {
            if (!e.is_regular_file(ec)) continue;
            const std::string file = e.path().filename().string();
            if (!is_font_file(file)) continue;
            const std::string licence = licence_path_for(file);
            if (licence.empty()) continue;
            if (read_file(licence, &text)) docs_.push_back(Document{tab_name_for(file), text});
        }
    }

    scroll_.assign(docs_.size(), 0.0f);
}

void LegalPanel::frame(const GuiInput& in, int width, int height) {
    begin_widget_frame();
    wordmark_clicked_ = false;

    const float w = static_cast<float>(width), h = static_cast<float>(height);

    // The wordmark top left, where the rest of the screens are headed.
    const float word_tw = text_width(Font::Wordmark, "INOP");
    const float word_th = text_line_height(Font::Wordmark);
    Rect wordmark_r{kMargin, 6.0f, word_tw + 24.0f, word_th + 12.0f};
    if (wordmark_button(wordmark_r, in)) wordmark_clicked_ = true;

    float y = wordmark_r.y + wordmark_r.h + 10.0f;
    label(Rect{kMargin, y, w - 2 * kMargin, 26}, "Licences", false, Font::BodyLarge);
    y += 32.0f;

    if (docs_.empty()) {
        label(Rect{kMargin, y, w - 2 * kMargin, 26},
              "No licence files were found beside the program.", true);
        end_widget_frame(in);
        return;
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(docs_.size())) selected_ = 0;

    // Tabs down the left, the text filling everything to the right of
    // them. A tab is a button that stays pressed, which is the accent the
    // rest of the screens use for the choice already made.
    float ty = y;
    for (size_t i = 0; i < docs_.size(); ++i) {
        if (button(Rect{kMargin, ty, kTabW, kTabH}, docs_[i].tab, in, true,
                   static_cast<int>(i) == selected_))
            selected_ = static_cast<int>(i);
        ty += kTabH + kGap;
    }

    const float text_x = kMargin + kTabW + kColGap;
    text_block(Rect{text_x, y, w - text_x - kMargin, h - y - kMargin},
               docs_[static_cast<size_t>(selected_)].text, in,
               scroll_[static_cast<size_t>(selected_)]);

    end_widget_frame(in);
}

}  // namespace gui
}  // namespace inop
