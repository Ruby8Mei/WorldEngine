#include "gui_widgets_internal.hpp"
#include "gui_text_edit.hpp"

namespace inop::gui {

using namespace widget_internal;

void text_edit_self_test(const std::function<void(bool, const std::string&)>& check) {
    std::string clipboard;
    set_text_clipboard([&]() { return clipboard; }, [&](const std::string& text) { clipboard = text; });
    std::string value;
    FieldEdit ed;
    GuiInput in;
    in.typed = {'a', 'A', 0xE9, 0x1ED9, '5', ',', '!', 0xD55C};
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    const std::string original = "aA\xC3\xA9\xE1\xBB\x99" "5,!\xED\x95\x9C";
    check(value == original, "GUI typing retains case, diacritics, numbers, punctuation and Unicode");
    in = GuiInput{};
    in.ctrl_held = true;
    in.key_letter = 'A';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(ed.anchor == 0 && ed.caret == value.size(), "Ctrl+A selects the focused field");
    in.key_letter = 'C';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(clipboard == original && value == original, "Ctrl+C copies the selection without editing");
    in.key_letter = 'X';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value.empty() && clipboard == original, "Ctrl+X cuts selected Unicode text");
    in.key_letter = 'V';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == original, "Ctrl+V restores readable Unicode text");
    ed.anchor = 1;
    ed.caret = 2;
    clipboard = "Z";
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == "aZ" + original.substr(2), "Paste replaces only the selected text");
    in.key_letter = 'Z';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == original, "Paste can be undone as one edit");
    in = GuiInput{};
    in.key_backspace = true;
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == original.substr(0, original.size() - 3), "Backspace removes a complete Unicode codepoint");
    ed.caret = ed.anchor = 2;
    in.key_backspace = false;
    in.key_delete = true;
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == "aA\xE1\xBB\x99" "5,!", "Delete removes a complete accented character");
    const std::string before_clear = value;
    in = GuiInput{};
    in.ctrl_held = true;
    in.key_letter = 'Q';
    in.key_clear = true;
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value.empty(), "Clear shortcut empties the focused field");
    in.key_clear = false;
    in.key_letter = 'Z';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == before_clear, "Clear shortcut supports undo");
    ed.anchor = 0;
    ed.caret = value.size();
    clipboard = "unchanged";
    in.key_letter = 'C';
    in.shift_held = true;
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(clipboard == "unchanged", "Ctrl+Shift+C does not alter editable text");
    in.shift_held = false;
    in.alt_held = true;
    in.key_letter = 'X';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == before_clear, "Alt application shortcuts do not cut editable text");
    in.alt_held = false;
    in.key_letter = 'V';
    clipboard = "12x34";
    value.clear();
    ed = FieldEdit{};
    apply_text_input(value, ed, in, "0123456789", 4, CaseFold::None, false);
    check(value == "1234", "Paste respects numeric field restrictions");
    clipboard = "56789";
    ed.anchor = 0;
    apply_text_input(value, ed, in, "0123456789", 4, CaseFold::None, false);
    check(value == "1234", "Oversized paste preserves the existing selection and value");
    const std::string sample = "A\xC3\xA9\xE1\xBB\x99";
    check(next_utf8(sample, 1) == 3 && previous_utf8(sample, 6) == 3 &&
          previous_utf8(sample, 2) == 1 &&
          utf8_boundary(sample, 4) == 3, "Caret indices stay on UTF-8 boundaries");
    const std::string damaged = "A\xF0\x28\x8C\x28";
    size_t damaged_offset = 1;
    check(read_utf8(damaged, damaged_offset) == 0xFFFD && damaged_offset == 2 &&
          next_utf8(damaged, 1) == 2 && previous_utf8(damaged, 3) == 2,
          "GUI UTF-8 adapter replaces malformed bytes and keeps caret movement bounded");
    value.clear();
    ed = FieldEdit{};
    clipboard = damaged;
    in = GuiInput{};
    in.ctrl_held = true;
    in.key_letter = 'V';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == "A\xEF\xBF\xBD(\xEF\xBF\xBD(" && ed.caret == value.size(),
          "malformed pasted bytes become complete replacement characters");
    in = GuiInput{};
    in.key_backspace = true;
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == "A\xEF\xBF\xBD(\xEF\xBF\xBD",
          "Backspace removes one complete character after malformed paste");
    in = GuiInput{};
    in.ctrl_held = true;
    in.key_letter = 'Z';
    apply_text_input(value, ed, in, "", 4096, CaseFold::None, true);
    check(value == "A\xEF\xBF\xBD(\xEF\xBF\xBD(",
          "undo restores complete replacement characters");
    value = "1234";
    ed = FieldEdit{};
    add_focus_gate(Rect{0, 0, 10, 10});
    in.key_clear = true;
    const GuiInput blocked = filtered_input(in, Rect{20, 20, 10, 10});
    apply_text_input(value, ed, blocked, "0123456789", 4, CaseFold::None, false);
    check(value == "1234", "Focus gating blocks clipboard and Clear commands outside the active control");
    clear_focus_gate();
    std::string message = "left input";
    std::string ciphertext = "right input";
    const Rect left_focus{10, 10, 20, 20};
    const Rect right_focus{40, 10, 20, 20};
    set_keyboard_focus(right_focus);
    check(clear_field(message) && message.empty() && ciphertext == "right input" &&
              has_keyboard_focus(right_focus),
          "Left Clear targets the message while right focus and input remain");
    set_keyboard_focus(left_focus);
    message = "left input";
    check(clear_field(ciphertext) && ciphertext.empty() && message == "left input" &&
              has_keyboard_focus(left_focus),
          "Right Clear targets ciphertext while left focus and input remain");
    set_text_clipboard({}, {});
}

void dropdown_search_self_test(const std::function<void(bool, const std::string&)>& check) {
    const std::vector<std::string> options{"English", "French", "German", "Greek", "Spanish"};
    DropdownSearchState search;
    int selected = 0;
    reset_search(search, selected);

    GuiInput in;
    in.typed = {'f'};
    SearchUpdate update = advance_dropdown_search(options, search, selected, in);
    check(search.query == "f" && search.highlighted == 1,
          "language filtering matches displayed names without case sensitivity");
    check(selected == 0 && !update.close,
          "language filtering preserves the current selection while typing");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'G', 'r'};
    advance_dropdown_search(options, search, selected, in);
    check(search.query == "Gr" && search.highlighted == 3,
          "each language search character narrows the visible results");

    in = GuiInput{};
    in.key_backspace = true;
    advance_dropdown_search(options, search, selected, in);
    check(search.query == "G" && search.highlighted == 0,
          "Backspace removes the final language search character");

    in = GuiInput{};
    in.key_down = true;
    advance_dropdown_search(options, search, selected, in);
    check(search.highlighted == 2 && selected == 0,
          "language arrows move only through filtered results");

    in = GuiInput{};
    in.key_enter = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(update.close && selected == 2,
          "Enter selects the highlighted filtered language");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'s'};
    advance_dropdown_search(options, search, selected, in);
    in = GuiInput{};
    in.key_escape = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(update.close && selected == 2,
          "Escape closes language search without changing the selection");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'z', 'z'};
    update = advance_dropdown_search(options, search, selected, in);
    check(search.highlighted == -1 && selected == 2 && !update.close,
          "a language search with no results preserves the selection");
    in = GuiInput{};
    in.key_enter = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(!update.close && selected == 2,
          "Enter cannot select from an empty language result list");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'s', 'p'};
    advance_dropdown_search(options, search, selected, in);
    in = GuiInput{};
    in.key_tab = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(search.query == "Spanish" && search.highlighted == 4 && selected == 2 && !update.close,
          "Tab completes a unique language result without selecting it");
    in = GuiInput{};
    in.key_delete = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(search.query.empty() && selected == 2 && !update.close,
          "Delete clears a completed language query without selecting it");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'e'};
    advance_dropdown_search(options, search, selected, in);
    in = GuiInput{};
    in.key_down = true;
    advance_dropdown_search(options, search, selected, in);
    const int multiple_highlight = search.highlighted;
    in = GuiInput{};
    in.key_tab = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(multiple_highlight >= 0 &&
              search.query == options[static_cast<size_t>(multiple_highlight)] && selected == 2 &&
              !update.close,
          "Tab completes the highlighted language among multiple results without selecting it");

    reset_search(search, selected);
    in = GuiInput{};
    in.typed = {'z', 'z'};
    advance_dropdown_search(options, search, selected, in);
    in = GuiInput{};
    in.key_tab = true;
    update = advance_dropdown_search(options, search, selected, in);
    check(search.query == "zz" && search.highlighted == -1 && selected == 2 && !update.close,
          "Tab leaves a zero-result language query and selection unchanged");
}

void scroll_region_self_test(const std::function<void(bool, const std::string&)>& check) {
    check(clamp_first_row(7, 3, 2) == 1 && clamp_first_row(4, 1, 2) == 0,
          "multiline field scroll clamps after content changes");

    float scroll = 900.0f;
    GuiInput in;
    advance_scroll_region(100.0f, 800.0f, 600.0f, scroll, 400.0f, in);
    check(scroll == 0.0f, "panel scroll clamps when resize or zoom makes content fit");

    scroll = 240.0f;
    const ScrollGeometry start = compute_scroll_geometry(100.0f, 800.0f, 600.0f, scroll, 1100.0f);
    in.mouse_x = start.track_x + 1.0f;
    in.mouse_y = start.thumb_y + 4.0f;
    in.mouse_pressed = true;
    in.mouse_held = true;
    advance_scroll_region(100.0f, 800.0f, 600.0f, scroll, 1100.0f, in);
    in.mouse_pressed = false;
    in.mouse_y = 1000.0f;
    advance_scroll_region(100.0f, 800.0f, 600.0f, scroll, 1100.0f, in);
    const bool bottom = scroll == 600.0f;
    in.mouse_y = -100.0f;
    advance_scroll_region(100.0f, 800.0f, 600.0f, scroll, 1100.0f, in);
    const bool top_clamped = scroll == 0.0f;
    in.mouse_held = false;
    in.mouse_released = true;
    advance_scroll_region(100.0f, 800.0f, 600.0f, scroll, 1100.0f, in);
    check(bottom && top_clamped && !scroll_drag_active(),
          "scroll thumb drag clamps outside the track and releases cleanly");
}

}
