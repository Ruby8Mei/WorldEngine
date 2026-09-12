// gui_self_test.cpp — the parts of the GUI that can be checked without a
// window, run as part of `inop --self-test`.
//
// Everything here is pure logic that happens to live in a GUI-only source
// file: parsing a preferences file, parsing a script, and the saved
// configuration store. None of it draws, so none of it needs a GL context
// and none of it can be skipped for want of one.
//
// Compiled only when INOP_WITH_GUI is on. A CLI-only build gets the empty
// version in gui_stub.cpp instead, the same way it gets the empty
// run_gui_settings(), so main.cpp needs no #ifdef either way.
//
// What is deliberately not here: anything that measures text. wrap_lines()
// asks the baked font atlas how wide a character is, and with no atlas
// every character is nought wide, so a headless check of it would pass on
// nonsense. resolve_focus() is not here either, because the rects it works
// on are registered by focus_register(), which the header does not expose;
// widening the public interface for a test would be the wrong trade.
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>

#include "gui.hpp"
#include "audio_manager.hpp"
#include "gui_enciphering_panel.hpp"
#include "gui_config_store.hpp"
#include "gui_prefs.hpp"
#include "gui_script.hpp"
#include "gui_setup_panel.hpp"
#include "gui_tutorial.hpp"
#include "gui_widgets.hpp"

namespace inop {

namespace {

// A file under the working directory that only this suite writes, deleted
// as soon as the check that needed it is done. A check that reads a file
// has to write one first, and leaving it behind would make the next run
// depend on the last.
std::string write_temp(const std::string& name, const std::string& body) {
    const std::string path = "inop_selftest_" + name;
    std::ofstream f(path, std::ios::binary);
    f << body;
    return path;
}

void drop_temp(const std::string& path) { std::remove(path.c_str()); }

}  // namespace

void gui_self_test(const SelfTestCheck& check) {
    using namespace inop::gui;
    text_edit_self_test(check);
    audio_self_test(check);
    EncipheringPanel::self_test(check);

    // ── preferences ────────────────────────────────────────────────────
    {
        GuiPrefs wrote;
        wrote.theme = Theme::Light;
        wrote.colourblind = ColourblindMode::Tritanopia;
        wrote.window_mode = WindowMode::Windowed;
        wrote.vsync = false;
        wrote.frame_rate_limit = 180;
        wrote.zoom_percent = 135;
        wrote.reduced_motion = true;
        wrote.audio_muted = true;
        wrote.audio_volume = 40;
        const std::string path = "inop_selftest_prefs.json";
        const bool saved = save_prefs(wrote, path);
        GuiPrefs read;
        const bool loaded = load_prefs(read, path);
        drop_temp(path);
        check(saved && loaded && read == wrote, "prefs survive a save and a load unchanged");
    }
    {
        bool roundtrips = true;
        for (int limit : frame_rate_limits()) {
            for (bool vsync : {false, true}) {
                GuiPrefs wrote;
                wrote.vsync = vsync;
                wrote.frame_rate_limit = limit;
                const std::string path = "inop_selftest_frame_prefs.json";
                const bool saved = save_prefs(wrote, path);
                GuiPrefs read;
                const bool loaded = load_prefs(read, path);
                drop_temp(path);
                roundtrips = roundtrips && saved && loaded && read == wrote;
            }
        }
        check(roundtrips, "every frame limit, including 180 FPS and Unlimited, persists with either V-Sync mode");
        const std::string path = write_temp("old_frame_prefs.json", "{}");
        GuiPrefs old;
        old.vsync = false;
        old.frame_rate_limit = 30;
        const bool loaded = load_prefs(old, path);
        drop_temp(path);
        check(loaded && old.vsync && old.frame_rate_limit == 0,
              "older preferences keep V-Sync on with no additional frame cap");
        bool invalid_ignored = true;
        for (const std::string value : {"-1", "181", "180.5", "true", "\"180\"", "18446744073709551615"}) {
            const std::string invalid_path = write_temp("invalid_frame_prefs.json",
                "{\"vsync\":\"off\",\"frame_rate_limit\":" + value + "}");
            GuiPrefs p;
            invalid_ignored = load_prefs(p, invalid_path) && p.vsync &&
                              p.frame_rate_limit == 0 && invalid_ignored;
            drop_temp(invalid_path);
        }
        check(invalid_ignored, "malformed or unsupported frame settings safely retain defaults");
        GuiPrefs changed;
        changed.vsync = false;
        check(changed != GuiPrefs{}, "a V-Sync edit enables Apply");
        changed = GuiPrefs{};
        changed.frame_rate_limit = 180;
        check(changed != GuiPrefs{}, "a frame-limit edit enables Apply");
        bool pacing = frame_delay_seconds(0, 0) == 0 && frame_delay_seconds(-1, 0) == 0;
        for (int limit : frame_rate_limits()) {
            if (limit == 0) continue;
            const double budget = 1.0 / static_cast<double>(limit);
            pacing = pacing && std::abs(frame_delay_seconds(limit, budget / 4) - 3 * budget / 4) < 1e-10 &&
                     frame_delay_seconds(limit, budget) == 0 &&
                     frame_delay_seconds(limit, budget * 2) == 0;
        }
        check(pacing, "frame pacing subtracts rendering and V-Sync time and never delays a late or unlimited frame");
    }
    {
        // Every field here is one a hand edited file could carry and the
        // control could not produce. None of them may be taken at face
        // value: a zoom of 900 would scale the interface past any way back
        // to the settings screen that could undo it.
        const std::string path = write_temp(
            "prefs_bad.json",
            "{\"zoom\":900,\"font\":\"no-such-face.ttf\",\"colourblind\":\"red-green\","
            "\"audio\":{\"muted\":true,\"volume\":900}}");
        GuiPrefs p;
        const bool loaded = load_prefs(p, path);
        drop_temp(path);
        check(loaded && p.zoom_percent == 100, "a zoom no control offers is ignored");
        check(loaded && p.font_file == GuiPrefs{}.font_file,
              "a font this machine does not have falls back to the default");
        check(loaded && p.colourblind == ColourblindMode::Deuteranopia,
              "the older red-green name still reads as deuteranopia");
        check(loaded && p.audio_muted && p.audio_volume == 100,
              "audio preferences validate mute and clamp master volume");
    }
    {
        const std::string path = write_temp("prefs_off.json", "{\"colourblind\":\"off\"}");
        GuiPrefs p;
        const bool loaded = load_prefs(p, path);
        drop_temp(path);
        check(loaded && p.colourblind == ColourblindMode::Full,
              "the older off name still reads as full colour");
    }
    {
        const std::string path = write_temp("prefs_junk.json", "this is not json");
        GuiPrefs p;
        const bool loaded = load_prefs(p, path);
        drop_temp(path);
        check(!loaded, "a preferences file that is not JSON is refused");
    }
    {
        GuiPrefs p;
        check(!load_prefs(p, "inop_selftest_nothing_here.json"),
              "a preferences file that is not there is refused");
    }

    // ── where a font file is looked for ────────────────────────────────
    {
        check(!font_path(GuiPrefs{}.font_file).empty(),
              "font_path finds the portable default face");
        check(font_path("definitely-not-a-face.ttf").empty(),
              "font_path comes back empty for a face nobody has");
    }

    // ── the script parser refuses what it cannot run ───────────────────
    {
        struct Bad {
            const char* body;
            const char* what;
        };
        const Bad bad[] = {
            {"move 10\n", "move with only one coordinate is refused"},
            {"type\n", "type with nothing to type is refused"},
            {"key\n", "key with no key named is refused"},
            {"key sideways\n", "a key nobody has is refused"},
            {"ctrl maybe\n", "ctrl takes on or off and nothing else"},
            {"scroll\n", "scroll with no amount is refused"},
            {"wait\n", "wait with no number is refused"},
            {"wait -1\n", "a wait that runs backwards is refused"},
            {"shot\n", "shot with no name is refused"},
            {"shot sub/dir\n", "a shot name carrying a path is refused"},
            {"banana\n", "a verb the parser does not know is refused"},
        };
        for (const Bad& b : bad) {
            const std::string path = write_temp("script_bad.txt", b.body);
            InputScript s;
            std::string err;
            const bool ok = s.load(path, &err);
            drop_temp(path);
            check(!ok && !err.empty(), b.what);
        }
    }
    {
        InputScript s;
        std::string err;
        check(!s.load("inop_selftest_no_script.txt", &err),
              "a script file that is not there is refused");
    }

    // ── the script parser runs what it accepts ─────────────────────────
    {
        const std::string path = write_temp("script_ok.txt",
                                            "# a comment, then a blank line\n"
                                            "\n"
                                            "move 40 60\n"
                                            "click\n"
                                            "type hello\n"
                                            "key enter\n"
                                            "ctrl on\n"
                                            "scroll 2\n"
                                            "shot a-picture\n"
                                            "quit\n");
        InputScript s;
        std::string err;
        const bool ok = s.load(path, &err);
        drop_temp(path);
        check(ok && err.empty(), "a script using every verb loads");
        if (ok) {
            GuiInput in;
            s.fill(in, 0.016f);
            check(in.mouse_x == 40 && in.mouse_y == 60, "move puts the pointer where it says");
            // A click is two frames because a real button is a down edge
            // and then an up edge, and no widget in here sees both at once.
            s.fill(in, 0.016f);
            const bool down = in.mouse_pressed && in.mouse_held && !in.mouse_released;
            s.fill(in, 0.016f);
            const bool up = in.mouse_released && !in.mouse_held && !in.mouse_pressed;
            check(down && up, "click is a down frame and then an up frame");
            s.fill(in, 0.016f);
            check(in.typed.size() == 5 && in.typed[0] == 'h' && in.typed[4] == 'o',
                  "type delivers the rest of the line");
            s.fill(in, 0.016f);
            check(in.key_enter && !in.key_escape, "key enter arrives as one keypress");
            s.fill(in, 0.016f);
            check(in.ctrl_held, "ctrl on is held rather than pressed");
            s.fill(in, 0.016f);
            check(in.scroll_y == 2 && in.ctrl_held,
                  "scroll carries its amount, and ctrl is still held");
            s.fill(in, 0.016f);
            check(s.pending_shot() == "a-picture", "shot names the picture it wants");
            check(!s.fill(in, 0.016f), "quit asks for the window to close");
        }
    }
    {
        // A wait is real elapsed time, not a count of frames, so a short
        // frame must not advance past it.
        const std::string path = write_temp("script_wait.txt", "wait 0.1\nkey enter\n");
        InputScript s;
        std::string err;
        const bool ok = s.load(path, &err);
        drop_temp(path);
        GuiInput in;
        s.fill(in, 0.02f);
        // The frame that finishes the wait is still the waiting frame. The
        // step after it gets the next one, which is why this looks for the
        // keypress rather than assuming which frame carries it.
        const bool held = !in.key_enter;
        int frames = 0;
        while (frames < 20 && !in.key_enter) {
            s.fill(in, 0.02f);
            ++frames;
        }
        check(ok && held && in.key_enter && frames >= 4,
              "a wait holds for its seconds and then lets go");
    }

    // ── the saved configuration store ──────────────────────────────────
    {
        // A default panel state has no rotors picked, so it is exactly the
        // incomplete configuration load_config() exists to refuse.
        // load_config() is the sole authority on whether a file is
        // corrupted, and the tile browser shows its popup on that answer
        // alone, so what it refuses matters as much as what it accepts.
        PanelState empty_state;
        const std::string name = "inop-selftest.json";
        std::string save_err;
        const bool saved = save_config(empty_state, name, &save_err);
        check(saved, "a configuration writes to setup/");
        check(saved && config_exists(name), "a saved configuration is found again by name");

        PanelState back;
        std::string load_err;
        const bool loaded = load_config("setup/" + name, back, &load_err);
        check(!loaded && !load_err.empty(),
              "a configuration with no rotors picked is refused, with a reason");

        std::string del_err;
        const bool deleted = delete_config("setup/" + name, &del_err);
        check(deleted, "a saved configuration deletes");
        check(deleted && !config_exists(name), "a deleted configuration is gone");
    }
    {
        std::string err;
        check(!delete_config("setup/inop-selftest-never-existed.json", &err),
              "deleting a configuration that is not there is refused");
    }
    {
        const std::string path = write_temp("config_junk.json", "{ not a configuration");
        PanelState out;
        std::string err;
        const bool loaded = load_config(path, out, &err);
        drop_temp(path);
        check(!loaded && !err.empty(), "a configuration file that is not JSON is refused");
    }
    {
        // The suggestion is the lowest unused number for the suite, so it
        // must never name a file that is already sitting there.
        PanelState st;
        const std::string suggested = suggest_filename(st);
        check(!suggested.empty() && !config_exists(suggested),
              "the suggested filename is one that is not taken");
    }
    // ── the tutorial ───────────────────────────────────────────────────
    //
    // All of it is state, and none of it draws, so the whole walk can be
    // taken here rather than in a window. What cannot be checked here is
    // whether the landmarks name controls that exist -- a landmark is
    // only set while a panel draws, and nothing draws in this suite.
    {
        Tutorial t;
        t.start(TutorialSection::Maintenance);
        check(t.active() && t.step() == 0, "the tutorial starts on its first step");
        check(Tutorial::step_count() > 0, "the tutorial has steps");
    }
    {
        // The enciphering part cannot stand on its own: it needs a
        // machine that a restart threw away, so it resumes at the setup.
        Tutorial t;
        t.start(TutorialSection::Cipher);
        check(t.section() == TutorialSection::Setup,
              "resuming the enciphering part goes back to the setup part");
    }
    {
        Tutorial t;
        t.start(TutorialSection::Setup);
        check(t.section() == TutorialSection::Setup, "resuming the setup part starts there");
        t.skip();
        check(!t.active() && !t.finished(), "a skipped tutorial is neither running nor finished");
        check(t.section() == TutorialSection::Setup, "a skipped tutorial remembers its part");
    }
    {
        // A fact moves the first step on, and a click inside the gate
        // moves the second one, which is both ways a step can be
        // answered. The click is judged on the frame before, so it takes
        // two calls: one to be seen, one to be acted on.
        Tutorial t;
        t.start(TutorialSection::Maintenance);
        TutorialFacts facts;
        GuiInput quiet;
        facts.screen = TutorialScreen::Maintenance;
        t.begin_frame(facts, quiet);
        check(t.step() == 1, "arriving at maintenance answers the first step");

        set_landmark("maint.rotor_count", Rect{10.0f, 10.0f, 100.0f, 20.0f});
        resolve_focus(quiet);  // ages the landmark into the frame just gone

        GuiInput click;
        click.mouse_pressed = true;
        click.mouse_x = 20.0;
        click.mouse_y = 15.0;
        t.begin_frame(facts, click);
        check(t.step() == 1, "a click is not acted on in the frame it arrives");
        t.begin_frame(facts, quiet);
        check(t.step() == 2, "a click inside the gate answers the step");
    }
    {
        // The gate itself. A control outside it is not somewhere the
        // focus can land, which is what stops the arrows walking out of a
        // step, and it is put back the moment the gate comes down.
        const Rect inside{10.0f, 10.0f, 50.0f, 20.0f};
        const Rect outside{200.0f, 200.0f, 50.0f, 20.0f};
        clear_focus_gate();
        check(!focus_gate_on(), "no gate is up to begin with");
        add_focus_gate(Rect{0.0f, 0.0f, 100.0f, 100.0f});
        check(focus_gate_on(), "a gate goes up");
        check(!focus_gate_blocks(inside), "a control inside the gate still answers");
        check(focus_gate_blocks(outside), "a control outside the gate does not");
        begin_gate_bypass();
        check(!focus_gate_blocks(outside), "the tutorial bubble ignores the gate");
        end_gate_bypass();
        clear_focus_gate();
        check(!focus_gate_blocks(outside), "the gate coming down puts everything back");
    }
    {
        // The whole table, walked end to end. Every step is answered with
        // the fact that step waits for, so a step nothing could ever
        // satisfy shows up here as the walk stopping short rather than as
        // an operator stuck in a window with only Skip for a way out.
        //
        // The landmarks are all planted first, because a gate with
        // nothing to point at closes over the whole screen and the one
        // step that waits for a click would have nowhere to be clicked.
        const Rect here{10.0f, 10.0f, 100.0f, 20.0f};
        const char* names[] = {"menu.open",         "menu.maintenance",   "screen.wordmark",
                               "maint.rotor_count", "maint.rotor_generate", "setup.generate",
                               "setup.language",    "setup.rotor_one",    "setup.rotor_count",
                               "setup.rotor_grid",  "setup.plugboard",    "setup.master_key",
                               "setup.next",        "cipher.message",     "cipher.encipher",
                               "cipher.copy_cipher", "cipher.paste_cipher", "cipher.copy_marker",
                               "cipher.paste_marker", "cipher.decipher"};
        for (const char* n : names) set_landmark(n, here);
        GuiInput quiet;
        resolve_focus(quiet);

        GuiInput click;
        click.mouse_pressed = true;
        click.mouse_x = 20.0;
        click.mouse_y = 15.0;

        Tutorial t;
        t.start(TutorialSection::Maintenance);
        TutorialFacts f;
        int reached = 0;
        bool walked = true;

        // Each entry is what the operator does, and the step it answers.
        // A step that will not move on leaves `walked` false and names
        // itself in the failure.
        auto answer = [&](const GuiInput& in) {
            const int was = t.step();
            t.begin_frame(f, in);
            // A click is judged on the frame after it arrives, so it
            // takes a second call to be acted on.
            if (t.step() == was) t.begin_frame(f, quiet);
            if (t.step() != was + 1) walked = false;
            reached = t.step();
        };

        f.screen = TutorialScreen::Maintenance;      answer(quiet);   // 1
        answer(click);                                                // 2
        f.wheels_confirming = true;                  answer(quiet);   // 3
        f.wheels_written = true;                     answer(quiet);   // 4
        f.screen = TutorialScreen::MainMenu;         answer(quiet);   // 5
        f.screen = TutorialScreen::Setup;            answer(quiet);   // 6
        f.setup_fields_ok = true;
        f.setup_ready = true;                        answer(quiet);   // 7
        f.language_code = "spa";                     answer(quiet);   // 8
        f.rotor_one = "U12";                         answer(quiet);   // 9
        f.rotor_count = 5;                           answer(quiet);   // 10
        f.plugboard = "ab";                          answer(quiet);   // 11
        f.master_key = "abcdef";                     answer(quiet);   // 12
        f.screen = TutorialScreen::Cipher;           answer(quiet);   // 13
        f.has_message = true;                        answer(quiet);   // 14
        f.has_cipher = true;                         answer(quiet);   // 15
        f.cipher_pasted = true;                      answer(quiet);   // 16
        f.marker_pasted = true;                      answer(quiet);   // 17
        f.has_plain = true;                          answer(quiet);   // 18

        check(walked, "every step of the tutorial can be answered");
        check(reached == Tutorial::step_count() - 1,
              "answering every step reaches the last one");

        // The closing step waits for its own button and for nothing else,
        // so no fact may run off the end of the table.
        t.begin_frame(f, quiet);
        check(t.step() == Tutorial::step_count() - 1, "the last step stays until it is answered");
    }
    {
        // Half of the setup steps wait for the fields to be sound as well
        // as changed. Without that, a rotor count that left empty rows
        // would move the walk on to a master key it could never make
        // valid, and the box that would fix it is not the one the step
        // opened.
        const Rect here{10.0f, 10.0f, 100.0f, 20.0f};
        set_landmark("setup.rotor_count", here);
        set_landmark("setup.rotor_grid", here);
        GuiInput quiet;
        resolve_focus(quiet);

        Tutorial t;
        t.start(TutorialSection::Setup);
        TutorialFacts f;
        f.screen = TutorialScreen::Setup;
        f.setup_fields_ok = true;
        f.setup_ready = true;
        t.begin_frame(f, quiet);  // open INOP
        t.begin_frame(f, quiet);  // generate setup
        f.language_code = "spa";
        t.begin_frame(f, quiet);  // language
        f.rotor_one = "U12";
        t.begin_frame(f, quiet);  // rotor one
        const int at_count = t.step();
        // The count changes and leaves the fields unsound, which is what
        // asking for more rotors does.
        f.rotor_count = 9;
        f.setup_fields_ok = false;
        f.setup_ready = false;
        t.begin_frame(f, quiet);
        check(t.step() == at_count, "a rotor count that left empty rows does not move on");
        f.setup_fields_ok = true;
        t.begin_frame(f, quiet);
        check(t.step() == at_count + 1, "filling the new rows in moves it on");
    }
    {
        // The three tutorial fields ride in the preferences file with
        // everything else, and a hand edited section number cannot index
        // past the end of the step table.
        GuiPrefs wrote;
        wrote.tutorial_done = true;
        wrote.tutorial_section = 2;
        wrote.tutorial_launches = 3;
        const std::string path = "inop_selftest_tut.json";
        const bool saved = save_prefs(wrote, path);
        GuiPrefs read;
        const bool loaded = load_prefs(read, path);
        drop_temp(path);
        check(saved && loaded && read == wrote, "the tutorial state survives a save and a load");

        const std::string bad = write_temp("prefs_tut_bad.json",
                                           "{\"tutorial\":{\"section\":9,\"launches\":-4}}");
        GuiPrefs p;
        const bool bad_loaded = load_prefs(p, bad);
        drop_temp(bad);
        check(bad_loaded && p.tutorial_section == 2, "a section past the end is clamped");
        check(bad_loaded && p.tutorial_launches == 0, "a negative launch count is clamped");
    }
}

}  // namespace inop
