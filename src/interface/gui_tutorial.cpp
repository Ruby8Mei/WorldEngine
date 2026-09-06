#include "gui_tutorial.hpp"

#include <vector>

#include "gui_render.hpp"

namespace inop {
namespace gui {

namespace {

// What a step is waiting for. ClickTarget is the plain case -- a button
// that does one thing -- and everything else waits on a fact, because a
// click is not the action when the control is a list or a box: opening a
// list is not choosing from it, and clicking into a box is not typing.
enum class Advance {
    ClickTarget,
    OnMaintenance,
    WheelsConfirming,
    WheelsWritten,
    OnMainMenu,
    OnSetup,
    SetupReady,
    LanguageChanged,
    RotorOneChanged,
    RotorCountChanged,
    PlugboardChanged,
    MasterKeyChanged,
    OnCipher,
    HasMessage,
    HasCipher,
    CipherPasted,
    MarkerPasted,
    HasPlain,
    LastStep,
};

struct Step {
    TutorialSection section;
    // The control this step opens, by the name the panel published it
    // under. A second name for the two steps that need a pair of controls
    // at once -- copy over here, paste over there. Empty for the closing
    // step, which has nothing on the screen to point at.
    const char* target;
    const char* target2;
    const char* text;
    Advance advance;
};

// The tour. Order matters twice over: it is the order the work is really
// done in -- make wheels, build the machine, send a message -- and inside
// the setup screen it is the order that never breaks what came before.
// Generate Setup fills every box first, and each step after it changes
// one thing that leaves the machine complete. Picking a suite, or asking
// for more rotors, would empty boxes again.
const Step kSteps[] = {
    // -- maintenance ----------------------------------------------------
    {TutorialSection::Maintenance, "menu.maintenance", "",
     "INOP came with wheels the developer made. Yours should be your own. "
     "Click Maintenance and we will cut a fresh set.",
     Advance::OnMaintenance},
    {TutorialSection::Maintenance, "maint.rotor_count", "",
     "This screen makes key material and nothing else. No message ever comes through here. "
     "Start by saying how many rotors you want.",
     Advance::ClickTarget},
    {TutorialSection::Maintenance, "maint.rotor_generate", "",
     "Now click Generate.", Advance::WheelsConfirming},
    {TutorialSection::Maintenance, "maint.rotor_generate", "",
     "The old file is still there, so the button asks before it writes over it. "
     "Click it again. The wheels are yours from here on.",
     Advance::WheelsWritten},
    {TutorialSection::Maintenance, "screen.wordmark", "",
     "That is maintenance. The INOP wordmark takes you back, on every screen.",
     Advance::OnMainMenu},

    // -- setup ----------------------------------------------------------
    {TutorialSection::Setup, "menu.open", "",
     "Now we build a machine. Click Open INOP.", Advance::OnSetup},
    {TutorialSection::Setup, "setup.generate", "",
     "There is a lot on this screen. Click Generate Setup and INOP fills all of it in "
     "with a machine that works.",
     Advance::SetupReady},
    {TutorialSection::Setup, "setup.language", "",
     "Every box is full now, so you can change things without breaking anything. "
     "The language decides which letters a message may use. Pick one.",
     Advance::LanguageChanged},
    {TutorialSection::Setup, "setup.rotor_one", "",
     "Each row is one rotor: which wheel it is, where its ring sits, and where it notches. "
     "Change the wheel in the first row. No two rows may hold the same wheel.",
     Advance::RotorOneChanged},
    {TutorialSection::Setup, "setup.rotor_count", "setup.rotor_grid",
     "This is how many rotors the machine turns. Change it. Fewer rotors and the rows go "
     "away; more, and you fill the new ones in yourself.",
     Advance::RotorCountChanged},
    {TutorialSection::Setup, "setup.plugboard", "",
     "The plugboard swaps two letters over, before the rotors and again after them. "
     "Fill in a pair.",
     Advance::PlugboardChanged},
    {TutorialSection::Setup, "setup.master_key", "",
     "The master key is the secret. Everything else can be public. Change it. It has to be "
     "exactly as long as the line underneath says, which the rotor count decides.",
     Advance::MasterKeyChanged},
    {TutorialSection::Setup, "setup.next", "",
     "The machine is complete, so Next has woken up. Click it.", Advance::OnCipher},

    // -- enciphering ----------------------------------------------------
    {TutorialSection::Cipher, "cipher.message", "",
     "This is the machine running. Type a message on the left. Any words will do.",
     Advance::HasMessage},
    {TutorialSection::Cipher, "cipher.encipher", "",
     "Click Encipher.", Advance::HasCipher},
    {TutorialSection::Cipher, "cipher.copy_cipher", "cipher.paste_cipher",
     "Two things came out. The ciphertext is your message. The marker says where the wheels "
     "started. Click Copy cipher, then Paste cipher on the deciphering side.",
     Advance::CipherPasted},
    {TutorialSection::Cipher, "cipher.copy_marker", "cipher.paste_marker",
     "Now the other half. Without the marker the ciphertext cannot be read back. "
     "Copy marker, then Paste marker.",
     Advance::MarkerPasted},
    {TutorialSection::Cipher, "cipher.decipher", "",
     "Click Decipher.", Advance::HasPlain},
    {TutorialSection::Cipher, "", "",
     "Your message came back. That is the whole machine: cut the wheels, set them up, "
     "and run the message through. Nothing here is hidden from you.",
     Advance::LastStep},
};

constexpr int kStepCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

// The first step of each part, which is where a resume lands.
int first_step_of(TutorialSection s) {
    for (int i = 0; i < kStepCount; ++i)
        if (kSteps[i].section == s) return i;
    return 0;
}

// -- the bubble ----------------------------------------------------------

constexpr float kBubbleW = 520.0f;
constexpr float kBubblePad = 18.0f;
constexpr float kBubbleLine = 22.0f;
constexpr float kBubbleMargin = 20.0f;
constexpr float kRowH = 26.0f;
constexpr float kBtnW = 110.0f;
constexpr float kBtnH = 30.0f;

// Everything outside the opened control, greyed. Four bands rather than
// one sheet with a hole in it: this renderer has no stencil and wants
// none, and four rects say the same thing. The opened control is left
// alone, which is what makes the eye land on it.
void draw_spotlight(const Rect* holes, int count, float w, float h) {
    const Color dim = rgba(0.0f, 0.0f, 0.0f, 0.45f);
    if (count <= 0) {
        draw_rect(0, 0, w, h, dim);
        return;
    }
    // The union of the holes, padded, so two holes on one row leave one
    // clear band rather than two with a dark stripe between them. A step
    // never opens two controls that are far apart.
    Rect u = holes[0];
    for (int i = 1; i < count; ++i) {
        const Rect& r = holes[i];
        const float x0 = u.x < r.x ? u.x : r.x;
        const float y0 = u.y < r.y ? u.y : r.y;
        const float x1 = (u.x + u.w) > (r.x + r.w) ? (u.x + u.w) : (r.x + r.w);
        const float y1 = (u.y + u.h) > (r.y + r.h) ? (u.y + u.h) : (r.y + r.h);
        u = Rect{x0, y0, x1 - x0, y1 - y0};
    }
    const float pad = 6.0f;
    u = Rect{u.x - pad, u.y - pad, u.w + 2 * pad, u.h + 2 * pad};

    draw_rect(0, 0, w, u.y, dim);
    draw_rect(0, u.y + u.h, w, h - (u.y + u.h), dim);
    draw_rect(0, u.y, u.x, u.h, dim);
    draw_rect(u.x + u.w, u.y, w - (u.x + u.w), u.h, dim);
    draw_rect_outline(u.x, u.y, u.w, u.h, palette::accent(), 2.0f);
}

}  // namespace

int Tutorial::step_count() { return kStepCount; }

void Tutorial::start(TutorialSection from) {
    // The enciphering screen needs a machine that has been built, and a
    // restart threw the old one away, so that part resumes at the start
    // of setup. It is seven short steps and it ends on the very screen
    // the operator left.
    if (from == TutorialSection::Cipher) from = TutorialSection::Setup;
    active_ = true;
    finished_ = false;
    skip_requested_ = false;
    step_ = first_step_of(from);
    snapshot_ = TutorialFacts{};
    gate_count_ = 0;
    clear_focus_gate();
}

void Tutorial::skip() {
    active_ = false;
    skip_requested_ = false;
    gate_count_ = 0;
    clear_focus_gate();
}

TutorialSection Tutorial::section() const {
    if (step_ >= kStepCount) return TutorialSection::Cipher;
    return kSteps[step_].section;
}

bool Tutorial::step_done(const TutorialFacts& f, const GuiInput& in) const {
    const Step& s = kSteps[step_];
    switch (s.advance) {
        case Advance::ClickTarget: {
            // Only a click that landed inside the gate counts, and only
            // the gate can be clicked at all, so this is the same thing
            // as "the control was worked". Enter counts as well, for an
            // operator who never touches the pointer.
            for (int i = 0; i < gate_count_; ++i) {
                if (in.mouse_pressed && rect_contains(gate_[i], in.mouse_x, in.mouse_y))
                    return true;
                if (in.key_enter && keyboard_focus_inside(gate_[i])) return true;
            }
            return false;
        }
        case Advance::OnMaintenance: return f.screen == TutorialScreen::Maintenance;
        case Advance::WheelsConfirming: return f.wheels_confirming || f.wheels_written;
        case Advance::WheelsWritten: return f.wheels_written;
        case Advance::OnMainMenu: return f.screen == TutorialScreen::MainMenu;
        case Advance::OnSetup: return f.screen == TutorialScreen::Setup;
        case Advance::SetupReady: return f.setup_ready;
        case Advance::LanguageChanged: return f.language_code != snapshot_.language_code;
        // Every one of these three waits for the fields to be sound again
        // as well as changed, and sound means everything except the master
        // key: the key is fixed last, in the step after them, because the
        // rotor count is what decides how long it has to be.
        //
        // Waiting on soundness is also what makes each of them
        // recoverable. A wheel that clashes with another row, a rotor
        // count that left empty rows, half of a plugboard pair -- each is
        // put right with the very control the step opened, so nothing the
        // operator can do here strands them.
        case Advance::RotorOneChanged:
            return f.rotor_one != snapshot_.rotor_one && f.setup_fields_ok;
        case Advance::RotorCountChanged:
            return f.rotor_count != snapshot_.rotor_count && f.setup_fields_ok;
        case Advance::PlugboardChanged:
            // The pair has to be finished, not merely started: one half of
            // a pair is not a swap.
            return f.plugboard != snapshot_.plugboard && f.setup_fields_ok;
        case Advance::MasterKeyChanged:
            return f.master_key != snapshot_.master_key && f.setup_ready;
        case Advance::OnCipher: return f.screen == TutorialScreen::Cipher;
        case Advance::HasMessage: return f.has_message;
        case Advance::HasCipher: return f.has_cipher;
        case Advance::CipherPasted: return f.cipher_pasted;
        case Advance::MarkerPasted: return f.marker_pasted;
        case Advance::HasPlain: return f.has_plain;
        case Advance::LastStep: return false;  // the Done button ends it
    }
    return false;
}

void Tutorial::set_gate_for_step() {
    gate_count_ = 0;
    clear_focus_gate();
    if (!active_) return;

    const Step& s = kSteps[step_];
    const char* names[2] = {s.target, s.target2};
    for (const char* name : names) {
        if (name == nullptr || name[0] == '\0') continue;
        Rect r{0, 0, 0, 0};
        if (landmark(name, &r)) gate_[gate_count_++] = r;
    }

    if (gate_count_ == 0) {
        // The control is not on the screen yet. A screen change is still
        // travelling, or it is scrolled out of sight. Shut everything
        // rather than opening everything: a gate of no rects is no gate
        // at all, and the operator would be free to wander off mid-step.
        // Scrolling is never gated, so a control below the fold can still
        // be brought into view.
        add_focus_gate(Rect{-1.0f, -1.0f, 0.0f, 0.0f});
        return;
    }
    for (int i = 0; i < gate_count_; ++i) add_focus_gate(gate_[i]);
}

void Tutorial::begin_frame(const TutorialFacts& facts, const GuiInput& in) {
    skip_requested_ = false;
    if (!active_) {
        clear_focus_gate();
        return;
    }

    // Judged on the input of the frame just gone, never on this one. The
    // screen has not drawn yet, so a click that arrived this frame has
    // not reached the button it was aimed at: advancing on it here would
    // move the gate off that button before it ever saw the click.
    if (step_done(facts, last_in_)) {
        ++step_;
        if (step_ >= kStepCount) {
            // Cannot happen: the last step waits on its own button rather
            // than on a fact. Guarded anyway, because running off the end
            // of the table would read past it.
            step_ = kStepCount - 1;
        }
        snapshot_ = facts;
    }
    last_in_ = in;
    set_gate_for_step();
}

void Tutorial::draw(const GuiInput& in, int width, int height) {
    if (!active_) return;

    const float w = static_cast<float>(width), h = static_cast<float>(height);
    draw_spotlight(gate_, gate_count_, w, h);

    // Everything from here is the tutorial talking, and it answers even
    // though the screen underneath does not.
    begin_gate_bypass();

    const Step& s = kSteps[step_];
    const float box_w = kBubbleW < w - 2 * kBubbleMargin ? kBubbleW : w - 2 * kBubbleMargin;
    const std::vector<std::string>& lines = wrap_text(box_w - 2 * kBubblePad, s.text);
    const float box_h = 2 * kBubblePad + kRowH + static_cast<float>(lines.size()) * kBubbleLine +
                        10.0f + kBtnH;

    // Along the bottom, unless the control the step opened is down there,
    // in which case the bubble moves to the top and gets out of its way.
    bool low = false;
    for (int i = 0; i < gate_count_; ++i)
        if (gate_[i].y + gate_[i].h > h - box_h - 2 * kBubbleMargin) low = true;
    const float box_x = (w - box_w) * 0.5f;
    const float box_y = low ? kBubbleMargin : h - box_h - kBubbleMargin;

    draw_rect(box_x, box_y, box_w, box_h, palette::panel());
    draw_rect_outline(box_x, box_y, box_w, box_h, palette::accent());

    float y = box_y + kBubblePad;
    label(Rect{box_x + kBubblePad, y, box_w - 2 * kBubblePad, kRowH},
          "Step " + std::to_string(step_ + 1) + " of " + std::to_string(kStepCount), true);
    y += kRowH;
    for (const std::string& line : lines) {
        label(Rect{box_x + kBubblePad, y, box_w - 2 * kBubblePad, kBubbleLine}, line);
        y += kBubbleLine;
    }
    y += 10.0f;

    // Skip is on the screen from step one. Nobody should have to sit
    // through part of this to find the way out of it.
    if (text_link(Rect{box_x + kBubblePad, y, 120.0f, kBtnH}, "Skip tutorial", in, true))
        skip_requested_ = true;

    if (kSteps[step_].advance == Advance::LastStep) {
        if (button(Rect{box_x + box_w - kBubblePad - kBtnW, y, kBtnW, kBtnH}, "Done", in, true,
                   true)) {
            active_ = false;
            finished_ = true;
            clear_focus_gate();
        }
    }

    end_gate_bypass();
}

}  // namespace gui
}  // namespace inop
