// gui_script.hpp -- a scripted source of GuiInput, so the real window can
// be driven without touching the operators pointer.
//
// Every input in this interface converges on one GuiInput struct, filled
// once per frame in exactly one place. That is the whole seam this uses:
// the window, the drawing and every widget stay exactly as they are,
// because nothing downstream knows where a GuiInput came from. Only the
// filling of the struct is replaced.
//
// The window stays real and stays visible. That is the point rather than a
// concession: a screenshot at each step keeps working, and a screenshot is
// the only thing that catches a glyph the font atlas cannot draw.
//
// Nothing here calls the operating system. No cursor is moved, no window
// message is posted and no focus changes, which is what makes this the
// only way of driving the interface that is allowed at all.
//
// Deliberately knows nothing about GLFW or OpenGL, so the parser can be
// compiled into the headless harness and proved there rather than in a
// window nobody can watch.
//
// A script is one verb per line. Blank lines and lines beginning with #
// are ignored. Coordinates are logical units, the same ones the widgets
// lay themselves out in, so a script does not have to be rewritten for
// each zoom level.
//
//   move <x> <y>     put the pointer there, and leave it there
//   click            press and release, over two frames
//   press            button down, and held down until release
//   release          button up
//   type <text>      the rest of the line, typed this frame
//   key enter        one keypress
//   key escape
//   key backspace
//   key up           moves the keyboard focus, or an open dropdown list
//   key down
//   key left
//   key right
//   ctrl on|off      held, the way the real Control key is polled
//   scroll <amount>  one wheel notch is 1
//   wait <seconds>   hold everything still for that long
//   shot <name>      write gui-shots/<name>.png of this frame
//   quit             close the window, the way the close button does
//
// Running off the end of the file closes the window too, so a script can
// never leave one stranded.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class InputScript {
public:
    // Reads and parses the whole file up front. A script that does not
    // parse is refused whole and `error` says which line and why, so a
    // typo on the last line is reported before the first frame is drawn
    // rather than after forty of them.
    bool load(const std::string& path, std::string* error);

    // Fills `out` for the frame about to be drawn. Returns false once the
    // script is spent, which is the signal to close the window.
    //
    // The whole struct is written every frame rather than merged into, so
    // anything the GLFW callbacks left behind between polls is discarded.
    // That is deliberate: a stray keystroke on the machine running the
    // script cannot derail the run.
    bool fill(GuiInput& out, float dt);

    // Set by fill() when the frame it just filled asked for a picture of
    // itself, empty otherwise. Read after fill(), acted on once the frame
    // has been drawn, since a screenshot of a frame has to come after it.
    const std::string& pending_shot() const { return pending_shot_; }

private:
    enum class Verb { Move, Click, Press, Release, Type, Key, Ctrl, Shift, Scroll, Wait, Shot, Quit };
    // Letter is any of A to Z, carried in Step::letter. It is a key press
    // and not a typed character, so it is the only way a script can reach
    // a Control shortcut: holding Control produces no character event.
    enum class Key { Enter, Escape, Backspace, Delete, Up, Down, Left, Right, Letter };

    struct Step {
        Verb verb = Verb::Wait;
        double x = 0.0, y = 0.0;  // Move, and the amount for Scroll
        std::string text;         // Type and Shot
        float seconds = 0.0f;     // Wait
        bool flag = false;        // Ctrl
        Key key = Key::Enter;
        char letter = 0;
    };

    std::vector<Step> steps_;
    std::size_t at_ = 0;
    // Click is the one verb that takes two frames, since a real click is a
    // down edge and then an up edge and no widget sees both at once.
    bool click_released_next_ = false;
    float waited_ = 0.0f;
    bool done_ = false;

    // The pointer and the modifiers persist between steps, the way a real
    // pointer stays where it was last left rather than returning to some
    // origin. Off the window until the first move, so nothing is hovered
    // by a script that never said where to point.
    double mouse_x_ = -1.0e6, mouse_y_ = -1.0e6;
    bool held_ = false;
    bool ctrl_ = false;
    bool shift_ = false;

    std::string pending_shot_;
};

}  // namespace gui
}  // namespace inop
