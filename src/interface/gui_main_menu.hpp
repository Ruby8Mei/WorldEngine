#pragma once

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class MainMenu {
public:
    void frame(const GuiInput& in, int width, int height);

    bool open_inop_requested() const { return open_inop_requested_; }

    bool exit_requested() const { return exit_requested_; }

private:
    bool open_inop_requested_ = false;
    bool exit_requested_ = false;
};

}
}

