#include <iostream>

#include "gui.hpp"

namespace inop {

void run_gui_settings() {
    std::cout << "  this build has no GUI support "
                 "(configure with -DINOP_WITH_GUI=ON and a vcpkg toolchain file)\n";
}

}

