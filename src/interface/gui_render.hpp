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

// Shifts everything drawn after it by (dx, dy) logical units. One call
// moves a whole screen, which is what lets two of them be drawn side by
// side while one slides off and the other slides on.
//
// A render target would be the other way to do that, and is the wrong one
// here: an FBO wants a context this renderer deliberately does not ask
// for. See the header comment about the compatibility context.
//
// Scissor rectangles follow the offset too, or a panel that clips its own
// scrolling content would clip it where the panel is not.
void set_draw_offset(float dx, float dy);

// How many pixels one logical unit is worth. Everything drawn is in
// logical units, so raising this grows layout and text together and
// nothing can overflow a control that fitted at 1.0 — which is what
// separates a zoom from a font size. Callers work out their own logical
// window size as pixels divided by this, and must divide incoming mouse
// coordinates by it too, or hit testing lands in the wrong place.
//
// Re-applies the projection immediately using the framebuffer size last
// handed to set_viewport, so a change takes effect on the next frame
// without waiting for a resize.
void set_ui_scale(float scale);
float ui_scale();

void clear(Color background);

void draw_rect(float x, float y, float w, float h, Color c);
void draw_rect_outline(float x, float y, float w, float h, Color c, float thickness = 1.0f);

// Restricts drawing to the given rect (in the same top-left-origin space
// as everything else) until end_scissor(). Needed for content that can be
// partially off its own bounds by design — a dropdown popup scrolled by a
// fraction of a row — since draw_rect/draw_text otherwise draw their full
// extent with no cropping, bleeding into whatever sits just outside that
// rect. Nestable: an inner rect is intersected with the one already in
// force, so a clip inside a clip means both, and end_scissor() puts the
// outer one back rather than dropping the clip altogether.
void begin_scissor(float x, float y, float w, float h);
void end_scissor();

// BodyLarge is the same typeface as Body, just baked at a bigger point
// size — used where something needs visual prominence (the alphabet
// strip) without switching to the Wordmark typeface.
enum class Font { Body, Wordmark, BodyLarge };

// Bakes all three atlases from one typeface, named by its filename inside
// the system font directory (Times New Roman by default). One consistent
// face across the whole panel, at three sizes. Returns false — with a
// stderr message — if the file is missing, rather than falling back to a
// blank window.
//
// Safe to call again on a live context: any atlas already baked is torn
// down first, so the settings screen can change the typeface without
// leaking a texture per change. A failed re-bake leaves no atlases at
// all, so callers that can carry on (the settings screen, which has a
// known-good face to fall back to) must re-call with one that works.
bool load_fonts(const std::string& font_file = "cour.ttf");

// Counts how many times an atlas has been baked. Anything that caches a
// measurement taken from the atlas has to notice a re-bake, because the
// same string is a different width in a different typeface. Starts at 1,
// so a cache that begins at 0 always misses on its first look.
unsigned font_generation();

float text_width(Font font, const std::string& text);
float text_line_height(Font font);
void draw_text(Font font, float x, float baseline_y, const std::string& text, Color c);

// -- preview faces -------------------------------------------------------
//
// A second, separate set of atlases, one per typeface rather than one per
// size, so a list of font names can draw each name in the face it names.
// The three atlases above are the interface itself and are all one face;
// these exist only to be looked at.
//
// Baked on first ask and then kept, because the font list redraws every
// frame it is open and a bake is a file read plus a texture upload. They
// are small: body size only, and a bitmap a quarter the width of the ones
// the interface uses, which is all 96 glyphs at that size need.
//
// Comes back false when the file cannot be found or cannot be baked -- a
// caller that gets false must draw the name in the interface face
// instead, since a preview that silently drew nothing would leave a blank
// row where a font name should be.
bool preview_font_ready(const std::string& font_file);

// Whether this face draws the latin alphabet at all. SGA is a rune
// alphabet with its runes sitting in the latin letter slots, so it bakes
// and draws perfectly and still cannot spell its own name. That cannot be
// detected from the baked atlas -- the glyphs are there and are not blank,
// they are simply not letters -- so it is a short list of known faces,
// the same way typeface_size_scale() is. Anything not on it draws latin.
bool typeface_draws_latin(const std::string& font_file);

// Both do nothing and return 0 unless preview_font_ready() has already
// said yes for this file.
float preview_text_width(const std::string& font_file, const std::string& text);
float preview_line_height(const std::string& font_file);
void draw_preview_text(const std::string& font_file, float x, float baseline_y,
                       const std::string& text, Color c);

}  // namespace gui
}  // namespace inop
