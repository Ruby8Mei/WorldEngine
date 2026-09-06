#include "gui_script.hpp"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace inop {
namespace gui {

namespace {

// Everything after the first word, with the spaces either side taken off
// and nothing in the middle touched. Used by type and shot, both of which
// want the text exactly as it was written rather than split into words.
std::string after_verb(const std::string& line) {
    std::size_t p = line.find_first_not_of(" \t");
    if (p == std::string::npos) return {};
    p = line.find_first_of(" \t", p);
    if (p == std::string::npos) return {};
    p = line.find_first_not_of(" \t", p);
    if (p == std::string::npos) return {};
    std::string out = line.substr(p);
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ' || out.back() == '\t'))
        out.pop_back();
    return out;
}

}  // namespace

bool InputScript::load(const std::string& path, std::string* error) {
    auto fail = [&](int line_no, const std::string& why) {
        if (error) *error = path + ":" + std::to_string(line_no) + ": " + why;
        return false;
    };

    std::ifstream f(path);
    if (!f) {
        if (error) *error = "could not open " + path;
        return false;
    }

    steps_.clear();
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        ++line_no;
        std::istringstream ls(line);
        std::string verb;
        if (!(ls >> verb) || verb.empty() || verb[0] == '#') continue;

        Step s;
        if (verb == "move") {
            if (!(ls >> s.x >> s.y)) return fail(line_no, "move needs an x and a y");
            s.verb = Verb::Move;
        } else if (verb == "click") {
            s.verb = Verb::Click;
        } else if (verb == "press") {
            s.verb = Verb::Press;
        } else if (verb == "release") {
            s.verb = Verb::Release;
        } else if (verb == "type") {
            s.verb = Verb::Type;
            s.text = after_verb(line);
            if (s.text.empty()) return fail(line_no, "type needs something to type");
        } else if (verb == "key") {
            std::string which;
            if (!(ls >> which))
                return fail(line_no,
                            "key needs enter, escape, backspace, delete, an arrow or a "
                            "single letter");
            if (which == "enter") s.key = Key::Enter;
            else if (which == "delete") s.key = Key::Delete;
            else if (which == "escape") s.key = Key::Escape;
            else if (which == "backspace") s.key = Key::Backspace;
            else if (which == "up") s.key = Key::Up;
            else if (which == "down") s.key = Key::Down;
            else if (which == "left") s.key = Key::Left;
            else if (which == "right") s.key = Key::Right;
            else if (which.size() == 1 && std::isalpha(static_cast<unsigned char>(which[0]))) {
                s.key = Key::Letter;
                s.letter = static_cast<char>(std::toupper(static_cast<unsigned char>(which[0])));
            } else return fail(line_no, "unknown key '" + which + "'");
            s.verb = Verb::Key;
        } else if (verb == "shift") {
            std::string which;
            if (!(ls >> which)) return fail(line_no, "shift needs on or off");
            if (which == "on") s.flag = true;
            else if (which == "off") s.flag = false;
            else return fail(line_no, "shift takes on or off, not '" + which + "'");
            s.verb = Verb::Shift;
        } else if (verb == "alt") {
            std::string which;
            if (!(ls >> which)) return fail(line_no, "alt needs on or off");
            if (which == "on") s.flag = true;
            else if (which == "off") s.flag = false;
            else return fail(line_no, "alt takes on or off, not '" + which + "'");
            s.verb = Verb::Alt;
        } else if (verb == "ctrl") {
            std::string which;
            if (!(ls >> which)) return fail(line_no, "ctrl needs on or off");
            if (which == "on") s.flag = true;
            else if (which == "off") s.flag = false;
            else return fail(line_no, "ctrl takes on or off, not '" + which + "'");
            s.verb = Verb::Ctrl;
        } else if (verb == "scroll") {
            if (!(ls >> s.x)) return fail(line_no, "scroll needs an amount");
            s.verb = Verb::Scroll;
        } else if (verb == "wait") {
            if (!(ls >> s.seconds)) return fail(line_no, "wait needs a number of seconds");
            if (s.seconds < 0.0f) return fail(line_no, "wait cannot be negative");
            s.verb = Verb::Wait;
        } else if (verb == "shot") {
            s.verb = Verb::Shot;
            s.text = after_verb(line);
            if (s.text.empty()) return fail(line_no, "shot needs a name");
            // The name becomes a filename, so anything that would make it
            // one path segment deeper is refused here rather than turned
            // into a write somewhere nobody expected.
            if (s.text.find('/') != std::string::npos ||
                s.text.find('\\') != std::string::npos || s.text == "." || s.text == "..")
                return fail(line_no, "a shot name cannot contain a path");
        } else if (verb == "quit") {
            s.verb = Verb::Quit;
        } else {
            return fail(line_no, "unknown verb '" + verb + "'");
        }
        steps_.push_back(s);
    }

    at_ = 0;
    click_released_next_ = false;
    waited_ = 0.0f;
    done_ = false;
    return true;
}

bool InputScript::fill(GuiInput& out, float dt) {
    pending_shot_.clear();

    // Rebuilt from nothing every frame. Only the pointer position and the
    // two held states carry over, because those are the ones a real device
    // also carries over between polls.
    out = GuiInput{};
    out.mouse_x = mouse_x_;
    out.mouse_y = mouse_y_;
    out.mouse_held = held_;
    out.ctrl_held = ctrl_;
    out.shift_held = shift_;
    out.alt_held = alt_;

    if (done_ || at_ >= steps_.size()) return false;

    const Step& s = steps_[at_];
    switch (s.verb) {
        case Verb::Move:
            mouse_x_ = s.x;
            mouse_y_ = s.y;
            out.mouse_x = s.x;
            out.mouse_y = s.y;
            ++at_;
            break;

        case Verb::Click:
            // Two frames, the same two a polled mouse button produces. A
            // button fires on the down edge and a dip is held state, so a
            // single frame carrying both edges would be a click no widget
            // in here has ever seen.
            if (!click_released_next_) {
                out.mouse_pressed = true;
                out.mouse_held = true;
                held_ = true;
                click_released_next_ = true;
            } else {
                out.mouse_released = true;
                out.mouse_held = false;
                held_ = false;
                click_released_next_ = false;
                ++at_;
            }
            break;

        case Verb::Press:
            out.mouse_pressed = true;
            out.mouse_held = true;
            held_ = true;
            ++at_;
            break;

        case Verb::Release:
            out.mouse_released = true;
            out.mouse_held = false;
            held_ = false;
            ++at_;
            break;

        case Verb::Type:
            // GLFW hands over one codepoint per key, not one byte, so a
            // script written in UTF-8 is decoded here rather than pushed
            // through a byte at a time. Without this an accented letter
            // would arrive as the two halves of itself and the field would
            // see neither.
            for (std::size_t i = 0; i < s.text.size();) {
                const unsigned char c = static_cast<unsigned char>(s.text[i]);
                unsigned int cp = c;
                std::size_t len = 1;
                if ((c & 0xE0) == 0xC0) {
                    cp = c & 0x1Fu;
                    len = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    cp = c & 0x0Fu;
                    len = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    cp = c & 0x07u;
                    len = 4;
                }
                // A run that is cut short or malformed costs one byte and
                // is otherwise ignored, so a damaged script cannot walk
                // off the end of the line.
                if (len > 1 && i + len <= s.text.size()) {
                    bool ok = true;
                    for (std::size_t k = 1; k < len; ++k) {
                        const unsigned char t = static_cast<unsigned char>(s.text[i + k]);
                        if ((t & 0xC0) != 0x80) ok = false;
                        else cp = (cp << 6) | (t & 0x3Fu);
                    }
                    if (!ok) {
                        ++i;
                        continue;
                    }
                    i += len;
                } else if (len > 1) {
                    ++i;
                    continue;
                } else {
                    ++i;
                }
                out.typed.push_back(cp);
            }
            ++at_;
            break;

        case Verb::Key:
            switch (s.key) {
                case Key::Enter: out.key_enter = true; break;
                case Key::Escape: out.key_escape = true; break;
                case Key::Backspace: out.key_backspace = true; break;
                case Key::Delete: out.key_delete = true; break;
                case Key::Up: out.key_up = true; break;
                case Key::Down: out.key_down = true; break;
                case Key::Left: out.key_left = true; break;
                case Key::Right: out.key_right = true; break;
                case Key::Letter: out.key_letter = s.letter; break;
            }
            ++at_;
            break;

        case Verb::Ctrl:
            ctrl_ = s.flag;
            out.ctrl_held = ctrl_;
            ++at_;
            break;

        case Verb::Shift:
            shift_ = s.flag;
            out.shift_held = shift_;
            ++at_;
            break;

        case Verb::Alt:
            alt_ = s.flag;
            out.alt_held = alt_;
            ++at_;
            break;

        case Verb::Scroll:
            out.scroll_y = s.x;
            ++at_;
            break;

        case Verb::Wait:
            // Real elapsed time rather than a frame count, because every
            // wait in this interface that is worth testing is written in
            // seconds and a frame count only means what it looks like at
            // one refresh rate.
            waited_ += dt;
            if (waited_ >= s.seconds) {
                waited_ = 0.0f;
                ++at_;
            }
            break;

        case Verb::Shot:
            pending_shot_ = s.text;
            ++at_;
            break;

        case Verb::Quit:
            done_ = true;
            return false;
    }

    return true;
}

}  // namespace gui
}  // namespace inop
