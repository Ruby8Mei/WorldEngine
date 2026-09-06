// gui_enciphering_panel.hpp — the enciphering screen: a completed setup
// becomes a running machine, and messages go through it in both
// directions.
//
// Talks to settings.hpp/pipeline.hpp/registry.hpp the same way
// gui_setup_panel.hpp does, and knows nothing about GLFW: clipboard
// traffic is raised as a request that gui.cpp fulfils.
#pragma once

#include <memory>
#include <string>

#include "gui_setup_panel.hpp"
#include "gui_widgets.hpp"
#include "inop.hpp"
#include "pipeline.hpp"

namespace inop {
namespace gui {

class EncipheringPanel {
public:
    // Builds the machine and pipeline from a completed setup. If that
    // fails the screen still opens, shows the reason and offers only the
    // way back, so the failure is read where the click happened.
    void open(const PanelState& state);

    // width/height are the current framebuffer size in pixels.
    void frame(const GuiInput& in, int width, int height);

    // True the frame Back was clicked — caller returns to the setup screen.
    bool back_clicked() const { return back_clicked_; }
    // True the frame the INOP wordmark was clicked — caller returns to the
    // main menu.
    bool wordmark_clicked() const { return wordmark_clicked_; }

    // Clipboard. Only gui.cpp may call GLFW, so the panel asks: a copy
    // hands its text out once, a paste is answered with deliver_paste()
    // on a later frame, aimed at whichever field asked for it.
    bool take_copy_request(std::string* text);
    bool paste_requested() const { return paste_target_ != PasteTarget::None; }
    void deliver_paste(const std::string& text);

private:
    enum class PasteTarget { None, Message, Ciphertext, Marker };

    // Returns the y where the sections below the header begin.
    float draw_header(const GuiInput& in, float width);
    // Each draws one section downward from `y` and returns the y just
    // past it, so the sections stack instead of splitting the window.
    float draw_encipher(const GuiInput& in, float x, float y, float field_w);
    float draw_decipher(const GuiInput& in, float x, float y, float field_w);
    void on_encipher();
    void on_decipher();

    // Reduces pasted text to what the target field would have accepted
    // typed: case folded to the alphabet, line breaks and tabs as spaces,
    // everything else dropped.
    std::string filtered(const std::string& text, const std::string& allowed) const;

    std::unique_ptr<Machine> machine_;
    std::unique_ptr<Pipeline> pipeline_;
    std::string suite_code_ = "38";
    std::string summary_;
    std::string open_error_;
    bool padding_ = true;
    int block_ = 16;
    CaseFold fold_ = CaseFold::ToLower;
    std::string allowed_message_, allowed_cipher_, allowed_marker_;

    std::string message_, cipher_out_, marker_out_, check_out_, encipher_error_;
    std::string cipher_in_, marker_in_, plain_out_, decipher_error_;

    // Scroll offsets for the boxes that can overflow, owned here so the
    // widget set stays stateless like the rest of it. Heights are worked
    // out afresh every frame from the content and the room left on screen.
    float cipher_scroll_ = 0, marker_scroll_ = 0, check_scroll_ = 0, plain_scroll_ = 0;
    float h_cipher_ = 0, h_check_ = 0, h_plain_ = 0;

    // The ciphertext and its marker as one clipboard paste: the
    // ciphertext, exactly five spaces, then the marker. Both halves are
    // needed to read the message back, and they are useless apart, so
    // this is the one copy that carries a whole dispatch.
    void copy_both();
    // The Clear button both sections carry. Draws at bx,by and empties
    // whichever writable box has the focus. No keyboard shortcut yet: the
    // roadmap named Shift and C, which is how a capital C is typed, so it
    // would eat the keystroke inside every box it is meant to serve.
    void draw_clear_button(const GuiInput& in, float bx, float by);
    static const char* kBothSeparator;

    PasteTarget paste_target_ = PasteTarget::None;
    std::string copy_text_;
    bool copy_pending_ = false;
    bool back_clicked_ = false;
    bool wordmark_clicked_ = false;
};

}  // namespace gui
}  // namespace inop
