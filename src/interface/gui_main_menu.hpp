// gui_main_menu.hpp — the main menu screen: what the program opens on, and
// where the setup screen's INOP wordmark leads back to. Five centre
// buttons (Open INOP / Terminal / Maintenance / Settings / Exit), each of
// which leads somewhere.
//
// Terminal and Exit both close the window and differ only in what happens
// next: Terminal hands the operator the CLI session, Exit ends the
// process. The screen does not know that — it reports the click and
// gui.cpp turns it into a GuiExit.
#pragma once

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class MainMenu {
public:
    // width/height are the current framebuffer size in pixels.
    // A line down the left edge, level with the buttons. Empty means
    // there is none, which is the usual state. The only thing that sets
    // one is the tutorial counting in gui.cpp, on the launch where it
    // stops asking.
    void set_note(const std::string& text) { note_ = text; }

    void frame(const GuiInput& in, int width, int height);

    // True the frame "Open INOP" was clicked — caller switches to the
    // setup screen.
    bool open_inop_requested() const { return open_inop_requested_; }

    // True the frame Terminal was clicked — caller closes the window and
    // lets the terminal session take over.
    bool terminal_requested() const { return terminal_requested_; }

    // True the frame Maintenance was clicked — caller switches to the
    // maintenance screen.
    bool maintenance_requested() const { return maintenance_requested_; }

    // True the frame Settings was clicked — caller switches to the
    // settings screen.
    bool settings_requested() const { return settings_requested_; }

    // True the frame Exit was clicked — caller closes the window.
    bool exit_requested() const { return exit_requested_; }

private:
    bool open_inop_requested_ = false;
    bool terminal_requested_ = false;
    bool maintenance_requested_ = false;
    bool settings_requested_ = false;
    bool exit_requested_ = false;
    std::string note_;
};

}  // namespace gui
}  // namespace inop
