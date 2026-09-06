// gui_tutorial.hpp -- the walkthrough that runs on a first launch, and
// can be replayed from the settings screen.
//
// It is a tour of the three screens that do work: maintenance, setup and
// enciphering. Not a slideshow. Every step waits for the operator to do
// the real thing on the real screen -- click the real button, type into
// the real box -- and while it waits, Focus Mode is up: only the control
// the step opened answers anything at all. See gui_widgets.hpp for the
// gate itself.
//
// This file knows nothing about rotors, ciphers, GLFW or panels. It is
// handed a small set of plain facts once a frame (TutorialFacts below),
// which gui.cpp fills from whichever screens are on, and it answers with
// a gate and a bubble. That is what lets the whole state machine be
// checked headlessly in gui_self_test.cpp, where there is no window and
// nothing draws.
#pragma once

#include <string>

#include "gui_widgets.hpp"

namespace inop {
namespace gui {

// Which screen the operator is on, as much of it as the tutorial needs to
// know. Legal and settings are Other: no step happens there, so the
// tutorial only has to be able to tell that it is not where it should be.
enum class TutorialScreen { Other, MainMenu, Maintenance, Setup, Cipher };

// Everything the tutorial is allowed to know about the state of the
// application, gathered once a frame by gui.cpp. Deliberately plain
// strings and bools rather than pointers to the panels: a step advances
// on a fact, and a fact can be written down in a test.
struct TutorialFacts {
    TutorialScreen screen = TutorialScreen::Other;

    // Maintenance. The count box as typed, whether the generate button
    // has turned into its overwrite question, and whether a batch has
    // actually been written.
    std::string wheel_count;
    bool wheels_confirming = false;
    bool wheels_written = false;

    // Setup. `setup_ready` is the same condition that enables Next, so a
    // step can wait for the machine to be complete without knowing what
    // makes one complete.
    //
    // `setup_fields_ok` is everything except the master key. The two are
    // separate because the key has to be as long as the rotor count plus
    // one, so changing the count leaves a key that was right a moment ago
    // the wrong length. A step that changed the count and then waited for
    // the whole machine to be complete would wait for ever, since the box
    // that would fix it is not the one that step opened.
    bool setup_ready = false;
    bool setup_fields_ok = false;
    std::string language_code;
    std::string rotor_one;
    int rotor_count = 0;
    std::string plugboard;
    std::string master_key;

    // Enciphering.
    bool has_message = false;
    bool has_cipher = false;
    bool cipher_pasted = false;
    bool marker_pasted = false;
    bool has_plain = false;
};

// The three parts of the tour. Stored in the preferences file so a
// tutorial left half done can be picked up at the start of the part the
// operator was in -- a step in the middle of a screen leans on state that
// a restart has thrown away, so the section is as fine as a resume can
// honestly be.
enum class TutorialSection { Maintenance = 0, Setup = 1, Cipher = 2 };

class Tutorial {
public:
    // Starts at the beginning of `from`. Cipher resumes at the start of
    // Setup instead: the enciphering screen needs a built machine, and
    // after a restart there is not one.
    void start(TutorialSection from);
    // Stops it where it stands. What was reached is still readable from
    // section() afterwards, which is what the preferences file stores.
    void skip();

    bool active() const { return active_; }
    // Which part the operator reached. Meaningful whether it is running
    // or was stopped.
    TutorialSection section() const;
    // True once the last step has been answered. The tutorial is over and
    // is never offered again.
    bool finished() const { return finished_; }

    // Called once a frame, before the screen draws, with the facts as of
    // the frame just gone. Advances the step if what it was waiting for
    // has happened, then puts the gate up around whichever control the
    // new step is waiting on. Nothing is drawn here.
    void begin_frame(const TutorialFacts& facts, const GuiInput& in);

    // Called once a frame, after the screen has drawn, so the bubble and
    // the spotlight land on top of it. The Skip link and the Done button
    // live in here and ignore the gate.
    void draw(const GuiInput& in, int width, int height);

    // True the frame Skip was clicked, so gui.cpp can ask the question
    // rather than the tutorial ending under the operator without one.
    bool skip_requested() const { return skip_requested_; }

    // How many steps there are, and which one is up. Only the self-test
    // and the bubble need these.
    int step() const { return step_; }
    static int step_count();

private:
    // Where the gate goes for the current step, read back from the
    // landmarks the panels published while the previous frame drew.
    // Nothing found means the control is not on screen yet -- a screen
    // change is still travelling, or it is scrolled out of view -- and
    // the gate closes over the whole screen until it turns up.
    void set_gate_for_step();
    // Whether what the current step was waiting for has happened.
    bool step_done(const TutorialFacts& facts, const GuiInput& in) const;

    bool active_ = false;
    bool finished_ = false;
    int step_ = 0;
    // The facts as they stood when the current step began, so a step can
    // wait for a value to change rather than for it to take some
    // particular value the tutorial would have to know in advance.
    TutorialFacts snapshot_;
    bool skip_requested_ = false;
    // The input of the frame just gone. A step that waits for a click is
    // judged on this rather than on the frame arriving, because the
    // screen draws after begin_frame(): a click seen there has not yet
    // reached the control it was aimed at.
    GuiInput last_in_;
    // The rect the current step opened, kept for the spotlight. Empty
    // when the step has no control of its own.
    Rect gate_[2];
    int gate_count_ = 0;
};

}  // namespace gui
}  // namespace inop
