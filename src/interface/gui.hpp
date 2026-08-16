// gui.hpp — entry point for the optional GUI settings panel
//
// This header must never include any GL/GLFW types: it is included
// unconditionally by main.cpp regardless of whether the binary was built
// with INOP_WITH_GUI. Which .cpp actually defines run_gui_settings() (the
// real panel in gui.cpp, or the one-line stub in gui_stub.cpp) is decided
// entirely by CMakeLists.txt's source list — main.cpp needs no #ifdef.
#pragma once

namespace inop {

// Opens the settings-panel window and blocks until the operator closes it
// (window-close, Esc). Returns to the terminal menu afterward. Currently a
// dead end by design: the "Next" and "INOP" wordmark actions inside the
// panel are no-ops — the screens they would lead to are a separate,
// later piece of work.
void run_gui_settings();

}  // namespace inop
