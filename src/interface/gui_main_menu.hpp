// gui_main_menu.hpp — the main menu screen: what the GUI opens on, and
// where the setup screen's INOP wordmark leads back to. Four centre
// buttons (Open INOP / Maintenance / Settings / Exit) — only Open INOP and
// Exit actually go anywhere yet; Maintenance and Settings are deliberate
// stubs until their own screens exist.
#pragma once

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class MainMenu {
public:
    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

    // True the frame "Open INOP" was clicked — caller switches to the
    // setup screen.
    bool open_inop_requested() const { return open_inop_requested_; }

    // True the frame Exit was clicked — caller closes the window.
    bool exit_requested() const { return exit_requested_; }

private:
    bool open_inop_requested_ = false;
    bool exit_requested_ = false;
};

}  // namespace gui
}  // namespace inop
