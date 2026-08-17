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

struct GuiInput {
    double mouse_x = 0, mouse_y = 0;
    bool mouse_pressed = false;
    bool mouse_released = false;
    std::vector<unsigned int> typed;
    bool key_backspace = false;
    bool key_enter = false;
    bool key_escape = false;
    double scroll_y = 0;
};

void begin_widget_frame();
void end_widget_frame(const GuiInput& in);

namespace palette {
Color background();
Color panel();
Color border();
Color border_invalid();
Color text();
Color text_dim();
Color accent();
Color disabled_bg();
Color disabled_text();
Color error_bg();
Color error_text();
}

void label(const Rect& r, const std::string& text, bool dim = false, Font font = Font::Body);

bool button(const Rect& r, const std::string& text, const GuiInput& in, bool enabled,
            bool accent = false);

bool toggle(const Rect& r, bool& value, const std::string& text, const GuiInput& in, bool enabled);

enum class CaseFold { None, ToLower, ToUpper };

bool text_field(const Rect& r, std::string& value, const GuiInput& in, const std::string& allowed,
                 size_t max_len, bool enabled, bool invalid, CaseFold case_fold = CaseFold::None,
                 const std::string& placeholder = "", bool center_text = false);

bool numeric_field(const Rect& r, std::string& value, const GuiInput& in, size_t max_len,
                    bool enabled, bool invalid, bool center_text = false);

bool dropdown(const Rect& r, const std::vector<std::string>& options, int& selected, int id,
              int& open_dropdown_id, const GuiInput& in, bool enabled, bool invalid = false);

void draw_open_dropdown_popup(const GuiInput& in, int& open_dropdown_id);

}
}

