// gui.cpp — GLFW window/context creation and the main loop for the
// settings-panel GUI. Compiled only when INOP_WITH_GUI is ON.
#include "gui.hpp"

#include <iostream>

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

#include "gui_render.hpp"
#include "gui_settings_panel.hpp"
#include "gui_widgets.hpp"

namespace inop {

namespace {

gui::GuiInput g_input;

void char_callback(GLFWwindow*, unsigned int codepoint) { g_input.typed.push_back(codepoint); }

void key_callback(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (key == GLFW_KEY_BACKSPACE) g_input.key_backspace = true;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_input.key_enter = true;
    if (key == GLFW_KEY_ESCAPE) g_input.key_escape = true;
}

void scroll_callback(GLFWwindow*, double /*xoffset*/, double yoffset) { g_input.scroll_y += yoffset; }

void framebuffer_size_callback(GLFWwindow*, int width, int height) { gui::set_viewport(width, height); }

}  // namespace

void run_gui_settings() {
#if defined(_WIN32)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    if (!glfwInit()) {
        std::cerr << "gui: glfwInit failed\n";
        return;
    }

    GLFWwindow* window = glfwCreateWindow(1400, 950, "INOP settings", nullptr, nullptr);
    if (!window) {
        std::cerr << "gui: glfwCreateWindow failed\n";
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    gui::render_init();
    if (!gui::load_fonts()) {
        std::cerr << "gui: could not load system font (times.ttf) — closing\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return;
    }

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    gui::set_viewport(fb_w, fb_h);

    gui::SettingsPanel panel;
    bool mouse_down_prev = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double mx = 0, my = 0;
        glfwGetCursorPos(window, &mx, &my);
        g_input.mouse_x = mx;
        g_input.mouse_y = my;

        bool mouse_down_cur = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        g_input.mouse_pressed = mouse_down_cur && !mouse_down_prev;
        g_input.mouse_released = !mouse_down_cur && mouse_down_prev;
        mouse_down_prev = mouse_down_cur;

        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        panel.frame(g_input, fb_w, fb_h);

        if (g_input.key_escape) glfwSetWindowShouldClose(window, GLFW_TRUE);

        glfwSwapBuffers(window);

        // Edge-triggered/accumulated input has now been consumed for this
        // frame — clear it before the next poll picks up new events.
        g_input.typed.clear();
        g_input.key_backspace = g_input.key_enter = g_input.key_escape = false;
        g_input.scroll_y = 0;
    }

    gui::render_shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
}

}  // namespace inop
