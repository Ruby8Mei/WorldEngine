// gui_legal_panel.hpp — the licences INOP has to show: its own, and the
// one that came with every font it bundles.
//
// Reached from the Settings footer rather than from the main menu, and it
// reads files and nothing else. A font is listed only once it is actually
// in the bundled folder with its licence beside it, which is the same rule
// the settings font row already applies before offering a face at all: a
// program that showed terms for a file it does not ship would be inventing
// them, and one that shipped a file without its terms would be in the
// wrong.
#pragma once

#include <string>
#include <vector>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class LegalPanel {
public:
    // Reads the licences off disk. Called on the way in, so a font added
    // to the folder while INOP is running gets a tab without a restart.
    void open();

    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

    // True the frame the INOP wordmark was clicked — caller returns to the
    // main menu, the way the wordmark does on every other screen.
    bool wordmark_clicked() const { return wordmark_clicked_; }

private:
    struct Document {
        std::string tab;   // what the tab says
        std::string text;  // the licence, folded to what the atlas can draw
    };

    std::vector<Document> docs_;
    int selected_ = 0;
    // One offset per tab, so moving between them does not lose where the
    // operator had read up to.
    std::vector<float> scroll_;
    bool wordmark_clicked_ = false;
};

}  // namespace gui
}  // namespace inop
