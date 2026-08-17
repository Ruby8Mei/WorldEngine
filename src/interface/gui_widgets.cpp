#include "gui_widgets.hpp"

#include <algorithm>

namespace inop {
namespace gui {

bool rect_contains(const Rect& r, double mx, double my) {
    return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

namespace {

const void* g_focus = nullptr;
bool g_click_consumed_this_frame = false;

struct PendingDropdown {
    bool active = false;
    int id = -1;
    Rect box;
    const std::vector<std::string>* options = nullptr;
    int* selected = nullptr;
};
PendingDropdown g_pending;

float g_popup_scroll = 0.0f;
int g_popup_scroll_owner = -1;

const float PAD = 6.0f;

}

void begin_widget_frame() {
    g_click_consumed_this_frame = false;
    g_pending.active = false;
}

void end_widget_frame(const GuiInput& in) {
    if (in.mouse_pressed && !g_click_consumed_this_frame) g_focus = nullptr;
}

namespace palette {
Color background() { return rgba(0.09f, 0.09f, 0.10f); }
Color panel() { return rgba(0.14f, 0.14f, 0.16f); }
Color border() { return rgba(0.32f, 0.32f, 0.36f); }
Color border_invalid() { return rgba(0.75f, 0.30f, 0.28f); }
Color text() { return rgba(0.92f, 0.92f, 0.90f); }
Color text_dim() { return rgba(0.58f, 0.58f, 0.60f); }
Color accent() { return rgba(0.78f, 0.60f, 0.28f); }
Color disabled_bg() { return rgba(0.16f, 0.16f, 0.17f); }
Color disabled_text() { return rgba(0.40f, 0.40f, 0.42f); }
Color error_bg() { return rgba(0.30f, 0.12f, 0.12f); }
Color error_text() { return rgba(0.92f, 0.70f, 0.70f); }
}

void label(const Rect& r, const std::string& text, bool dim, Font font) {
    float ty = r.y + (r.h + text_line_height(font) * 0.7f) * 0.5f;
    draw_text(font, r.x, ty, text, dim ? palette::text_dim() : palette::text());
}

bool button(const Rect& r, const std::string& text, const GuiInput& in, bool enabled,
            bool accent) {
    bool hovered = rect_contains(r, in.mouse_x, in.mouse_y);
    bool clicked = false;
    Color bg = !enabled ? palette::disabled_bg()
                         : (accent ? palette::accent() : palette::panel());
    draw_rect(r.x, r.y, r.w, r.h, bg);
    draw_rect_outline(r.x, r.y, r.w, r.h, palette::border());
    Color fg = !enabled ? palette::disabled_text()
                         : (accent ? rgba(0.10f, 0.09f, 0.07f) : palette::text());
    float tw = text_width(Font::Body, text);
    float tx = r.x + (r.w - tw) * 0.5f;
    float ty = r.y + (r.h + text_line_height(Font::Body) * 0.7f) * 0.5f;
    draw_text(Font::Body, tx, ty, text, fg);
    if (enabled && hovered && in.mouse_pressed) {
        clicked = true;
        g_click_consumed_this_frame = true;
    }
    return clicked;
}

bool toggle(const Rect& r, bool& value, const std::string& text, const GuiInput& in,
            bool enabled) {
    bool changed = false;
    float box_size = r.h;
    Rect box{r.x, r.y, box_size, box_size};
    Color bg = !enabled ? palette::disabled_bg() : (value ? palette::accent() : palette::panel());
    draw_rect(box.x, box.y, box.w, box.h, bg);
    draw_rect_outline(box.x, box.y, box.w, box.h, palette::border());
    label(Rect{r.x + box_size + PAD, r.y, r.w - box_size - PAD, r.h}, text, !enabled);
    if (enabled && rect_contains(box, in.mouse_x, in.mouse_y) && in.mouse_pressed) {
        value = !value;
        changed = true;
        g_click_consumed_this_frame = true;
    }
    return changed;
}

bool text_field(const Rect& r, std::string& value, const GuiInput& in, const std::string& allowed,
                 size_t max_len, bool enabled, bool invalid, CaseFold case_fold,
                 const std::string& placeholder, bool center_text) {
    bool changed = false;
    bool focused = enabled && g_focus == static_cast<const void*>(&value);

    if (enabled && rect_contains(r, in.mouse_x, in.mouse_y) && in.mouse_pressed) {
        g_focus = &value;
        focused = true;
        g_click_consumed_this_frame = true;
    }

    if (focused) {
        if (in.key_backspace && !value.empty()) {
            value.pop_back();
            changed = true;
        }
        for (unsigned int cp : in.typed) {
            if (cp > 127) continue;
            char c = static_cast<char>(cp);
            if (case_fold == CaseFold::ToLower && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            else if (case_fold == CaseFold::ToUpper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (value.size() >= max_len) continue;
            if (allowed.find(c) == std::string::npos) continue;
            value.push_back(c);
            changed = true;
        }
    }

    Color bg = !enabled ? palette::disabled_bg() : palette::panel();
    draw_rect(r.x, r.y, r.w, r.h, bg);
    Color border = invalid ? palette::border_invalid() : (focused ? palette::accent() : palette::border());
    draw_rect_outline(r.x, r.y, r.w, r.h, border, focused ? 2.0f : 1.0f);
    if (value.empty() && !focused && !placeholder.empty()) {
        if (center_text) {
            float tw = text_width(Font::Body, placeholder);
            label(Rect{r.x + (r.w - tw) * 0.5f, r.y, r.w, r.h}, placeholder, true);
        } else {
            label(Rect{r.x + PAD, r.y, r.w - 2 * PAD, r.h}, placeholder, true);
        }
    } else {
        std::string shown = value + (focused ? "|" : "");
        if (center_text) {
            float tw = text_width(Font::Body, shown);
            label(Rect{r.x + (r.w - tw) * 0.5f, r.y, r.w, r.h}, shown, !enabled);
        } else {
            label(Rect{r.x + PAD, r.y, r.w - 2 * PAD, r.h}, shown, !enabled);
        }
    }
    return changed;
}

bool numeric_field(const Rect& r, std::string& value, const GuiInput& in, size_t max_len,
                    bool enabled, bool invalid, bool center_text) {
    return text_field(r, value, in, "0123456789", max_len, enabled, invalid,
                       CaseFold::None, "", center_text);
}

bool dropdown(const Rect& r, const std::vector<std::string>& options, int& selected, int id,
              int& open_dropdown_id, const GuiInput& in, bool enabled, bool invalid) {
    bool changed = false;
    bool is_open = enabled && open_dropdown_id == id;

    Color bg = !enabled ? palette::disabled_bg() : palette::panel();
    draw_rect(r.x, r.y, r.w, r.h, bg);
    Color border_color =
        invalid ? palette::border_invalid() : (is_open ? palette::accent() : palette::border());
    draw_rect_outline(r.x, r.y, r.w, r.h, border_color);
    std::string shown =
        (selected >= 0 && selected < static_cast<int>(options.size())) ? options[static_cast<size_t>(selected)] : "";
    label(Rect{r.x + PAD, r.y, r.w - 2 * PAD - 14, r.h}, shown, !enabled);
    label(Rect{r.x + r.w - 16, r.y, 14, r.h}, is_open ? "^" : "v", !enabled);

    if (enabled && rect_contains(r, in.mouse_x, in.mouse_y) && in.mouse_pressed) {
        open_dropdown_id = is_open ? -1 : id;
        g_click_consumed_this_frame = true;
        is_open = !is_open;
    }

    if (is_open) {
        g_pending.active = true;
        g_pending.id = id;
        g_pending.box = r;
        g_pending.options = &options;
        g_pending.selected = &selected;
    }
    return changed;
}

void draw_open_dropdown_popup(const GuiInput& in, int& open_dropdown_id) {
    if (!g_pending.active || !g_pending.options) return;
    if (g_pending.id != g_popup_scroll_owner) {
        g_popup_scroll = 0.0f;
        g_popup_scroll_owner = g_pending.id;
    }

    const auto& options = *g_pending.options;
    const float row_h = g_pending.box.h;
    const float max_visible = 8.0f;
    float list_h = row_h * std::min<float>(static_cast<float>(options.size()), max_visible);
    Rect popup{g_pending.box.x, g_pending.box.y + g_pending.box.h, g_pending.box.w, list_h};

    float max_scroll = std::max(0.0f, static_cast<float>(options.size()) * row_h - list_h);
    g_popup_scroll -= static_cast<float>(in.scroll_y) * row_h;
    if (g_popup_scroll < 0.0f) g_popup_scroll = 0.0f;
    if (g_popup_scroll > max_scroll) g_popup_scroll = max_scroll;

    draw_rect(popup.x, popup.y, popup.w, popup.h, palette::panel());
    draw_rect_outline(popup.x, popup.y, popup.w, popup.h, palette::accent());

    bool clicked_inside = false;
    begin_scissor(popup.x, popup.y, popup.w, popup.h);
    for (size_t i = 0; i < options.size(); ++i) {
        float row_y = popup.y + row_h * static_cast<float>(i) - g_popup_scroll;
        if (row_y + row_h < popup.y || row_y > popup.y + popup.h) continue;

        Rect row{popup.x, row_y, popup.w, row_h};
        bool hovered = rect_contains(row, in.mouse_x, in.mouse_y) && rect_contains(popup, in.mouse_x, in.mouse_y);
        if (hovered) draw_rect(row.x, row.y, row.w, row.h, palette::border());
        label(Rect{row.x + PAD, row.y, row.w - 2 * PAD, row.h}, options[i]);
        if (hovered && in.mouse_pressed) {
            *g_pending.selected = static_cast<int>(i);
            open_dropdown_id = -1;
            clicked_inside = true;
        }
    }
    end_scissor();

    if (max_scroll > 0.0f) {
        float thumb_h = std::max(12.0f, popup.h * (list_h / (static_cast<float>(options.size()) * row_h)));
        float thumb_y = popup.y + (popup.h - thumb_h) * (g_popup_scroll / max_scroll);
        draw_rect(popup.x + popup.w - 4, thumb_y, 3, thumb_h, palette::accent());
    }

    if (in.mouse_pressed && !clicked_inside && !rect_contains(g_pending.box, in.mouse_x, in.mouse_y)) {
        if (!rect_contains(popup, in.mouse_x, in.mouse_y)) open_dropdown_id = -1;
    }
}

}
}

