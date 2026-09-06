// gui_maintenance_panel.hpp — the maintenance screen: fresh wheels and
// fresh key sheets, reached from the main menu.
//
// The windowed counterpart of terminal option 2, and the same three jobs:
// a rotor batch, a reflector batch, a key sheet. It produces key material
// and nothing else — no message ever passes through this screen.
//
// Every button here goes through the generator.hpp entry points the
// terminal menu already uses (build_wheel_batch, write_wheel_batch,
// write_key_sheet), so the two front ends cannot produce different files
// and no generation logic lives in the interface layer.
//
// Unlike the settings screen there is nothing pending to apply: each
// section acts when its own button is clicked and reports underneath
// itself. Writing files is the whole point of the screen, so the work
// happens here rather than being handed back to gui.cpp, which owns only
// the window, the font atlases and the palette.
#pragma once

#include <string>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class MaintenancePanel {
public:
    // Returns every field to its default and drops any status text and any
    // half-made overwrite confirmation, so arriving at the screen never
    // inherits an armed destructive click from a previous visit.
    void open();

    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

    // True the frame the INOP wordmark was clicked — caller returns to the
    // main menu.
    bool wordmark_clicked() const { return wordmark_clicked_; }

private:
    // The two wheel sections differ only in whether they carry a notch
    // count, so they share one set of fields and one generate path.
    struct WheelForm {
        std::string count;
        std::string prefix;
        std::string start;
        std::string notches;  // rotors only; ignored by the reflector section
        std::string path;
        int mode_idx = 0;  // 0 = overwrite, 1 = append
        bool confirm = false;
        std::string status;
        bool status_error = false;
    };

    struct SheetForm {
        int suite_idx = 1;  // 0 = Legacy, 1 = INOP-38
        std::string entries;
        std::string plug_pairs;
        std::string notches;
        int count_mode_idx = 0;  // 0 = fixed, 1 = drawn per entry
        std::string rotor_count;
        std::string path;
        bool confirm = false;
        std::string status;
        bool status_error = false;
    };

    float draw_header(const GuiInput& in, float width);
    // Each draws one section downward from `y` and returns the y just past
    // it, so the sections stack down one column exactly as the settings
    // screen's do.
    float draw_wheels(const GuiInput& in, float x, float y, bool rotors);
    float draw_key_sheet(const GuiInput& in, float x, float y);

    // The generating half, kept away from the drawing half so a section
    // reads as layout only.
    void generate_wheels(bool rotors);
    void generate_key_sheet();

    // Overwrite is the default mode, so the guard is the target file
    // itself: a click about to destroy something asks once, and only then.
    // Appending destroys nothing and never asks.
    static bool wheels_need_confirm(const WheelForm& f);

    WheelForm rotor_;
    WheelForm reflector_;
    SheetForm sheet_;

    // How far the content is scrolled, and how tall it measured last frame.
    // The second is what the clamp needs and can only be known after a
    // layout pass, so it lags by one frame by construction.
    float scroll_ = 0.0f;
    float content_h_ = 0.0f;

    int open_dropdown_id_ = -1;
    bool wordmark_clicked_ = false;
};

}  // namespace gui
}  // namespace inop
