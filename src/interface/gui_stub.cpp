// gui_stub.cpp — compiled instead of gui.cpp when INOP_WITH_GUI is OFF.
// Keeps the CLI-only build's terminal menu identical in shape (option
// always listed) without linking GLFW/OpenGL/stb_truetype/nlohmann-json.
#include <iostream>

#include "gui.hpp"

namespace inop {

void run_gui_settings() {
    std::cout << "  this build has no GUI support "
                 "(configure with -DINOP_WITH_GUI=ON and a vcpkg toolchain file)\n";
}

}  // namespace inop
