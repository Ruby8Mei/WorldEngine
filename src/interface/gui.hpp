// gui.hpp — entry point for the optional GUI
//
// This header must never include any GL/GLFW types: it is included
// unconditionally by main.cpp regardless of whether the binary was built
// with INOP_WITH_GUI. Which .cpp actually defines run_gui_settings() (the
// real window in gui.cpp, or the one-line stub in gui_stub.cpp) is decided
// entirely by CMakeLists.txt's source list — main.cpp needs no #ifdef.
#pragma once

namespace inop {

// Opens the GUI window and blocks until the operator closes it
// (window-close, Esc, or the main menu's Exit button). Returns to the
// terminal menu afterward. Opens on the main menu (gui_main_menu.hpp);
// "Open INOP" there leads to the machine setup screen
// (gui_setup_panel.hpp), whose own INOP wordmark leads back. The main
// menu's Maintenance and Settings buttons, and the setup screen's own
// "Next" button, are all still no-ops — later pieces of work.
void run_gui_settings();

}  // namespace inop
