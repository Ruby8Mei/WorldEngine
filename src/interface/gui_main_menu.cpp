#include "gui_main_menu.hpp"

namespace inop {
namespace gui {

namespace {
constexpr float kMargin = 24.0f;
constexpr float kButtonW = 220.0f, kButtonH = 40.0f, kButtonGap = 14.0f;
}

void MainMenu::frame(const GuiInput& in, int width, int height) {
    open_inop_requested_ = false;
    exit_requested_ = false;

    float w = static_cast<float>(width), h = static_cast<float>(height);
    clear(palette::background());

    float title_tw = text_width(Font::Wordmark, "INOP");
    float title_th = text_line_height(Font::Wordmark);
    label(Rect{(w - title_tw) * 0.5f, kMargin, title_tw, title_th}, "INOP", false, Font::Wordmark);

    const int kCount = 4;
    float stack_h = kCount * kButtonH + (kCount - 1) * kButtonGap;
    float x = (w - kButtonW) * 0.5f;
    float y = (h - stack_h) * 0.5f;

    Rect open_inop_r{x, y, kButtonW, kButtonH};
    if (button(open_inop_r, "Open INOP", in, true)) open_inop_requested_ = true;

    Rect maintenance_r{x, y + (kButtonH + kButtonGap), kButtonW, kButtonH};
    button(maintenance_r, "Maintenance", in, true);

    Rect settings_r{x, y + 2 * (kButtonH + kButtonGap), kButtonW, kButtonH};
    button(settings_r, "Settings", in, true);

    Rect exit_r{x, y + 3 * (kButtonH + kButtonGap), kButtonW, kButtonH};
    if (button(exit_r, "Exit", in, true)) exit_requested_ = true;
}

}
}

