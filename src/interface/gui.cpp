// gui.cpp — GLFW window/context creation and the main loop for the
// optional GUI. Compiled only when INOP_WITH_GUI is ON.
#include "gui.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Older MinGW/SDK headers may not declare this constant even though the
// function exists on Windows 10 1703+. Without it, a DPI-unaware process
// gets its window bitmap-scaled by DWM at non-100% display scaling — the
// window looks shrunk and content clips at the edges, because GLFW still
// lays widgets out for the full pixel size while DWM presents a scaled
// copy. Declaring per-monitor-v2 awareness up front avoids that entirely.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif
#endif

// IMPORTANT: no GLFW_CONTEXT_VERSION_MAJOR/MINOR or GLFW_OPENGL_PROFILE
// hints are ever set below. Requesting a specific version/profile from
// GLFW would silently break every fixed-function draw call in
// gui_render.cpp (glBegin/glOrtho/glTexImage2D/...) — see gui_render.hpp's
// header comment for the full reasoning. Leaving these hints untouched is
// what makes WGL hand back the driver's default compatibility context.
#include <GLFW/glfw3.h>

// Only for the screenshot a scripted run can ask for. It arrives through
// the stb package this build already depends on for the font atlas, so it
// costs no new dependency.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "gui_anim.hpp"
#include "gui_enciphering_panel.hpp"
#include "gui_main_menu.hpp"
#include "gui_maintenance_panel.hpp"
#include "gui_prefs.hpp"
#include "gui_render.hpp"
#include "gui_script.hpp"
#include "gui_settings_panel.hpp"
#include "gui_setup_panel.hpp"
#include "gui_widgets.hpp"

namespace inop {

namespace {

gui::GuiInput g_input;

void char_callback(GLFWwindow*, unsigned int codepoint) { g_input.typed.push_back(codepoint); }

void key_callback(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_BACKSPACE) g_input.key_backspace = true;
    if (key == GLFW_KEY_DELETE) g_input.key_delete = true;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_input.key_enter = true;
    if (key == GLFW_KEY_ESCAPE) g_input.key_escape = true;
    if (key == GLFW_KEY_LEFT) g_input.key_left = true;
    if (key == GLFW_KEY_RIGHT) g_input.key_right = true;
    if (key == GLFW_KEY_UP) g_input.key_up = true;
    if (key == GLFW_KEY_DOWN) g_input.key_down = true;
    // Recorded here rather than read out of the typed characters, because
    // Windows sends no character event while Control is held. GLFW_KEY_A
    // through GLFW_KEY_Z are the ASCII codes for the uppercase letters, so
    // the key is already the letter it stands for.
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) g_input.key_letter = static_cast<char>(key);
}

void scroll_callback(GLFWwindow*, double /*xoffset*/, double yoffset) { g_input.scroll_y += yoffset; }

void framebuffer_size_callback(GLFWwindow*, int width, int height) { gui::set_viewport(width, height); }

// The one place a real pointer, a real mouse button and a real Control key
// reach this interface. Everything downstream is handed the struct this
// fills and cannot tell where the contents came from, which is exactly
// what lets gui_script.hpp offer a scripted sibling without any other part
// of the GUI changing.
void fill_input_from_glfw(GLFWwindow* window, gui::GuiInput& in, float scale,
                          bool& mouse_down_prev) {
    // Cursor position arrives in real pixels while every widget lays
    // itself out in logical units, so it has to come back through the zoom
    // before any hit test sees it.
    double mx = 0, my = 0;
    glfwGetCursorPos(window, &mx, &my);
    in.mouse_x = mx / scale;
    in.mouse_y = my / scale;

    bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    in.mouse_pressed = down && !mouse_down_prev;
    in.mouse_released = !down && mouse_down_prev;
    // Held as well as the two edges: a dip has to last as long as the
    // button is down, which neither edge can say on its own.
    in.mouse_held = down;
    mouse_down_prev = down;

    in.ctrl_held = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    in.shift_held = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

// A picture of the frame that has just been drawn, read out of the back
// buffer before it is swapped. The application photographing itself rather
// than something outside grabbing the screen, so a shot lands on exactly
// the frame that asked for one instead of racing it, and so nothing has to
// be in front or in focus for it to work.
void save_screenshot(int w, int h, const std::string& name) {
    if (w <= 0 || h <= 0) return;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) *
                                      static_cast<std::size_t>(h) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::error_code ec;
    std::filesystem::create_directories("gui-shots", ec);
    const std::string path = "gui-shots/" + name + ".png";
    // GL hands a buffer back bottom row first, which is upside down to
    // every image format there is.
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.c_str(), w, h, 3, pixels.data(), w * 3))
        std::cerr << "gui: could not write " << path << "\n";
}

// Where the window sits and how big it is while it is an ordinary window,
// captured once at startup. Going fullscreen throws that away, so it is
// kept here to come back to rather than re-derived from a window that is
// currently covering the whole screen.
struct WindowedGeometry {
    int x = 0, y = 0, w = 0, h = 0;
};

void apply_window_mode(GLFWwindow* window, gui::WindowMode mode, const WindowedGeometry& geom) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = monitor ? glfwGetVideoMode(monitor) : nullptr;
    switch (mode) {
        case gui::WindowMode::Fullscreen:
            // Exclusive: the window takes the monitor, at the mode the
            // monitor is already running, so no resolution switch happens
            // and nothing else on the desktop gets rearranged.
            if (monitor && vm)
                glfwSetWindowMonitor(window, monitor, 0, 0, vm->width, vm->height,
                                     vm->refreshRate);
            break;
        case gui::WindowMode::BorderlessFullscreen: {
            // Still an ordinary window as far as the compositor is
            // concerned, just an undecorated one covering the monitor,
            // which is what keeps alt-tab and overlays behaving.
            if (!vm) break;
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitor, &mx, &my);
            // Decoration comes off first: asking for the monitor size
            // while the window still has a title bar sizes the frame,
            // not the content, and taking the bar away afterwards leaves
            // the window short by exactly the height of the bar.
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(window, nullptr, mx, my, vm->width, vm->height, 0);
            break;
        }
        case gui::WindowMode::Windowed:
        default:
            glfwSetWindowMonitor(window, nullptr, geom.x, geom.y, geom.w, geom.h, 0);
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
            break;
    }
}

// -- the console window ----------------------------------------------
//
// INOP is a console program that opens a window, so launching it puts a
// terminal on screen beside the GUI. That terminal is only ever wanted on
// the way back out to the CLI menu, so it is hidden while the window is up
// and shown again when the operator asks for the terminal. The operation
// being symmetric is what makes the main menus Terminal button nearly
// free: leaving the GUI is what brings the console back, so nothing has to
// be spawned or relaunched.
//
// The console still exists at process start, so there is a brief flash
// before the call below runs. Removing that flash needs -mwindows
// subsystem linking and a GUI-as-host restructure, both of which are far
// more work than the flash is worth.
#if defined(_WIN32)

bool g_console_hidden = false;

// One process attached means INOP created this console and owns it, so
// hiding it takes away nothing that was already there. More than one means
// the binary was launched from a shell that was open before it, and hiding
// would take that terminal with it. This is also what keeps
// inop --self-test printing normally when it is run from a terminal.
bool owns_console() {
    DWORD pids[2] = {0, 0};
    return GetConsoleProcessList(pids, 2) == 1;
}

void hide_owned_console() {
    HWND console = GetConsoleWindow();
    if (!console || !owns_console() || !IsWindowVisible(console)) return;
    ShowWindow(console, SW_HIDE);
    g_console_hidden = true;
}

void restore_console() {
    if (!g_console_hidden) return;
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_SHOW);
    g_console_hidden = false;
}

#else

// Linux has no console subsystem to hide: a terminal there belongs to the
// shell that opened it, never to the process running inside it.
void hide_owned_console() {}
void restore_console() {}

#endif

// Terminal is the only way out that needs the console back. Quit ends the
// process immediately afterwards, so showing the window first would put a
// terminal on screen for exactly as long as it takes to tear one down,
// which is the thing being avoided in the first place.
GuiExit leave_gui(GuiExit reason) {
    if (reason == GuiExit::Terminal) restore_console();
    return reason;
}

}  // namespace

bool gui_available() { return true; }

// Every failure path below returns Terminal rather than Quit. The program
// now opens on the window, so a machine that cannot start GLFW at all
// would otherwise have no way to reach the cipher — falling through to the
// terminal keeps a broken graphics stack from making the whole binary
// unusable.
GuiExit run_gui_settings(const std::string& script_path) {
    // Parsed before anything else, and in particular before the console is
    // hidden, so that a bad line in a script is reported somewhere it can
    // still be read. A script that does not parse is refused whole rather
    // than run as far as the bad line.
    gui::InputScript script_storage;
    gui::InputScript* script = nullptr;
    if (!script_path.empty()) {
        std::string error;
        if (!script_storage.load(script_path, &error)) {
            std::cerr << "gui: " << error << "\n";
            return GuiExit::Terminal;
        }
        script = &script_storage;
    }

    // First thing in, so the console is on screen for as little time as
    // there is any way to manage.
    hide_owned_console();

#if defined(_WIN32)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    if (!glfwInit()) {
        std::cerr << "gui: glfwInit failed\n";
        return leave_gui(GuiExit::Terminal);
    }

    GLFWwindow* window = glfwCreateWindow(1400, 950, "INOP", nullptr, nullptr);
    if (!window) {
        std::cerr << "gui: glfwCreateWindow failed\n";
        glfwTerminate();
        return leave_gui(GuiExit::Terminal);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    gui::render_init();

    // Preferences decide how everything below looks, so they are read
    // before the first atlas is baked and the first frame is drawn. A
    // first run has no file and gets the defaults.
    gui::GuiPrefs prefs;
    gui::load_prefs(prefs);
    gui::palette::set_palette(prefs.theme, prefs.colourblind);

    if (!gui::load_fonts(prefs.font_file)) {
        // A stored typeface that has since been uninstalled must not cost
        // the operator the whole application, so the default is tried
        // before giving up, and Times New Roman after that. Times is the
        // last resort rather than the default because it is the face a
        // Windows install is least likely to be without, and a window with
        // no text at all is the one failure the operator cannot work
        // around.
        std::cerr << "gui: could not load font '" << prefs.font_file << "' — trying the default\n";
        prefs.font_file = gui::GuiPrefs{}.font_file;
        if (!gui::load_fonts(prefs.font_file)) {
            std::cerr << "gui: could not load the default font (" << prefs.font_file
                      << ") — trying times.ttf\n";
            prefs.font_file = "times.ttf";
            if (!gui::load_fonts(prefs.font_file)) {
                std::cerr << "gui: could not load any font — closing\n";
                glfwDestroyWindow(window);
                glfwTerminate();
                return leave_gui(GuiExit::Terminal);
            }
        }
    }

    // Captured before any fullscreen switch, so Windowed has somewhere to
    // come back to.
    WindowedGeometry geom;
    glfwGetWindowPos(window, &geom.x, &geom.y);
    glfwGetWindowSize(window, &geom.w, &geom.h);
    if (prefs.window_mode != gui::WindowMode::Windowed)
        apply_window_mode(window, prefs.window_mode, geom);

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    gui::set_viewport(fb_w, fb_h);

    gui::SetupPanel panel;
    gui::MainMenu main_menu;
    gui::EncipheringPanel enciphering;
    gui::SettingsPanel settings;
    gui::MaintenancePanel maintenance;
    // Owned here, not by any screen — a screen only knows how to signal
    // "the operator picked me" (open_inop_requested()/wordmark_clicked()/
    // next_clicked()/back_clicked()/exit_requested()), not what that means
    // for what gets shown next. See the header comment of
    // gui_setup_panel.hpp for why the screens themselves stay this narrow.
    // The GUI opens on MainMenu, not Setup directly.
    enum class Screen { MainMenu, Setup, Enciphering, Settings, Maintenance };
    Screen screen = Screen::MainMenu;

    // Draws one screen and nothing else -- no acting on what it reports,
    // because during a slide both screens draw and neither is being
    // worked. The ordinary path below still reads its screens answers
    // itself, since only that path can act on them.
    auto draw_screen = [&](Screen s, const gui::GuiInput& in, int w, int h) {
        switch (s) {
            case Screen::Setup: panel.frame(in, w, h); break;
            case Screen::Enciphering: enciphering.frame(in, w, h); break;
            case Screen::Settings: settings.frame(in, w, h); break;
            case Screen::Maintenance: maintenance.frame(in, w, h); break;
            default: main_menu.frame(in, w, h); break;
        }
    };

    // A screen change slides rather than snaps. Both screens are drawn for
    // the length of it, one going out and one coming in, offset by a whole
    // window width apart -- so they never overlap and neither has to be
    // clipped against the other.
    //
    // Most of the application is a line running left to right: deeper in
    // comes from the right and Back comes from the left, so Back reads as
    // an undoing of the step that got you there.
    //
    // Settings is the exception and moves on the other axis. It sits below
    // the main menu rather than beside it, so reaching it works like
    // scrolling down a list: the menu leaves through the top and the
    // settings come up from the bottom. Coming back reverses both.
    //
    // dir_x and dir_y are the direction the pair of screens travels, as
    // whole screen widths and heights. Exactly one of them is ever set.
    struct Transition {
        bool active = false;
        Screen from = Screen::MainMenu;
        float t = 0.0f;  // 0 to 1
        float dir_x = 1.0f;
        float dir_y = 0.0f;
    };
    Transition transition;
    const float kSwipeSeconds = 0.2f;

    auto go_to = [&](Screen next, float dir_x, float dir_y) {
        if (next == screen) return;
        // Reduced motion means the screen changes and nothing travels to
        // get there, which is the whole of what the setting promises.
        transition.active = gui::motion_enabled();
        transition.from = screen;
        transition.t = 0.0f;
        transition.dir_x = dir_x;
        transition.dir_y = dir_y;
        screen = next;
    };
    bool mouse_down_prev = false;
    // Quit unless the operator specifically asks for the terminal: the
    // window close button and Esc are both ways of saying "I am done", and
    // only the Terminal button means "carry on somewhere else".
    GuiExit exit_reason = GuiExit::Quit;

    // Modals belong to this loop rather than to a screen, because they
    // cover the whole window and the screen underneath must not react
    // while one is up. Only one is ever open.
    enum class Modal { None, ConfirmQuit, ZoomUnsupported };
    Modal modal = Modal::None;

    // Which modal was on screen when the previous frame finished. A modal
    // raised by a key would otherwise be handed the very key that raised
    // it, in the same frame, and answer it: Escape on the main menu opened
    // the quit box and cancelled it again, and Enter on the focused Exit
    // button opened it and confirmed it, quitting without ever showing the
    // question. Same shape as the dropdown popup latch in gui_widgets.cpp,
    // and the same rule — a thing has to have been drawn once before it is
    // allowed to answer for itself.
    Modal modal_shown_last = Modal::None;

    gui::set_ui_scale(static_cast<float>(prefs.zoom_percent) / 100.0f);
    gui::set_motion_enabled(!prefs.reduced_motion);

    // The frame clock every animation reads. Taken from GLFW rather than
    // from a chrono clock so that it shares an origin with the rest of the
    // windowing, and clamped below because dragging or resizing the window
    // stalls this loop: an animation handed a quarter second of elapsed
    // time jumps instead of moving.
    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float dt = static_cast<float>(now - last_time);
        last_time = now;
        const float kMaxFrameDt = 0.1f;
        if (dt > kMaxFrameDt) dt = kMaxFrameDt;
        if (dt < 0.0f) dt = 0.0f;
        gui::set_frame_dt(dt);

        const float scale = gui::ui_scale();
        if (script) {
            // Writes the whole struct rather than merging into it, so
            // whatever the callbacks accumulated during the poll above is
            // dropped. That is deliberate: a stray keystroke on the
            // machine running a script cannot derail the run. Running off
            // the end of the script closes the window, so a script can
            // never leave one stranded.
            if (!script->fill(g_input, dt)) glfwSetWindowShouldClose(window, GLFW_TRUE);
        } else {
            fill_input_from_glfw(window, g_input, scale, mouse_down_prev);
        }

        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        // The size the screens are told about is the logical one, so a
        // panel centring itself in "the window" centres in what the
        // operator can actually see at this zoom.
        const int lw = static_cast<int>(static_cast<float>(fb_w) / scale);
        const int lh = static_cast<int>(static_cast<float>(fb_h) / scale);

        // While a modal is up the screen behind still draws, so it stays
        // visible under the dimming, but it must not react to anything.
        gui::GuiInput screen_input = g_input;
        if (modal != Modal::None) {
            screen_input.mouse_pressed = false;
            screen_input.mouse_released = false;
            screen_input.typed.clear();
            screen_input.key_backspace = false;
            screen_input.key_delete = false;
            screen_input.key_enter = false;
            screen_input.key_escape = false;
            screen_input.key_left = screen_input.key_right = false;
            screen_input.key_up = screen_input.key_down = false;
            screen_input.key_letter = 0;
            screen_input.scroll_y = 0;
            // The pointer goes with the rest of it. Left in place, the
            // screen behind still reads as hovered under the dimming, so
            // controls light up through it and, now that there are
            // tooltips, one could surface from under a modal that is
            // supposed to have the screen to itself.
            screen_input.mouse_held = false;
            screen_input.mouse_x = screen_input.mouse_y = -1.0e6;
        }

        // Answered once, on the real input, before anything draws. A modal
        // has to be reachable from the keyboard as well, and the screen
        // behind one was just handed input with the arrows taken out.
        gui::resolve_focus(g_input);

        // The window belongs to this loop, so clearing it does too. It
        // used to be the first thing each screen did, which cannot work
        // once two of them draw in the same frame: the second clear would
        // wipe the first screen away.
        gui::clear(gui::palette::background());

        if (transition.active) {
            // Nothing reacts to anything mid-slide. A control travelling
            // under a still pointer would otherwise take a click that was
            // never aimed at it.
            gui::GuiInput dead;
            dead.mouse_x = dead.mouse_y = -1.0e6;

            transition.t += dt / kSwipeSeconds;
            if (transition.t >= 1.0f) {
                transition.t = 1.0f;
                transition.active = false;
            }
            // Smoothstep: fastest in the middle and still at both ends, so
            // the screen reads as having been carried rather than yanked.
            const float t = transition.t;
            const float e = t * t * (3.0f - 2.0f * t);
            // A whole screen apart, so the two never overlap and neither
            // has to be clipped against the other.
            const float sx = transition.dir_x * static_cast<float>(lw);
            const float sy = transition.dir_y * static_cast<float>(lh);

            gui::set_draw_offset(-e * sx, -e * sy);
            draw_screen(transition.from, dead, lw, lh);
            gui::set_draw_offset((1.0f - e) * sx, (1.0f - e) * sy);
            draw_screen(screen, dead, lw, lh);
            gui::set_draw_offset(0.0f, 0.0f);
        } else if (screen == Screen::Setup) {
            panel.frame(screen_input, lw, lh);
            if (panel.wordmark_clicked()) go_to(Screen::MainMenu, -1.0f, 0.0f);
            if (panel.next_clicked()) {
                enciphering.open(panel.state());
                go_to(Screen::Enciphering, 1.0f, 0.0f);
            }
        } else if (screen == Screen::Enciphering) {
            enciphering.frame(screen_input, lw, lh);
            // The clipboard is the one thing the screen needs GLFW for,
            // and GLFW stays in this file, so the screen asks and this
            // loop answers.
            std::string copy;
            if (enciphering.take_copy_request(&copy)) glfwSetClipboardString(window, copy.c_str());
            if (enciphering.paste_requested()) {
                const char* pasted = glfwGetClipboardString(window);
                enciphering.deliver_paste(pasted ? pasted : "");
            }
            if (enciphering.back_clicked()) {
                panel.open();
                go_to(Screen::Setup, -1.0f, 0.0f);
            }
            if (enciphering.wordmark_clicked()) go_to(Screen::MainMenu, -1.0f, 0.0f);
        } else if (screen == Screen::Settings) {
            settings.frame(screen_input, lw, lh);
            // The screen edits preferences and asks; the window, the font
            // atlases and the palette are all owned here, so putting a
            // change into force is this loop's job, the same way the
            // clipboard is.
            gui::GuiPrefs next;
            if (settings.take_apply_request(&next)) {
                bool font_ok = true;
                if (next.font_file != prefs.font_file &&
                    !gui::load_fonts(next.font_file)) {
                    gui::load_fonts(prefs.font_file);
                    next.font_file = prefs.font_file;
                    font_ok = false;
                }
                gui::palette::set_palette(next.theme, next.colourblind);
                if (next.window_mode != prefs.window_mode)
                    apply_window_mode(window, next.window_mode, geom);

                // The zoom ceiling is enforced here rather than in the
                // settings screen because it is a fact about the window,
                // which this loop owns. Refusing the value outright and
                // saying so beats applying a scale that puts half a panel
                // past the bottom edge with no way to reach the control
                // that would undo it.
                bool zoom_refused = false;
                if (next.zoom_percent > gui::kMaxSupportedZoom) {
                    next.zoom_percent = prefs.zoom_percent;
                    zoom_refused = true;
                    modal = Modal::ZoomUnsupported;
                }
                if (next.zoom_percent != prefs.zoom_percent)
                    gui::set_ui_scale(static_cast<float>(next.zoom_percent) / 100.0f);

                gui::set_motion_enabled(!next.reduced_motion);

                prefs = next;
                bool saved = gui::save_prefs(prefs);
                // Reopened so the screen shows what is actually in force,
                // which after a failed font is not quite what was asked
                // for.
                settings.open(prefs);
                if (!font_ok)
                    settings.set_status("That font could not be read. Everything else applied.",
                                        true);
                else if (zoom_refused)
                    settings.set_status("Everything except the zoom applied.", true);
                else if (!saved)
                    settings.set_status("Applied, but inop.gui.json could not be written.", true);
                else
                    settings.set_status("Applied, and saved to inop.gui.json.", false);
            }
            // Down to reach the settings, so up to leave them.
            // The wordmark alone now; the Back button that meant the same
            // thing on this one screen is gone.
            if (settings.wordmark_clicked())
                go_to(Screen::MainMenu, 0.0f, -1.0f);
        } else if (screen == Screen::Maintenance) {
            // Nothing to answer for this screen: it writes wheel files and
            // key sheets itself, and needs neither the window, the fonts
            // nor the clipboard to do it.
            maintenance.frame(screen_input, lw, lh);
            if (maintenance.back_clicked() || maintenance.wordmark_clicked())
                go_to(Screen::MainMenu, 1.0f, 0.0f);
        } else {
            main_menu.frame(screen_input, lw, lh);
            if (main_menu.open_inop_requested()) {
                panel.open();
                go_to(Screen::Setup, 1.0f, 0.0f);
            }
            if (main_menu.maintenance_requested()) {
                maintenance.open();
                // Left, where Setup goes right. The two screens the menu
                // opens sideways are told apart by which way they arrive,
                // so a glance at the movement says which one this is.
                go_to(Screen::Maintenance, -1.0f, 0.0f);
            }
            if (main_menu.settings_requested()) {
                settings.open(prefs);
                // The menu leaves through the top and the settings come up
                // from underneath it, the way scrolling down a list moves
                // what is on it upward.
                go_to(Screen::Settings, 0.0f, 1.0f);
            }
            if (main_menu.terminal_requested()) {
                exit_reason = GuiExit::Terminal;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            if (main_menu.exit_requested()) {
                if (g_input.ctrl_held) glfwSetWindowShouldClose(window, GLFW_TRUE);
                else modal = Modal::ConfirmQuit;
            }
        }

        // Above everything the screen drew, including any dropdown popup,
        // and below any modal, which is drawn after this and is entitled
        // to cover it.
        gui::draw_pending_tooltip(static_cast<float>(lw), static_cast<float>(lh));

        // Escape, but only when no modal is already holding it — an open
        // modal answers the key itself. Holding Control skips the question,
        // which is the bargain every warning in this interface offers.
        // An open dropdown list has first claim on Escape, and closes
        // itself. Answering here as well would close the list and leave
        // the screen in the same keypress.
        //
        // So does an overlay the screen drew itself, such as the setup
        // screen's save and load boxes: the screen has already drawn by
        // this line, so modal_layer_open() can say whether one is up, and
        // the box answers the key rather than the screen leaving out from
        // under it.
        if (modal == Modal::None && g_input.key_escape && !gui::dropdown_popup_open() &&
            !gui::modal_layer_open()) {
            if (g_input.ctrl_held) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else if (screen != Screen::MainMenu) {
                // A sub-screen has somewhere to go back to, so Escape means
                // up rather than out. Only the main menu, which has nothing
                // above it, reads Escape as leaving.
                // Each screen leaves the way its own Back button leaves, so
                // Escape and Back are never two different journeys out of
                // the same place.
                if (screen == Screen::Settings) go_to(Screen::MainMenu, 0.0f, -1.0f);
                else if (screen == Screen::Maintenance) go_to(Screen::MainMenu, 1.0f, 0.0f);
                else go_to(Screen::MainMenu, -1.0f, 0.0f);
            } else {
                modal = Modal::ConfirmQuit;
            }
        }

        // Modals draw last so they sit over whichever screen is behind, and
        // they get the real input that the screen was just denied — except
        // on the frame one opens, where the keys that opened it are held
        // back. It still draws, so the operator sees it immediately.
        gui::GuiInput modal_input = g_input;
        if (modal != modal_shown_last) {
            modal_input.key_enter = false;
            modal_input.key_escape = false;
            modal_input.mouse_pressed = false;
            modal_input.mouse_released = false;
        }
        modal_shown_last = modal;

        if (modal == Modal::ConfirmQuit) {
            // The settings screen holds an edit pending until Apply, and
            // leaving saves what is in force rather than what is pending,
            // so a modal that says everything is safe would be wrong in
            // exactly the case where being wrong costs something. It names
            // the loss instead, and the button stops offering to save what
            // it cannot save.
            const bool pending = settings.has_unapplied_changes();
            gui::ModalChoice choice = gui::modal_question(
                static_cast<float>(lw), static_cast<float>(lh), "Quit INOP?",
                pending ? "Settings you have not applied will be lost, and so will an "
                          "unsaved setup."
                        : "Your settings are saved. An unsaved setup is not.",
                pending ? "Quit anyway" : "Save & quit", "Cancel", modal_input);
            if (choice == gui::ModalChoice::Confirm)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            else if (choice == gui::ModalChoice::Cancel)
                modal = Modal::None;
        } else if (modal == Modal::ZoomUnsupported) {
            if (gui::modal_notice(static_cast<float>(lw), static_cast<float>(lh),
                                  "That zoom is not supported yet",
                                  "Above " + std::to_string(gui::kMaxSupportedZoom) +
                                      "% the panels do not fit the window. Zoom left unchanged.",
                                  modal_input))
                modal = Modal::None;
        }

        if (script && !script->pending_shot().empty())
            save_screenshot(fb_w, fb_h, script->pending_shot());

        glfwSwapBuffers(window);

        // Edge-triggered/accumulated input has now been consumed for this
        // frame — clear it before the next poll picks up new events.
        g_input.typed.clear();
        g_input.key_backspace = g_input.key_enter = g_input.key_escape = false;
        g_input.key_delete = false;
        g_input.key_letter = 0;
        g_input.key_left = g_input.key_right = g_input.key_up = g_input.key_down = false;
        g_input.scroll_y = 0;
    }

    // The preferences go to disk on the way out, however the operator
    // left. That is what lets the quit modal say Save and quit and mean
    // it: the button describes what leaving does rather than being the one
    // door that happens to save. It is usually a write of what is already
    // in the file, since Apply saved it at the time, and that is the
    // point. Holding Control to skip the question skips the question and
    // not the save, which is what Control means everywhere else in here.
    //
    // What is saved is what is in force, never an edit still sitting on
    // the settings screen unapplied -- those never leave the panel, and
    // the quit modal says so when there is one. Note that the window close
    // button and Control-Escape reach this line without raising that modal
    // at all, so on those two paths an unapplied edit is dropped without
    // anything being said about it.
    //
    // A machine setup is deliberately not included. It is message
    // material, it is saved under a name the operator chooses, and writing
    // one out unasked would leave files in setup/ that nobody named.
    gui::save_prefs(prefs);

    gui::render_shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return leave_gui(exit_reason);
}

}  // namespace inop
