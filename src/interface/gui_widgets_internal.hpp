#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gui_widgets.hpp"

namespace inop::gui {

struct FieldEdit {
    std::size_t caret = 0;
    std::size_t anchor = 0;
    bool dragging = false;
    std::size_t view_start = 0;
    std::size_t first_row = 0;
    std::vector<std::string> undo;
    std::vector<std::string> redo;
};

struct SearchUpdate {
    bool close = false;
    bool changed = false;
};

struct ScrollGeometry {
    float maximum = 0.0f;
    float view_h = 0.0f;
    float track_x = 0.0f;
    float thumb_h = 0.0f;
    float thumb_y = 0.0f;
};

namespace widget_internal {

bool apply_text_input(std::string& value, FieldEdit& edit, const GuiInput& input,
                      const std::string& allowed, std::size_t max_length,
                      CaseFold case_fold, bool unicode_text);
GuiInput filtered_input(const GuiInput& input, const Rect& rect);
std::size_t clamp_first_row(std::size_t first, std::size_t row_count, int visible_rows);
void reset_search(DropdownSearchState& search, int selected);
SearchUpdate advance_dropdown_search(const std::vector<std::string>& options,
                                     DropdownSearchState& search, int& selected,
                                     const GuiInput& input);
ScrollGeometry compute_scroll_geometry(float top, float width, float height,
                                       float scroll, float content_height);
void advance_scroll_region(float top, float width, float height, float& scroll,
                           float content_height, const GuiInput& input);
bool scroll_drag_active();

}
}
