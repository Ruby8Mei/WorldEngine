// gui_render.hpp — pure OpenGL 1.1 drawing primitives + stb_truetype text.
//
// No mouse/keyboard knowledge lives here — see gui_widgets.hpp for that.
// Deliberately fixed-function (glBegin/glOrtho/glTexImage2D — all genuine
// GL 1.1, statically exported by opengl32.dll on Windows) rather than a
// shader/VBO pipeline, so no GL loader (GLAD/GLEW) is needed on top of
// GLFW + stb_truetype.
#pragma once

#include <string>

namespace inop {
namespace gui {

struct Color {
    float r, g, b, a;
};

inline Color rgba(float r, float g, float b, float a = 1.0f) { return Color{r, g, b, a}; }

// Call once, right after the GL context is current.
bool render_init();
void render_shutdown();

// Call whenever the framebuffer size changes (including at startup).
void set_viewport(int width, int height);

void clear(Color background);

void draw_rect(float x, float y, float w, float h, Color c);
void draw_rect_outline(float x, float y, float w, float h, Color c, float thickness = 1.0f);

// Restricts drawing to the given rect (in the same top-left-origin space
// as everything else) until end_scissor(). Needed for content that can be
// partially off its own bounds by design — a dropdown popup scrolled by a
// fraction of a row — since draw_rect/draw_text otherwise draw their full
// extent with no cropping, bleeding into whatever sits just outside that
// rect. Not nestable: only one scissor rect is active at a time.
void begin_scissor(float x, float y, float w, float h);
void end_scissor();

// BodyLarge is the same typeface as Body, just baked at a bigger point
// size — used where something needs visual prominence (the alphabet
// strip) without switching to the Wordmark typeface.
enum class Font { Body, Wordmark, BodyLarge };

// Bakes all font atlases from Times New Roman, one consistent typeface
// across the whole panel. Returns false — with a stderr message — if the
// file is missing, rather than falling back to a blank window.
bool load_fonts();

float text_width(Font font, const std::string& text);
float text_line_height(Font font);
void draw_text(Font font, float x, float baseline_y, const std::string& text, Color c);

}  // namespace gui
}  // namespace inop
